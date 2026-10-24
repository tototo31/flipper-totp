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
    /* Calculate/Delete name and Put key are set in totp_app_begin_scan (or kept
     * across OATH PIN → re-scan). Do not re-parse secret here. */
    if(app->pending_op == NfcSessionOpCalculate || app->pending_op == NfcSessionOpDelete) {
        nfc_session_set_name(app->nfc_session, app->name_buffer);
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
                totp_app_finish_put(app);
                snprintf(app->result_message, sizeof(app->result_message), "Credential saved");
                scene_manager_next_scene(app->scene_manager, TotpSceneResult);
            } else if(op == NfcSessionOpDelete) {
                snprintf(app->result_message, sizeof(app->result_message), "Credential deleted");
                scene_manager_next_scene(app->scene_manager, TotpSceneResult);
            }
        } else if(event.event == TotpCustomEventNfcNeedPin) {
            notification_message(app->notifications, &sequence_blink_stop);
            nfc_session_stop(app->nfc_session);
            /* Keep PUT key in session for retry after OATH password. */
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
            if(app->pending_op == NfcSessionOpPut) {
                totp_app_finish_put(app);
            }
            scene_manager_next_scene(app->scene_manager, TotpSceneResult);
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        nfc_session_stop(app->nfc_session);
        notification_message(app->notifications, &sequence_blink_stop);
        if(app->pending_op == NfcSessionOpPut) {
            totp_app_finish_put(app);
        }
    }
    return consumed;
}

void totp_scene_scan_on_exit(void* context) {
    TotpApp* app = context;
    nfc_session_stop(app->nfc_session);
    notification_message(app->notifications, &sequence_blink_stop);
    popup_reset(app->popup);
}
