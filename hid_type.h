#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** v1: always send Enter after the digits. */
#define TOTP_HID_ENTER_AFTER true

/**
 * Type a digit-only string over USB HID keyboard.
 * Switches USB to `usb_hid` if needed, waits for host connect, types each
 * digit (press/release), optionally Enter, then restores the prior USB mode.
 *
 * @param digits  NUL-terminated ASCII digits only (no '*', no letters)
 * @param press_enter  if true, press Return after the last digit
 * @return true on success
 */
bool totp_hid_type_digits(const char* digits, bool press_enter);

#ifdef __cplusplus
}
#endif
