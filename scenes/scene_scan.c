#include "../app.h"
#include "scenes.h"

#include <string.h>
#include <stdio.h>

static void totp_scene_scan_nfc_callback(NfcSessionEvent event, void* context) {
    TotpApp* app = context;
    uint32_t custom = TotpCustomEventNfcError;
    switch(event) {
    case NfcSessionEventCardDetected:
        custom = TotpCustomEventNfcCardDetected;
        break;
    case NfcSessionEventSuccess:
        custom = TotpCustomEventNfcSuccess;
        break;
    case NfcSessionEventNeedPin:
        custom = TotpCustomEventNfcNeedPin;
        break;
    case NfcSessionEventError:
    default:
        custom = TotpCustomEventNfcError;
        break;
    }
    view_dispatcher_send_custom_event(app->view_dispatcher, custom);
}

static const char* totp_scene_scan_action_label(NfcSessionOp op) {
    switch(op) {
    case NfcSessionOpList:
        return "LIST credentials";
    case NfcSessionOpCalculate:
        return "CALCULATE code";
    case NfcSessionOpPut:
        return "PUT credential";
    case NfcSessionOpDelete:
        return "DELETE credential";
    case NfcSessionOpReadAll:
    default:
        return "CALCULATE ALL";
    }
}

void totp_scene_scan_on_enter(void* context) {
    TotpApp* app = context;
    popup_reset(app->popup);
    popup_set_header(app->popup, "Place card", 64, 22, AlignCenter, AlignCenter);
    popup_set_text(
        app->popup, totp_scene_scan_action_label(app->pending_op), 64, 40, AlignCenter, AlignTop);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewPopup);

    notification_message(app->notifications, &sequence_blink_start_cyan);

    nfc_session_set_callback(app->nfc_session, totp_scene_scan_nfc_callback, app);
    nfc_session_set_op(app->nfc_session, app->pending_op);
    if(app->pending_op == NfcSessionOpCalculate || app->pending_op == NfcSessionOpDelete) {
        nfc_session_set_name(app->nfc_session, app->name_buffer);
    } else if(app->pending_op == NfcSessionOpPut) {
        YkoathPutParams put;
        memset(&put, 0, sizeof(put));
        strncpy(put.name, app->name_buffer, sizeof(put.name) - 1);
        put.digits = app->put_digits ? app->put_digits : 6;
        put.type_alg = YKOATH_TYPE_TOTP | YKOATH_ALG_SHA1;
        if(!ykoath_parse_secret(app->secret_buffer, put.key, sizeof(put.key), &put.key_len)) {
            snprintf(app->result_message, sizeof(app->result_message), "Bad secret");
            scene_manager_next_scene(app->scene_manager, TotpSceneResult);
            return;
        }
        nfc_session_set_put(app->nfc_session, &put);
    }

    nfc_session_start(
        app->nfc_session,
        app->prefs.oath_password[0] ? app->prefs.oath_password : NULL,
        totp_app_unix_utc(app));
}

bool totp_scene_scan_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == TotpCustomEventNfcCardDetected) {
            notification_message(app->notifications, &sequence_blink_stop);
            notification_message(app->notifications, &sequence_set_only_blue_255);
            popup_set_header(app->popup, "Hold steady…", 64, 24, AlignCenter, AlignCenter);
            popup_set_text(
                app->popup, "Reading (auto-retry)", 64, 42, AlignCenter, AlignTop);
        } else if(event.event == TotpCustomEventNfcSuccess) {
            notification_message(app->notifications, &sequence_blink_stop);
            notification_message(app->notifications, &sequence_success);
            nfc_session_stop(app->nfc_session);
            NfcSessionOp op = app->pending_op;
            if(op == NfcSessionOpReadAll || op == NfcSessionOpCalculate) {
                totp_app_copy_credentials(app);
                if(!scene_manager_search_and_switch_to_previous_scene(
                       app->scene_manager, TotpSceneCodes)) {
                    scene_manager_next_scene(app->scene_manager, TotpSceneCodes);
                }
            } else if(op == NfcSessionOpList) {
                totp_app_copy_credentials(app);
                scene_manager_next_scene(app->scene_manager, TotpSceneList);
            } else if(op == NfcSessionOpPut) {
                snprintf(app->result_message, sizeof(app->result_message), "Credential saved");
                scene_manager_next_scene(app->scene_manager, TotpSceneResult);
            } else if(op == NfcSessionOpDelete) {
                snprintf(app->result_message, sizeof(app->result_message), "Credential deleted");
                scene_manager_next_scene(app->scene_manager, TotpSceneResult);
            }
        } else if(event.event == TotpCustomEventNfcNeedPin) {
            notification_message(app->notifications, &sequence_blink_stop);
            nfc_session_stop(app->nfc_session);
            scene_manager_next_scene(app->scene_manager, TotpScenePin);
        } else if(event.event == TotpCustomEventNfcError) {
            notification_message(app->notifications, &sequence_blink_stop);
            notification_message(app->notifications, &sequence_error);
            snprintf(
                app->result_message,
                sizeof(app->result_message),
                "%s",
                nfc_session_result_str(nfc_session_get_result(app->nfc_session)));
            nfc_session_stop(app->nfc_session);
            scene_manager_next_scene(app->scene_manager, TotpSceneResult);
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        nfc_session_stop(app->nfc_session);
        notification_message(app->notifications, &sequence_blink_stop);
    }
    return consumed;
}

void totp_scene_scan_on_exit(void* context) {
    TotpApp* app = context;
    nfc_session_stop(app->nfc_session);
    notification_message(app->notifications, &sequence_blink_stop);
    popup_reset(app->popup);
}
