#!/usr/bin/env python3
"""Pipe a TOTP secret to Flipper NFC TOTP over USB CDC (RAM only on device).

Requires the NFC TOTP FAP to be running (registers `totp_put`).
Does not write the secret to the Flipper SD card or to local files.

Examples:
  python3 tools/companion.py --name 'GitHub:you' --secret JBSWY3DPEHPK3PXP
  python3 tools/companion.py --name GitHub:you --secret hex:DEADBEEFCAFE --digits 6
"""

from __future__ import annotations

import argparse
import re
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


def cli_quote(s: str) -> str:
    """Always quote for Flipper args_read_probably_quoted_string_and_trim."""
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> int:
    ap = argparse.ArgumentParser(description="Send totp_put to Flipper NFC TOTP (RAM-only)")
    ap.add_argument("--port", help="Serial port (default: auto-detect Flipper)")
    ap.add_argument(
        "--name",
        required=True,
        help="Credential name (Yubico-style Issuer:account is fine; always quoted on the wire)",
    )
    ap.add_argument(
        "--secret",
        required=True,
        help="Base32 secret, or hex:DEADBEEF / 0xDEADBEEF",
    )
    ap.add_argument("--digits", type=int, choices=(6, 8), default=6, help="Code digits (default 6)")
    ap.add_argument("--baud", type=int, default=230400)
    ap.add_argument(
        "--hex",
        action="store_true",
        help="Treat --secret as raw hex (prefix hex: automatically)",
    )
    args = ap.parse_args()

    port = args.port or find_flipper_port()
    if not port:
        print("No Flipper port found; pass --port /dev/ttyACM0", file=sys.stderr)
        return 1

    secret = args.secret.strip()
    if args.hex and not (secret.lower().startswith("hex:") or secret.lower().startswith("0x")):
        secret = "hex:" + secret

    # Always quote name+secret so ':' and spaces survive Flipper tokenization.
    cmd = f"totp_put {cli_quote(args.name.strip())} {cli_quote(secret)} {args.digits}\r"

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

    text = re.sub(r"\x1b\[[0-9;]*[mA-Za-z]", "", buf.decode("utf-8", errors="replace"))
    # Do not echo the secret back if the device reflected the command line.
    safe = re.sub(re.escape(secret), "<secret>", text, flags=re.IGNORECASE)
    print(safe.strip())
    if "OK:" in text:
        print("(Present the card on the Flipper when it shows Place card / PUT.)")
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
