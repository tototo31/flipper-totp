#include "../app.h"
#include "../hid_type.h"
#include "scenes.h"

#include <string.h>

static void totp_scene_single_code_mask(TotpApp* app) {
    if(app->credentials.count == 0) return;
    YkoathCredential* cred = &app->credentials.items[0];
    size_t n = cred->digits ? cred->digits : strlen(cred->code);
    if(n < 4) n = 6;
    if(n >= sizeof(cred->code)) n = sizeof(cred->code) - 1;
    for(size_t i = 0; i < n; i++) {
        cred->code[i] = '*';
    }
    cred->code[n] = '\0';
}

static void totp_scene_single_code_expire(TotpApp* app) {
    if(!app->codes_live) return;
    app->codes_live = false;
    totp_scene_single_code_mask(app);
    nfc_session_stop(app->nfc_session);
}

static void totp_scene_single_code_refresh(TotpApp* app) {
    uint64_t utc = totp_app_unix_utc(app);
    if(app->codes_live && utc >= app->code_expires_utc) {
        totp_scene_single_code_expire(app);
    }
    single_code_view_update(app->single_code_view);
}

void totp_scene_single_code_on_enter(void* context) {
    TotpApp* app = context;
    app->codes_single = true;
    totp_scene_single_code_refresh(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewSingleCode);
}

bool totp_scene_single_code_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        totp_scene_single_code_refresh(app);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == TotpCustomEventCodesRefresh) {
            if(app->name_buffer[0] == '\0' && app->credentials.count > 0) {
                strncpy(
                    app->name_buffer,
                    app->credentials.items[0].name,
                    sizeof(app->name_buffer) - 1);
                app->name_buffer[sizeof(app->name_buffer) - 1] = '\0';
            }
            totp_app_begin_scan(app, NfcSessionOpCalculate);
        } else if(event.event == TotpCustomEventHidType) {
            /* Only type a live digit string — never expired/starred codes. */
            totp_scene_single_code_refresh(app);
            const char* code = NULL;
            if(app->codes_live && app->credentials.count > 0) {
                uint64_t now = totp_app_unix_utc(app);
                if(app->code_expires_utc > now) {
                    code = app->credentials.items[0].code;
                }
            }
            bool typed = false;
            if(code) {
                typed = totp_hid_type_digits(code, TOTP_HID_ENTER_AFTER);
            }
            if(typed) {
                notification_message(app->notifications, &sequence_success);
            } else {
                notification_message(app->notifications, &sequence_error);
            }
            single_code_view_update(app->single_code_view);
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        nfc_session_stop(app->nfc_session);
    }
    return consumed;
}

void totp_scene_single_code_on_exit(void* context) {
    TotpApp* app = context;
    nfc_session_stop(app->nfc_session);
}
