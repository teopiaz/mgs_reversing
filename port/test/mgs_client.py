"""
mgs_client.py — Python client for the MGS port test harness.

Connects to the Unix socket server in the mgs binary (test_server.c) and
provides a clean API for driving the game, inspecting state, and running tests.

Usage:
    from mgs_client import MGSTestClient

    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log("../testcase/1_rendering_left_side_s01a.log")
        print(state["camera"]["pos"], state["snake"]["pos"])
"""

import json
import os
import select
import socket
import subprocess
import sys
import time


SOCKET_PATH = "/tmp/mgs_test.sock"

# Set by testcase_runner.py --no-headless; tests that use the default
# MGSTestClient() constructor inherit this setting automatically.
_default_headless = True


class MGSTestError(Exception):
    pass


class MGSTestClient:
    def __init__(
        self,
        binary=None,
        socket_path=SOCKET_PATH,
        timeout=30,
        auto_input=True,
        headless=None,
        recv_timeout=60,
    ):
        if binary is None:
            binary = os.path.join(os.path.dirname(__file__), "..", "mgs")
        self.binary = os.path.abspath(binary)
        self.socket_path = socket_path
        self.timeout = timeout
        self.auto_input = auto_input
        self.headless = headless if headless is not None else _default_headless
        self.recv_timeout = recv_timeout
        self.proc = None
        self.sock = None
        self._buf = b""

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def start(self, env_extras=None, auto_input=None, headless=None):
        """Start the game process and connect to the test socket."""
        if auto_input is None:
            auto_input = self.auto_input
        if headless is None:
            headless = self.headless
        env = dict(os.environ)
        if headless:
            env["SDL_VIDEODRIVER"] = "dummy"
            env["SDL_AUDIODRIVER"] = "dummy"
        if auto_input:
            env["MGS_AUTO_INPUT"] = "1"
        if env_extras:
            env.update(env_extras)

        # Remove any stale socket so _wait_connect only connects to the new game.
        # Without this, _wait_connect may connect to a lingering game from a
        # previous test before the new game's TEST_HARNESS_init() runs.
        try:
            os.unlink(self.socket_path)
        except FileNotFoundError:
            pass

        # Capture stdout/stderr for debugging; can be None for quiet mode
        self.proc = subprocess.Popen(
            [self.binary],
            env=env,
            cwd=os.path.dirname(self.binary),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self._wait_connect()

    def stop(self):
        """Gracefully quit the game and close the connection."""
        try:
            self.send("quit")
        except Exception:
            pass
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None
        if self.proc:
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.terminate()
                try:
                    self.proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait()
            self.proc = None

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *_):
        self.stop()

    # ------------------------------------------------------------------
    # Low-level protocol
    # ------------------------------------------------------------------

    def send(self, cmd, **kwargs):
        """Send a command and return the parsed JSON response."""
        msg = json.dumps({"cmd": cmd, **kwargs}) + "\n"
        self.sock.sendall(msg.encode())
        resp = json.loads(self._recv_line())
        if not resp.get("ok"):
            raise MGSTestError(f"Command {cmd!r} failed: {resp.get('error', resp)}")
        return resp

    def _wait_connect(self):
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.settimeout(2)
                s.connect(self.socket_path)
                # Switch to blocking for normal operations — run(N) may take
                # many seconds when N is large (e.g. run_log with 1751 frames).
                s.settimeout(None)
                self.sock = s
                return
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(0.1)
            except Exception as e:
                time.sleep(0.1)
        raise TimeoutError(
            f"Game did not open test socket within {self.timeout}s"
        )

    def _recv_line(self):
        deadline = time.monotonic() + self.recv_timeout
        while b"\n" not in self._buf:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(
                    f"No response from game within {self.recv_timeout}s"
                )
            # Check if the game process is still alive — if it crashed,
            # don't wait for the full recv_timeout.
            if self.proc and self.proc.poll() is not None:
                raise ConnectionError(
                    f"Game process exited (code={self.proc.returncode})"
                )
            ready, _, _ = select.select([self.sock], [], [], min(remaining, 2.0))
            if not ready:
                continue
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("Game disconnected")
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        return line.decode()

    # ------------------------------------------------------------------
    # Control commands
    # ------------------------------------------------------------------

    def status(self):
        """Return current frame, load_complete, gv_time."""
        return self.send("status")

    def run(self, frames=1):
        """Advance the game by N frames and return status."""
        return self.send("run", frames=frames)

    def step(self):
        """Advance exactly one frame."""
        return self.send("step")

    def step_for_frames(self, frames):
        """Advance N frames (alias for run, explicit name for clarity)."""
        return self.send("step_for_frames", frames=frames)

    def pause(self):
        """Pause: game blocks in test_server_tick until step/run."""
        return self.send("pause")

    def resume(self):
        """Resume from pause without advancing a frame."""
        return self.send("resume")

    # ------------------------------------------------------------------
    # Input
    # ------------------------------------------------------------------

    def inject_input(self, buttons, frames=1):
        """Override pad buttons for next N frames.

        Button bits match mts.c BTN_* constants (active-HIGH, same as log files):
        0x0001=L2  0x0002=R2  0x0004=L1  0x0008=R1
        0x0010=TRIANGLE 0x0020=CIRCLE 0x0040=CROSS 0x0080=SQUARE
        0x0100=SELECT   0x0800=START
        0x1000=UP  0x2000=RIGHT  0x4000=DOWN  0x8000=LEFT
        """
        return self.send("inject_input", buttons=buttons, frames=frames)

    def replay_log(self, path):
        """Switch to replaying the given .log file (resets frame counter)."""
        path = os.path.abspath(path) if not os.path.isabs(path) else path
        return self.send("replay_log", path=path)

    def record_log(self, path):
        """Start recording input to the given .log file."""
        path = os.path.abspath(path) if not os.path.isabs(path) else path
        return self.send("record_log", path=path)

    def stop_log(self):
        """Stop recording."""
        return self.send("stop_log")

    # ------------------------------------------------------------------
    # Inspection
    # ------------------------------------------------------------------

    def get_state(self):
        """Full game state: camera, snake, actors count, replay status."""
        return self.send("get_state")

    def get_actors(self):
        """List of all active actors with priority, name, count, runtime."""
        return self.send("get_actors")

    def get_mesh(self, simplified=False):
        """3D mesh of current render queue.

        simplified=True returns only bounding boxes (much smaller response).
        Full mesh can be 5-20 MB for a complete stage.
        """
        return self.send("get_mesh", simplified=1 if simplified else 0)

    def get_collision(self):
        """Active HZD collision data: walls, floors, triggers."""
        return self.send("get_collision")

    def screenshot(self, path):
        """Save current framebuffer as a BMP to the given path."""
        path = os.path.abspath(path) if not os.path.isabs(path) else path
        return self.send("screenshot", path=path)

    # ------------------------------------------------------------------
    # High-level helpers
    # ------------------------------------------------------------------

    def wait_for_stage(self, timeout_frames=3000, batch=30):
        """Run until GM_LoadComplete==1. Returns number of frames waited.

        Runs in batches of `batch` frames to reduce round-trip overhead.
        """
        waited = 0
        while waited < timeout_frames:
            n = min(batch, timeout_frames - waited)
            r = self.run(n)
            waited += n
            if r.get("load_complete"):
                return waited
        raise TimeoutError(
            f"Stage did not finish loading after {timeout_frames} frames"
        )

    def run_log(self, log_path):
        """Replay a full input log and return final state.

        Automatically determines the last frame from the log file.
        Runs 30 extra frames past the end so the final state settles.
        """
        log_path = os.path.abspath(log_path)
        self.replay_log(log_path)
        last_frame = _log_last_frame(log_path)
        self.run(last_frame + 30)
        return self.get_state()

    def run_to_frame(self, target_frame):
        """Run until GV_Time reaches target_frame."""
        r = self.status()
        current = r.get("frame", 0)
        if target_frame > current:
            self.run(target_frame - current)
        return self.get_state()

    def drain_stdout(self, max_lines=200):
        """Read and return pending stdout from the game process (non-blocking)."""
        if not self.proc:
            return []
        lines = []
        while select.select([self.proc.stdout], [], [], 0)[0]:
            line = self.proc.stdout.readline()
            if not line:
                break
            lines.append(line.decode(errors="replace").rstrip())
            if len(lines) >= max_lines:
                break
        return lines


