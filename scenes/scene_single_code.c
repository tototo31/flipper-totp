#include "../app.h"
#include "../hid_type.h"
#include "scenes.h"

#include <stdio.h>
#include <string.h>

static void totp_scene_single_code_hid_status(const char* status, void* context) {
    TotpApp* app = context;
    popup_reset(app->popup);
    popup_set_header(app->popup, "Type via USB", 64, 18, AlignCenter, AlignTop);
    popup_set_text(app->popup, status, 64, 36, AlignCenter, AlignTop);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewPopup);
}

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

            TotpHidTypeResult hid_rc = TotpHidTypeBadCode;
            if(code) {
                /* Copy before USB switch — UI stays usable; digits won't change mid-type. */
                char digits[16];
                snprintf(digits, sizeof(digits), "%s", code);
                totp_scene_single_code_hid_status("Switching USB…", app);
                hid_rc = totp_hid_type_digits(
                    digits, TOTP_HID_ENTER_AFTER, totp_scene_single_code_hid_status, app);
            }

            /*
             * sequence_error = double note_c5 beep — that was the "double beep"
             * with no useful on-screen message. Only success-beep after keys
             * were sent; failures are status text + light vibro (no error seq).
             */
            if(hid_rc == TotpHidTypeOk) {
                totp_scene_single_code_hid_status("Typed", app);
                notification_message(app->notifications, &sequence_success);
                furi_delay_ms(400);
            } else {
                const char* err = "Type failed";
                if(hid_rc == TotpHidTypeBadCode) {
                    err = "No live code";
                } else if(hid_rc == TotpHidTypeUsbBusy) {
                    err = "USB busy";
                } else if(hid_rc == TotpHidTypeTimeout) {
                    err = "HID not ready\nFocus PC field,\nretry Type";
                } else if(hid_rc == TotpHidTypeSendFailed) {
                    err = "HID send failed";
                }
                totp_scene_single_code_hid_status(err, app);
                notification_message(app->notifications, &sequence_single_vibro);
                furi_delay_ms(1200);
            }

            popup_reset(app->popup);
            single_code_view_update(app->single_code_view);
            view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewSingleCode);
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
