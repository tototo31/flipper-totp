#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOTP_PREFS_PASSWORD_MAX 64

typedef struct {
    int16_t tz_offset_minutes; /* Flipper local − UTC, e.g. UTC+2 → 120 */
    char oath_password[TOTP_PREFS_PASSWORD_MAX];
} TotpPrefs;

void totp_prefs_set_defaults(TotpPrefs* prefs);
bool totp_prefs_load(TotpPrefs* prefs);
bool totp_prefs_save(const TotpPrefs* prefs);

#ifdef __cplusplus
}
#endif