# ------------------------------------------------------------------
# Utilities
# ------------------------------------------------------------------

def _log_last_frame(path):
    """Return the last frame number in a .log file."""
    last = 0
    with open(path) as f:
        for line in f:
            parts = line.split()
            if parts:
                try:
                    last = int(parts[0])
                except ValueError:
                    pass
    return last


# PSX button constants (active-HIGH, matching mts.c BTN_* and .log format)
# These MUST match the BTN_ defines in port/mts/mts.c exactly.
BTN_L2       = 0x0001
BTN_R2       = 0x0002
BTN_L1       = 0x0004
BTN_R1       = 0x0008
BTN_TRIANGLE = 0x0010
BTN_CIRCLE   = 0x0020
BTN_CROSS    = 0x0040
BTN_SQUARE   = 0x0080
BTN_SELECT   = 0x0100
BTN_START    = 0x0800
BTN_UP       = 0x1000
BTN_RIGHT    = 0x2000
BTN_DOWN     = 0x4000
BTN_LEFT     = 0x8000


if __name__ == "__main__":
    # Quick connectivity check
    with MGSTestClient() as c:
        s = c.status()
        print(f"Connected: frame={s['frame']} load_complete={s['load_complete']}")
        c.wait_for_stage()
        state = c.get_state()
        print(f"Stage loaded: frame={state['frame']}")
        print(f"  Snake pos:  {state['snake']['pos']}")
        print(f"  Camera pos: {state['camera']['pos']}")
        print(f"  Faces drawn: {state['faces_last_frame']}")
