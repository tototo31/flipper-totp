#!/usr/bin/env python3
"""Pipe a TOTP secret to Flipper NFC TOTP over USB CDC (RAM only on device).

Requires the NFC TOTP FAP to be running (registers `totp_put`).
Does not write the secret to the Flipper SD card or to local files.
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("Install pyserial: pip install pyserial", file=sys.stderr)
    sys.exit(1)


def find_flipper_port() -> str | None:
    for p in list_ports.comports():
        desc = f"{p.description} {p.manufacturer or ''} {p.product or ''}".lower()
        if "flipper" in desc or (p.vid == 0x0483 and p.pid == 0x5740):
            return p.device
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description="Send totp_put to Flipper NFC TOTP (RAM-only)")
    ap.add_argument("--port", help="Serial port (default: auto-detect Flipper)")
    ap.add_argument("--name", required=True, help="Credential name")
    ap.add_argument("--secret", required=True, help="Base32 or hex secret (not written to disk by this tool)")
    ap.add_argument("--digits", type=int, choices=(6, 8), default=6, help="Code digits (default 6)")
    ap.add_argument("--baud", type=int, default=230400)
    args = ap.parse_args()

    port = args.port or find_flipper_port()
    if not port:
        print("No Flipper port found; pass --port /dev/ttyACM0", file=sys.stderr)
        return 1

    # Build CLI line; avoid logging secret to stdout.
    # Prefer quoting name/secret for Flipper args_read_probably_quoted_string.
    def q(s: str) -> str:
        if any(c in s for c in ' \t"'):
            return '"' + s.replace('"', '\\"') + '"'
        return s

    cmd = f"totp_put {q(args.name)} {q(args.secret)} {args.digits}\r"

    ser = serial.Serial(port, args.baud, timeout=1)
    try:
        time.sleep(0.2)
        ser.reset_input_buffer()
        ser.write(b"\r")
        time.sleep(0.2)
        ser.reset_input_buffer()
        ser.write(cmd.encode("utf-8"))
        deadline = time.time() + 3.0
        buf = b""
        while time.time() < deadline:
            chunk = ser.read(4096)
            if chunk:
                buf += chunk
                if b"OK:" in buf or b"Error:" in buf or b"Usage:" in buf:
                    break
            else:
                time.sleep(0.05)
    finally:
        ser.close()

    text = buf.decode("utf-8", errors="replace")
    # Strip ANSI color codes for readability.
    import re

    text = re.sub(r"\x1b\[[0-9;]*[mA-Za-z]", "", text)
    print(text.strip())
    if "OK:" in text:
        print("(Present the card on the Flipper when it shows Place card / PUT.)")
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
