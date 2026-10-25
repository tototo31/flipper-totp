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
    case NfcSessionOpSetCode:
        return "SET CODE password";
    case NfcSessionOpClearCode:
        return "CLEAR CODE password";
    case NfcSessionOpReset:
        return "RESET applet";
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
        app->nfc_session, totp_app_oath_password(app), totp_app_unix_utc(app));
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
            if(op == NfcSessionOpCalculate) {
                /* Merge into existing Read All list when present; do not truncate. */
                totp_app_apply_calculate_result(app);
                app->codes_single = true;
                if(app->credentials.count > 0) {
                    size_t idx = totp_app_cred_index(app);
                    strncpy(
                        app->name_buffer,
                        app->credentials.items[idx].name,
                        sizeof(app->name_buffer) - 1);
                    app->name_buffer[sizeof(app->name_buffer) - 1] = '\0';
                }
                /*
                 * Scene stack: prefer Codes → SingleCode so Back restores the
                 * full list. Avoid leaving Scan under SingleCode after a pick.
                 */
                if(!scene_manager_search_and_switch_to_previous_scene(
                       app->scene_manager, TotpSceneSingleCode)) {
                    if(app->credentials.count > 1 &&
                       scene_manager_search_and_switch_to_previous_scene(
                           app->scene_manager, TotpSceneCodes)) {
                        scene_manager_next_scene(app->scene_manager, TotpSceneSingleCode);
                    } else {
                        scene_manager_next_scene(app->scene_manager, TotpSceneSingleCode);
                    }
                }
            } else if(op == NfcSessionOpReadAll) {
                app->codes_single = false;
                app->selected_cred = 0;
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
            } else if(op == NfcSessionOpSetCode) {
                totp_app_store_oath_password(app, app->set_code_password);
                memset(app->set_code_password, 0, sizeof(app->set_code_password));
                nfc_session_clear_new_password(app->nfc_session);
                snprintf(
                    app->result_message,
                    sizeof(app->result_message),
                    app->prefs.auto_unlock ? "Password set+saved" : "Password set (RAM)");
                scene_manager_next_scene(app->scene_manager, TotpSceneResult);
            } else if(op == NfcSessionOpClearCode) {
                totp_app_clear_oath_password(app);
                snprintf(app->result_message, sizeof(app->result_message), "OATH password cleared");
                scene_manager_next_scene(app->scene_manager, TotpSceneResult);
            } else if(op == NfcSessionOpReset) {
                totp_app_clear_oath_password(app);
                snprintf(app->result_message, sizeof(app->result_message), "OATH applet reset");
                scene_manager_next_scene(app->scene_manager, TotpSceneResult);
            }
        } else if(event.event == TotpCustomEventNfcNeedPin) {
            notification_message(app->notifications, &sequence_blink_stop);
            nfc_session_stop(app->nfc_session);
            /* Keep PUT key / SET CODE new password for retry after OATH password. */
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
            } else if(app->pending_op == NfcSessionOpSetCode) {
                memset(app->set_code_password, 0, sizeof(app->set_code_password));
                nfc_session_clear_new_password(app->nfc_session);
            }
            scene_manager_next_scene(app->scene_manager, TotpSceneResult);
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        nfc_session_stop(app->nfc_session);
        notification_message(app->notifications, &sequence_blink_stop);
        if(app->pending_op == NfcSessionOpPut) {
            totp_app_finish_put(app);
        } else if(app->pending_op == NfcSessionOpSetCode) {
            memset(app->set_code_password, 0, sizeof(app->set_code_password));
            nfc_session_clear_new_password(app->nfc_session);
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
