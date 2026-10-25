#include "../app.h"
#include "scenes.h"

#include <string.h>

static void totp_scene_codes_mask_inplace(YkoathCredential* cred) {
    size_t n = cred->digits ? cred->digits : strlen(cred->code);
    if(n < 4) n = 6;
    if(n >= sizeof(cred->code)) n = sizeof(cred->code) - 1;
    for(size_t i = 0; i < n; i++) {
        cred->code[i] = '*';
    }
    cred->code[n] = '\0';
}

static void totp_scene_codes_expire(TotpApp* app) {
    if(!app->codes_live) return;
    app->codes_live = false;
    for(size_t i = 0; i < app->credentials.count; i++) {
        totp_scene_codes_mask_inplace(&app->credentials.items[i]);
    }
    nfc_session_stop(app->nfc_session);
}

static void totp_scene_codes_refresh_view(TotpApp* app) {
    uint64_t utc = totp_app_unix_utc(app);
    if(app->codes_live && utc >= app->code_expires_utc) {
        totp_scene_codes_expire(app);
    }
    codes_view_update(app->codes_view);
}

void totp_scene_codes_on_enter(void* context) {
    TotpApp* app = context;
    codes_view_reset_selection(app->codes_view);
    totp_scene_codes_refresh_view(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewCodes);
}

bool totp_scene_codes_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        totp_scene_codes_refresh_view(app);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == TotpCustomEventCodesRefresh) {
            /* Long-press OK (or empty-list short OK): CALCULATE ALL. */
            app->codes_single = false;
            totp_app_begin_scan(app, NfcSessionOpReadAll);
        } else if(event.event == TotpCustomEventCodesSelect) {
            /* Short OK on a row: CALCULATE that name only → single-code view. */
            app->codes_single = true;
            totp_app_begin_scan(app, NfcSessionOpCalculate);
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        nfc_session_stop(app->nfc_session);
    }
    return consumed;
}

void totp_scene_codes_on_exit(void* context) {
    TotpApp* app = context;
    nfc_session_stop(app->nfc_session);
}
