#include "prefs.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#include <string.h>

#define TOTP_PREFS_PATH APP_DATA_PATH("totp_nfc_prefs.bin")
#define TOTP_PREFS_MAGIC 0x54
#define TOTP_PREFS_VERSION 1

void totp_prefs_set_defaults(TotpPrefs* prefs) {
    memset(prefs, 0, sizeof(*prefs));
    prefs->tz_offset_minutes = 0;
}

bool totp_prefs_load(TotpPrefs* prefs) {
    totp_prefs_set_defaults(prefs);
    return saved_struct_load(
        TOTP_PREFS_PATH, prefs, sizeof(*prefs), TOTP_PREFS_MAGIC, TOTP_PREFS_VERSION);
}

bool totp_prefs_save(const TotpPrefs* prefs) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, STORAGE_APP_DATA_PATH_PREFIX);
    furi_record_close(RECORD_STORAGE);

    return saved_struct_save(
        TOTP_PREFS_PATH, prefs, sizeof(*prefs), TOTP_PREFS_MAGIC, TOTP_PREFS_VERSION);
}
