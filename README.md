# Flipper TOTP NFC

Momentum FAP that reads **YKOATH** TOTP codes over NFC from **VivoKey Apex** and YubiKey-compatible cards, then shows them with a 30-second countdown.

Supports SELECT, optional VALIDATE, LIST, CALCULATE / CALCULATE ALL, PUT, DELETE, SET CODE / CLEAR CODE, RESET, and SEND REMAINING.

## Requirements

- Flipper Zero running [Momentum](https://momentum-fw.dev/) firmware
- NFC ISO14443-4A card with YKOATH applet (AID `A0 00 00 05 27 21 01`)

## Build (Momentum release SDK)

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install ufbt
ufbt update --index-url https://up.momentum-fw.dev/firmware/directory.json --channel release
ufbt
```

Output: `dist/totp_nfc.fap`.

### Optional: `dev` channel

If the **release** SDK is missing an NFC symbol you need (e.g. a newer `iso14443_4a_poller_*` helper), switch to the Momentum **dev** channel:

```bash
ufbt update --index-url https://up.momentum-fw.dev/firmware/directory.json --channel dev
ufbt
```

Match the channel to the firmware installed on the Flipper when possible.

## Install

Copy `dist/totp_nfc.fap` to `/ext/apps/NFC/` on the SD card, or with the Flipper connected over USB:

```bash
source .venv/bin/activate
ufbt launch
```

## Usage

1. Open **NFC TOTP** from the NFC apps menu.
2. **Settings** — Flipper RTC is local. Use **Set TZ from UTC unix** (paste phone UTC unix) to auto-compute `tz = local − UTC` minutes, or **Edit TZ minutes** manually (UTC+2 → `+120`; US Central CDT → `−300`, CST → `−360`). Challenge: `utc = rtc_local − tz×60`.
3. **Settings → Set OATH password** — YKOATH SET CODE (PBKDF2-HMAC-SHA1, salt = SELECT id). By default the password stays in **RAM for this session only**. **Auto-unlock** (off by default) must be confirmed with a warning — then the password is stored in Flipper app data for silent VALIDATE. Turning Auto-unlock off clears the stored password. **Clear OATH password** sends SET CODE with empty KEY TLV (`73 00`). **Reset OATH applet** requires confirm, then RESET (`04 DE AD`) — deletes all credentials and the card password.
4. **Read TOTP** — CALCULATE ALL; place the card (LED blinks while waiting).
5. **List credentials** — LIST, then Calculate (single) or Delete a named account.
6. **Add credential** — name → Base32 or hex secret → digits → PUT on next tap.
7. **Receive secret (USB)** — open the app (optionally **Receive secret**), then from a PC pipe the secret over USB CDC. The FAP registers `totp_put` only while running; the secret stays in RAM, arms NFC PUT (same Place-card flow), and is wiped after success/fail/cancel — never written under `/ext` or prefs.
8. If SELECT returns challenge tag `0x74`, enter the OATH password (PBKDF2-HMAC-SHA1, 1000 iterations, salt = SELECT id — same as VivoKey `apex-totp` `client.pl`).
9. Read/Calculate code lists omit touch-required (`0x7c`) entries from CALCULATE ALL; LIST still shows HOTP names so you can delete them. When the 30s window ends, codes are masked (`******`); press OK and tap the card again to reread.
10. **Type via USB (HID)** — open a single credential code view (List → Calculate, or pick one account). While the code is live, press **Right** (`Type`) to send the digits over USB as a keyboard, then Enter. Focus the password / OTP field on the PC first. Expired or starred (`******`) codes are never typed.

### Type via USB (HID keyboard)

Flipper must present as a **USB HID keyboard** while typing (`furi_hal_usb` → `usb_hid`, then `furi_hal_hid_kb_press` / `release`). That can conflict with the usual CDC serial used by `ufbt` / qFlipper / `totp_put` — serial may drop while HID is active and return afterward (expected; not treated as a Type failure). Focus the target field on the host before pressing **Right**; the app shows **Switching USB… / Waiting for host… / Typing…** and waits up to ~10s for HID ready. Bluetooth HID is not implemented yet.

### USB companion (`totp_put`)

Flipper CLI (app must be open):

```text
totp_put <name> <secret> [digits]
```

- `name` may be Yubico-style `Issuer:account` (quote if it has spaces).
- `secret` is **Base32 by default** (otpauth). For hex use `hex:DEADBEEF` / `0x…` or `companion.py --hex`.
- Short secrets (e.g. 16-char Base32 → 10 bytes) are **zero-padded to 14 bytes** before PUT (YubiKey/Apex reject shorter keys with SW `6A80`).

Example with the helper script (pyserial; secret only on argv / process memory — do not redirect logs to a file):

```bash
pip install pyserial
python3 tools/companion.py --name 'GitHub:you' --secret JBSWY3DPEHPK3PXP --digits 6
# or: python3 tools/companion.py --port /dev/ttyACM0 --name 'GitHub:you' --secret …
```

Then present the card when the Flipper shows **Place card / PUT credential**.

## Protocol notes

- Spec: [YKOATH Protocol](https://developers.yubico.com/OATH/YKOATH_Protocol.html)
- Card reference: [VivoKey/apex-totp @ e4ea9aa](https://github.com/VivoKey/apex-totp/tree/e4ea9aa812163eb29979723af4351aeef6f8d8b7)
- Transport: ISO 7816-4 APDUs over ISO14443-4A (`iso14443_4a_poller_send_block`, SEND REMAINING while `SW=61xx`)
- Truncated CALCULATE ALL / CALCULATE (`INS=0xA4` / `0xA2`, `P2=0x01`); display tag `0x76` → `code % 10^digits`
- LIST (`0xA1`), PUT (`0x01`, TOTP+HMAC-SHA1), DELETE (`0x02`)

## Layout

| File | Role |
|------|------|
| `application.fam` | FAP manifest (NFC category) |
| `app.c` / `app.h` | GUI shell, scene manager |
| `scenes/` | Start → Scan → (PIN) → Codes; TZ; Help; Receive secret |
| `cli_put.c` | USB CDC `totp_put` while FAP runs (RAM-only secret) |
| `hid_type.c` / `hid_type.h` | USB HID keyboard type-out of live OTP digits |
| `tools/companion.py` | PC helper for `totp_put` over pyserial |
| `ykoath.c` / `ykoath.h` | TLV + SELECT / VALIDATE / CALCULATE ALL / SEND REMAINING |
| `nfc_session.c` / `nfc_session.h` | Nfc poller lifecycle; APDUs in poller callback |
| `prefs.c` / `prefs.h` | TZ offset + optional OATH password |
| `crypto_hmac.c` / `crypto_hmac.h` | HMAC-SHA1 + PBKDF2 (FAP-local; firmware HMAC not exported) |
