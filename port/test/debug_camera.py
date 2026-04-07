#!/usr/bin/env python3
"""
debug_camera.py — Move Snake around s01a and track camera behavior.

Connects to a RUNNING game (already in s01a) and systematically moves Snake
in all 4 directions, sampling camera/snake position every N frames.
"""

import json
import os
import socket
import sys

SOCK = "/tmp/mgs_test.sock"
SCREENSHOT_DIR = "/tmp/cam_debug"
os.makedirs(SCREENSHOT_DIR, exist_ok=True)

# Button values matching mts.c
BTN_UP    = 0x1000
BTN_DOWN  = 0x4000
BTN_LEFT  = 0x8000
BTN_RIGHT = 0x2000
BTN_L1    = 0x0004

DIR_MAP = {
    "UP":    BTN_UP,
    "DOWN":  BTN_DOWN,
    "LEFT":  BTN_LEFT,
    "RIGHT": BTN_RIGHT,
}


def send(sock, cmd, **kw):
    msg = json.dumps({"cmd": cmd, **kw}) + "\n"
    sock.sendall(msg.encode())
    buf = b""
    while b"\n" not in buf:
        buf += sock.recv(65536)
    return json.loads(buf.split(b"\n")[0])


def get_state(sock):
    return send(sock, "get_state")


def screenshot(sock, name):
    path = os.path.join(SCREENSHOT_DIR, f"{name}.bmp")
    send(sock, "screenshot", path=path)
    os.system(f"sips -s format png '{path}' --out '{path.replace('.bmp','.png')}' 2>/dev/null")


def main():
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(SOCK)
    s.settimeout(None)

    # First, let alert die down — run 300 frames with no input
    print("Waiting for alert to settle...")
    send(s, "run", frames=300)
    state = get_state(s)
    print(f"  alert_mode={state['alert_mode']} alert_level={state['alert_level']}")

    # Baseline position
    state = get_state(s)
    base_snake = state["snake"]["pos"]
    base_cam   = state["camera"]["pos"]
    print(f"\n{'='*80}")
    print(f"BASELINE: snake={base_snake} camera={base_cam}")
    print(f"  delta: cam-snake = [{base_cam[0]-base_snake[0]}, {base_cam[1]-base_snake[1]}, {base_cam[2]-base_snake[2]}]")
    print(f"  faces={state['faces_last_frame']} objs={state['objs_count']}")
    print(f"{'='*80}")
    screenshot(s, "00_baseline")

    # Move in each direction: hold for 60 frames, sample every 15 frames
    results = []
    step_idx = 1

    for dir_name, btn in DIR_MAP.items():
        print(f"\n--- Moving {dir_name} (btn=0x{btn:04X}) ---")

        samples = []
        # Hold direction for 90 frames total, sampling every 15
        send(s, "inject_input", buttons=btn, frames=90)

        for i in range(6):
            send(s, "run", frames=15)
            st = get_state(s)
            sp = st["snake"]["pos"]
            cp = st["camera"]["pos"]
            dx = cp[0] - sp[0]
            dy = cp[1] - sp[1]
            dz = cp[2] - sp[2]
            dist = (dx*dx + dy*dy + dz*dz) ** 0.5
            samples.append({
                "frame": st["frame"],
                "snake": sp,
                "camera": cp,
                "delta": [dx, dy, dz],
                "dist": dist,
                "faces": st["faces_last_frame"],
            })
            print(f"  t={i*15:3d}  snake={sp}  cam={cp}  delta=[{dx},{dy},{dz}]  dist={dist:.0f}  faces={st['faces_last_frame']}")

        screenshot(s, f"{step_idx:02d}_{dir_name}_end")
        results.append({"direction": dir_name, "samples": samples})
        step_idx += 1

        # Settle for 30 frames (release input)
        send(s, "run", frames=30)

    # Now do a camera analysis: move in a circle and track cam vs snake
    print(f"\n{'='*80}")
    print("CIRCULAR MOVEMENT TEST (UP-RIGHT-DOWN-LEFT cycle)")
    print(f"{'='*80}")

    circle_dirs = ["UP", "RIGHT", "DOWN", "LEFT"] * 2  # two full circles
    for dir_name in circle_dirs:
        btn = DIR_MAP[dir_name]
        send(s, "inject_input", buttons=btn, frames=30)
        send(s, "run", frames=30)
        st = get_state(s)
        sp = st["snake"]["pos"]
        cp = st["camera"]["pos"]
        dx, dy, dz = cp[0]-sp[0], cp[1]-sp[1], cp[2]-sp[2]
        dist = (dx*dx + dy*dy + dz*dz) ** 0.5
        print(f"  {dir_name:5s}  snake={sp}  cam={cp}  delta=[{dx},{dy},{dz}]  dist={dist:.0f}  faces={st['faces_last_frame']}")

    screenshot(s, f"{step_idx:02d}_circle_end")
    step_idx += 1

    # First-person mode test: enter FP and track camera change
    print(f"\n{'='*80}")
    print("FIRST-PERSON CAMERA TEST (L1 hold)")
    print(f"{'='*80}")

    st_before = get_state(s)
    print(f"  Before L1: snake={st_before['snake']['pos']} cam={st_before['camera']['pos']}")
    screenshot(s, f"{step_idx:02d}_before_fp")
    step_idx += 1

    send(s, "inject_input", buttons=BTN_L1, frames=60)
    for i in range(4):
        send(s, "run", frames=15)
        st = get_state(s)
        sp = st["snake"]["pos"]
        cp = st["camera"]["pos"]
        print(f"  L1 t={i*15:3d}: snake={sp} cam={cp} faces={st['faces_last_frame']}")

    screenshot(s, f"{step_idx:02d}_during_fp")
    step_idx += 1

    # Release L1 and check camera returns
    send(s, "run", frames=30)
    st_after = get_state(s)
    print(f"  After L1:  snake={st_after['snake']['pos']} cam={st_after['camera']['pos']}")
    screenshot(s, f"{step_idx:02d}_after_fp")

    # Summary
    print(f"\n{'='*80}")
    print("CAMERA OFFSET SUMMARY")
    print(f"{'='*80}")
    print(f"{'Direction':>10} | {'Snake X':>8} {'Snake Z':>8} | {'Cam X':>8} {'Cam Z':>8} | {'dX':>6} {'dZ':>6} | {'Dist':>6} | {'Faces':>5}")
    print(f"{'-'*10}-+-{'-'*17}-+-{'-'*17}-+-{'-'*13}-+-{'-'*6}-+-{'-'*5}")
    for r in results:
        last = r["samples"][-1]
        sp, cp, d = last["snake"], last["camera"], last["delta"]
        print(f"{r['direction']:>10} | {sp[0]:>8} {sp[2]:>8} | {cp[0]:>8} {cp[2]:>8} | {d[0]:>6} {d[2]:>6} | {last['dist']:>6.0f} | {last['faces']:>5}")

    print(f"\nScreenshots in {SCREENSHOT_DIR}")
    s.close()


if __name__ == "__main__":
    main()
