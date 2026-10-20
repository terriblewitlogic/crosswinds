#!/usr/bin/env python3
"""Observe a running X4 without resetting it. Requires its input diagnostic build.

S reports real button history; --key explicitly sends one diagnostic gesture.
--frame exports the MCU framebuffer, not a photograph of the e-paper panel.
"""
import argparse
from pathlib import Path
import re
import time

import serial

KEYS = {"back": "0", "ok": "1", "down": "2", "up": "3", "prev": "4", "next": "5"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/cu.usbmodem101")
    parser.add_argument("--key", choices=KEYS)
    parser.add_argument("--seconds", type=float, default=5)
    parser.add_argument("--frame", type=Path)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 0.15
    # False -> true after open resets this board. Set true BEFORE opening.
    port.dtr, port.rts = True, False
    port.open()
    try:
        with args.log.open("wb") as log:
            if args.key:
                port.write(KEYS[args.key].encode())
            deadline = time.monotonic() + args.seconds
            next_status = 0
            while time.monotonic() < deadline:
                if time.monotonic() >= next_status:
                    port.write(b"S")
                    next_status = time.monotonic() + 0.5
                data = port.read(4096)
                log.write(data)
                log.flush()
            if args.frame:
                port.write(b"F")
                deadline = time.monotonic() + 20
                header = b""
                while time.monotonic() < deadline:
                    line = port.readline()
                    if line == b"P4\n":
                        header = line + port.readline()
                        break
                    log.write(line)
                match = re.fullmatch(rb"P4\n(\d+) (\d+)\n", header)
                if not match:
                    raise RuntimeError("No framebuffer header from diagnostic firmware")
                width, height = map(int, match.groups())
                if (width, height) != (800, 480):
                    raise RuntimeError(f"Unexpected framebuffer dimensions: {width}x{height}")
                length = ((width + 7) // 8) * height
                pixels = bytearray()
                while len(pixels) < length and time.monotonic() < deadline:
                    pixels.extend(port.read(length - len(pixels)))
                if len(pixels) != length:
                    raise RuntimeError(f"Incomplete framebuffer: {len(pixels)}/{length}")
                args.frame.write_bytes(header + pixels)
                print(f"Framebuffer: {args.frame}")
        lines = args.log.read_text(errors="replace").splitlines()
        for prefix in ("X4 diagnostic key:", "X4 live:", "X4 physical:"):
            matches = [line for line in lines if line.startswith(prefix)]
            if matches:
                print(matches[-1])
        if not any(line.startswith("X4 physical:") for line in lines):
            raise RuntimeError("No live input-diagnostic response")
        if any("panic" in line.lower() or "Guru Meditation" in line for line in lines):
            raise RuntimeError("Device fault recorded")
    finally:
        port.close()


if __name__ == "__main__":
    main()
