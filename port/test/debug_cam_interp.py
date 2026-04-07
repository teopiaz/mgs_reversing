#!/usr/bin/env python3
"""
debug_cam_interp.py — Peek raw camera interpolation values during movement.

Reads GM_Camera.eye (target), gUnkCameraStruct2_800B7868.eye (smoothed),
and DG_Chanls[1].eye.t (final) every frame during slight LEFT then RIGHT.
"""

import json, os, socket, struct, subprocess, sys

SOCK = "/tmp/mgs_test.sock"

# Addresses from nm + ASLR slide (computed at runtime)
STATIC_GM_CAMERA = 0x100669080
STATIC_STRUCT2   = 0x1006782d4
STATIC_STRUCTB   = 0x1006782f8

BTN_LEFT  = 0x8000
BTN_RIGHT = 0x2000


def send(sock, cmd, **kw):
    msg = json.dumps({"cmd": cmd, **kw}) + "\n"
    sock.sendall(msg.encode())
    buf = b""
    while b"\n" not in buf:
        buf += sock.recv(65536)
    return json.loads(buf.split(b"\n")[0])


def peek_svec(sock, addr):
    """Read an SVECTOR (4 shorts) at addr, return (vx, vy, vz)."""
    r = send(sock, "peek", addr=f"{addr:x}", size=8)
    d = bytes.fromhex(r["data"])
    vx, vy, vz, _ = struct.unpack("<hhhh", d)
    return vx, vy, vz


def get_slide():
    """Compute ASLR slide from vmmap."""
    pid = subprocess.check_output(["pgrep", "-x", "mgs"]).decode().strip().split("\n")[0]
    vm = subprocess.check_output(["vmmap", pid], stderr=subprocess.DEVNULL).decode()
    for line in vm.splitlines():
        if "__TEXT" in line and "/mgs" in line:
            addr_str = line.split()[1].split("-")[0]
            runtime_base = int(addr_str, 16)
            return runtime_base - 0x100000000
    raise RuntimeError("Could not determine ASLR slide")


def main():
    slide = get_slide()
    gm_cam_addr = STATIC_GM_CAMERA + slide
    struct2_addr = STATIC_STRUCT2 + slide
    print(f"ASLR slide: 0x{slide:x}")
    print(f"GM_Camera:          0x{gm_cam_addr:x}")
    print(f"gUnkCameraStruct2:  0x{struct2_addr:x}")

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(SOCK)
    s.settimeout(None)

    # Baseline
    st = send(s, "get_state")
    snake = st["snake"]["pos"]
    cam = st["camera"]["pos"]
    print(f"\nBASELINE: snake_x={snake[0]} cam_x={cam[0]} dx={cam[0]-snake[0]}")

    target = peek_svec(s, gm_cam_addr)
    smooth = peek_svec(s, struct2_addr)
    print(f"  GM_Camera.eye.vx={target[0]}  struct2.eye.vx={smooth[0]}")

    # Move LEFT 15 frames, sampling every frame
    print(f"\n{'frame':>5} {'snake_x':>8} {'target_x':>9} {'smooth_x':>9} {'final_x':>8} {'tgt-smth':>9} {'smth-snk':>9}")
    print("-" * 75)

    send(s, "inject_input", buttons=BTN_LEFT, frames=15)
    for i in range(15):
        send(s, "run", frames=1)
        st = send(s, "get_state")
        snake_x = st["snake"]["pos"][0]
        final_x = st["camera"]["pos"][0]
        target = peek_svec(s, gm_cam_addr)
        smooth = peek_svec(s, struct2_addr)
        diff_ts = target[0] - smooth[0]
        diff_sf = smooth[0] - snake_x
        print(f"L {i+1:>3}  {snake_x:>8} {target[0]:>9} {smooth[0]:>9} {final_x:>8} {diff_ts:>9} {diff_sf:>9}")

    # Stop 5 frames
    for i in range(5):
        send(s, "run", frames=1)
        st = send(s, "get_state")
        snake_x = st["snake"]["pos"][0]
        final_x = st["camera"]["pos"][0]
        target = peek_svec(s, gm_cam_addr)
        smooth = peek_svec(s, struct2_addr)
        diff_ts = target[0] - smooth[0]
        diff_sf = smooth[0] - snake_x
        print(f"S {i+1:>3}  {snake_x:>8} {target[0]:>9} {smooth[0]:>9} {final_x:>8} {diff_ts:>9} {diff_sf:>9}")

    # Move RIGHT 15 frames
    send(s, "inject_input", buttons=BTN_RIGHT, frames=15)
    for i in range(15):
        send(s, "run", frames=1)
        st = send(s, "get_state")
        snake_x = st["snake"]["pos"][0]
        final_x = st["camera"]["pos"][0]
        target = peek_svec(s, gm_cam_addr)
        smooth = peek_svec(s, struct2_addr)
        diff_ts = target[0] - smooth[0]
        diff_sf = smooth[0] - snake_x
        print(f"R {i+1:>3}  {snake_x:>8} {target[0]:>9} {smooth[0]:>9} {final_x:>8} {diff_ts:>9} {diff_sf:>9}")

    # Stop 10 frames to see settling
    for i in range(10):
        send(s, "run", frames=1)
        st = send(s, "get_state")
        snake_x = st["snake"]["pos"][0]
        final_x = st["camera"]["pos"][0]
        target = peek_svec(s, gm_cam_addr)
        smooth = peek_svec(s, struct2_addr)
        diff_ts = target[0] - smooth[0]
        diff_sf = smooth[0] - snake_x
        print(f"S {i+1:>3}  {snake_x:>8} {target[0]:>9} {smooth[0]:>9} {final_x:>8} {diff_ts:>9} {diff_sf:>9}")

    s.close()


if __name__ == "__main__":
    main()
