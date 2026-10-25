#include "prefs.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#include <string.h>

#define TOTP_PREFS_PATH APP_DATA_PATH("totp_nfc_prefs.bin")
#define TOTP_PREFS_MAGIC 0x54
#define TOTP_PREFS_VERSION 4
#define TOTP_PREFS_VERSION_V3 3

/* Layout of prefs before bt_enabled (v3). */
typedef struct {
    int16_t tz_offset_minutes;
    bool auto_unlock;
    char oath_password[TOTP_PREFS_PASSWORD_MAX];
    bool type_via_bt;
} TotpPrefsV3;

void totp_prefs_set_defaults(TotpPrefs* prefs) {
    memset(prefs, 0, sizeof(*prefs));
    prefs->tz_offset_minutes = 0;
    prefs->auto_unlock = false;
    prefs->type_via_bt = false;
    prefs->bt_enabled = true; /* feature available; Off = quiet radio */
}

bool totp_prefs_load(TotpPrefs* prefs) {
    totp_prefs_set_defaults(prefs);
    bool ok = saved_struct_load(
        TOTP_PREFS_PATH, prefs, sizeof(*prefs), TOTP_PREFS_MAGIC, TOTP_PREFS_VERSION);
    if(!ok) {
        TotpPrefsV3 old;
        memset(&old, 0, sizeof(old));
        if(saved_struct_load(
               TOTP_PREFS_PATH,
               &old,
               sizeof(old),
               TOTP_PREFS_MAGIC,
               TOTP_PREFS_VERSION_V3)) {
            prefs->tz_offset_minutes = old.tz_offset_minutes;
            prefs->auto_unlock = old.auto_unlock;
            memcpy(prefs->oath_password, old.oath_password, sizeof(prefs->oath_password));
            prefs->type_via_bt = old.type_via_bt;
            prefs->bt_enabled = true;
            ok = totp_prefs_save(prefs);
        }
    }
    /* Never keep a stored password unless Auto-unlock is explicitly on. */
    if(!prefs->auto_unlock && prefs->oath_password[0] != '\0') {
        memset(prefs->oath_password, 0, sizeof(prefs->oath_password));
        if(ok) {
            totp_prefs_save(prefs);
        }
    }
    /* BT Off forces USB as the preferred Type transport. */
    if(!prefs->bt_enabled) {
        prefs->type_via_bt = false;
    }
    return ok;
}

bool totp_prefs_save(const TotpPrefs* prefs) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, STORAGE_APP_DATA_PATH_PREFIX);
    furi_record_close(RECORD_STORAGE);

    return saved_struct_save(
        TOTP_PREFS_PATH, prefs, sizeof(*prefs), TOTP_PREFS_MAGIC, TOTP_PREFS_VERSION);
}
