#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOTP_PREFS_PASSWORD_MAX 64

typedef struct {
    /* (Flipper local wall − UTC) in minutes.
     * UTC+2 → +120; America/Chicago CDT (UTC−5) → −300; CST → −360. */
    int16_t tz_offset_minutes;
    bool auto_unlock; /* if true, oath_password may be stored on Flipper storage */
    char oath_password[TOTP_PREFS_PASSWORD_MAX];
    /* v3: preferred Type transport for single-code Right button. */
    bool type_via_bt; /* false = USB HID (default); true = BLE HID */
    /* v4: master switch for in-app Bluetooth HID (radio quiet when false). */
    bool bt_enabled;
} TotpPrefs;

void totp_prefs_set_defaults(TotpPrefs* prefs);
bool totp_prefs_load(TotpPrefs* prefs);
bool totp_prefs_save(const TotpPrefs* prefs);

#ifdef __cplusplus
}
#endif
