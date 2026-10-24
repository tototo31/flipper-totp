# Flipper TOTP NFC

Momentum FAP that reads **YKOATH** TOTP codes over NFC from **VivoKey Apex** and YubiKey-compatible cards, then shows them with a 30-second countdown.

Supports SELECT, optional VALIDATE, LIST, CALCULATE / CALCULATE ALL, PUT, DELETE, and SEND REMAINING. No SET CODE / RESET yet.

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

1. Open **TOTP NFC** from the NFC apps menu.
2. Set **TZ offset** if the Flipper clock is not UTC (example: UTC+2 → `120`). Challenge time is `floor((rtc − offset×60) / 30)`.
3. **Read TOTP** — CALCULATE ALL; place the card (LED blinks while waiting).
4. **List credentials** — LIST, then Calculate (single) or Delete a named account.
5. **Add credential** — name → Base32 or hex secret → digits → PUT on next tap.
6. If SELECT returns challenge tag `0x74`, enter the OATH password (PBKDF2-HMAC-SHA1, 1000 iterations, salt = SELECT id — same as VivoKey `apex-totp` `client.pl`).
7. Read/Calculate code lists omit touch-required (`0x7c`) entries from CALCULATE ALL; LIST still shows HOTP names so you can delete them. When the 30s window ends, codes are masked (`******`); press OK and tap the card again to reread.

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
| `scenes/` | Start → Scan → (PIN) → Codes; TZ settings |
| `ykoath.c` / `ykoath.h` | TLV + SELECT / VALIDATE / CALCULATE ALL / SEND REMAINING |
| `nfc_session.c` / `nfc_session.h` | Nfc poller lifecycle; APDUs in poller callback |
| `prefs.c` / `prefs.h` | TZ offset + optional OATH password |
| `crypto_hmac.c` / `crypto_hmac.h` | HMAC-SHA1 + PBKDF2 (FAP-local; firmware HMAC not exported) |
