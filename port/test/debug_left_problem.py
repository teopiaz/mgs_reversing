#!/usr/bin/env python3
"""Replay 1_rendering_left_problem_s01a.log and track camera per-frame."""

import json, os, socket, struct, subprocess, sys

SOCK = "/tmp/mgs_test.sock"
LOGFILE = os.path.join(os.path.dirname(__file__), "..", "testcase", "1_rendering_left_problem_s01a.log")

# Static addresses from nm
STATIC_GM_CAMERA = 0x100669080
STATIC_STRUCT2   = 0x1006782d4


def send(sock, cmd, **kw):
    msg = json.dumps({"cmd": cmd, **kw}) + "\n"
    sock.sendall(msg.encode())
    buf = b""
    while b"\n" not in buf:
        buf += sock.recv(65536)
    return json.loads(buf.split(b"\n")[0])


def peek_svec_x(sock, addr):
    r = send(sock, "peek", addr=f"{addr:x}", size=2)
    return struct.unpack("<h", bytes.fromhex(r["data"]))[0]


def get_slide():
    pid = subprocess.check_output(["pgrep", "-x", "mgs"]).decode().strip().split("\n")[0]
    vm = subprocess.check_output(["vmmap", pid], stderr=subprocess.DEVNULL).decode()
    for line in vm.splitlines():
        if "__TEXT" in line and "/mgs" in line:
            return int(line.split()[1].split("-")[0], 16) - 0x100000000
    raise RuntimeError("ASLR slide not found")


# Parse log to know what buttons are pressed at each frame
def parse_log(path):
    entries = []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2:
                entries.append((int(parts[0]), int(parts[1], 16)))
    return entries

BTN_NAMES = {0x0020: "CIRCLE", 0x4000: "DOWN", 0x2000: "RIGHT", 0x8000: "LEFT", 0x1000: "UP", 0x0000: "---"}


def main():
    slide = get_slide()
    gm_cam = STATIC_GM_CAMERA + slide
    struct2 = STATIC_STRUCT2 + slide

    log_entries = parse_log(LOGFILE)
    last_frame = log_entries[-1][0] if log_entries else 0

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(SOCK)
    s.settimeout(None)

    # Start replay
    send(s, "replay_log", path=os.path.abspath(LOGFILE))

    # Run to frame 140 (just before gameplay starts at ~177)
    send(s, "run", frames=140)
    st = send(s, "get_state")
    print(f"At frame 140: stage={st['stage']} snake={st['snake']['pos']} cam={st['camera']['pos']} faces={st['faces_last_frame']}")

    # Build a frame→button lookup
    btn_at = {}
    cur_btn = 0
    for frame, btn in log_entries:
        btn_at[frame] = btn

    # Now run frame-by-frame from 140 to end, tracking camera
    print(f"\n{'frm':>4} {'btn':>6} {'snake_x':>8} {'tgt_x':>7} {'s2_x':>7} {'cam_x':>7} {'dx':>6} {'faces':>5}")
    print("-" * 65)

    cur_btn_name = "---"
    for frame in range(140, last_frame + 30):
        send(s, "run", frames=1)

        # Only print on interesting frames (button changes or every 10th frame near movement)
        # Check if this frame has a button change
        show = False
        for lf, lb in log_entries:
            if lf == frame:
                cur_btn_name = BTN_NAMES.get(lb, f"0x{lb:04X}")
                show = True
                break

        if frame >= 170 and (frame % 5 == 0 or show):
            st = send(s, "get_state")
            snake_x = st["snake"]["pos"][0]
            cam_x = st["camera"]["pos"][0]
            tgt_x = peek_svec_x(s, gm_cam)
            s2_x = peek_svec_x(s, struct2)
            dx = cam_x - snake_x
            faces = st["faces_last_frame"]
            marker = " <--" if show else ""
            print(f"{frame:>4} {cur_btn_name:>6} {snake_x:>8} {tgt_x:>7} {s2_x:>7} {cam_x:>7} {dx:>6} {faces:>5}{marker}")

    # Screenshot final state
    send(s, "screenshot", path="/tmp/left_problem_final.bmp")
    os.system("sips -s format png /tmp/left_problem_final.bmp --out /tmp/left_problem_final.png 2>/dev/null")

    s.close()
    print("\nScreenshot: /tmp/left_problem_final.png")


if __name__ == "__main__":
    main()
