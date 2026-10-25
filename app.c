#include "app.h"
#include "scenes/scenes.h"

#include <furi_hal_rtc.h>
#include <storage/storage.h>

#include <stdio.h>
#include <string.h>

uint32_t totp_app_rtc_local_unix(void) {
    /* Flipper RTC datetime is wall-clock local; get_timestamp() is a naive
     * epoch of that datetime (not true UTC). */
    return furi_hal_rtc_get_timestamp();
}

uint64_t totp_app_unix_utc(const TotpApp* app) {
    /* YKOATH challenge needs true UTC unix.
     * prefs.tz_offset_minutes = (local − UTC) in minutes
     *   UTC+2 → +120;  US Central CDT (UTC−5) → −300;  CST → −360
     * utc = local_naive_epoch − tz_offset×60 */
    int64_t local = (int64_t)totp_app_rtc_local_unix();
    int64_t utc = local - (int64_t)app->prefs.tz_offset_minutes * 60;
    if(utc < 0) utc = 0;
    return (uint64_t)utc;
}

void totp_app_copy_credentials(TotpApp* app) {
    const YkoathCredentialList* list = nfc_session_get_credentials(app->nfc_session);
    memcpy(&app->credentials, list, sizeof(app->credentials));
    /* Prefer the unix UTC that was sent in the CALCULATE challenge. */
    uint64_t utc = nfc_session_get_unix_utc(app->nfc_session);
    if(utc == 0) {
        utc = totp_app_unix_utc(app);
    }
    app->code_challenge_utc = utc;
    app->code_expires_utc = ((utc / 30) + 1) * 30;
    app->codes_live = true;
}

void totp_app_clear_secret(TotpApp* app) {
    furi_assert(app);
    memset(app->secret_buffer, 0, sizeof(app->secret_buffer));
}

const char* totp_app_oath_password(const TotpApp* app) {
    furi_assert(app);
    if(app->prefs.auto_unlock && app->prefs.oath_password[0] != '\0') {
        return app->prefs.oath_password;
    }
    if(app->session_password[0] != '\0') {
        return app->session_password;
    }
    return NULL;
}

void totp_app_store_oath_password(TotpApp* app, const char* password) {
    furi_assert(app);
    memset(app->session_password, 0, sizeof(app->session_password));
    if(password && password[0] != '\0') {
        strncpy(app->session_password, password, sizeof(app->session_password) - 1);
    }
    if(app->prefs.auto_unlock) {
        memset(app->prefs.oath_password, 0, sizeof(app->prefs.oath_password));
        if(password && password[0] != '\0') {
            strncpy(
                app->prefs.oath_password, password, sizeof(app->prefs.oath_password) - 1);
        }
        totp_prefs_save(&app->prefs);
    } else if(app->prefs.oath_password[0] != '\0') {
        memset(app->prefs.oath_password, 0, sizeof(app->prefs.oath_password));
        totp_prefs_save(&app->prefs);
    }
}

void totp_app_clear_oath_password(TotpApp* app) {
    furi_assert(app);
    memset(app->session_password, 0, sizeof(app->session_password));
    if(app->prefs.oath_password[0] != '\0') {
        memset(app->prefs.oath_password, 0, sizeof(app->prefs.oath_password));
        totp_prefs_save(&app->prefs);
    }
}

void totp_app_finish_put(TotpApp* app) {
    furi_assert(app);
    totp_app_clear_secret(app);
    nfc_session_clear_put(app->nfc_session);
    furi_check(furi_mutex_acquire(app->cli_mutex, FuriWaitForever) == FuriStatusOk);
    app->cli_put_busy = false;
    furi_mutex_release(app->cli_mutex);
}

size_t totp_app_cred_index(const TotpApp* app) {
    furi_assert(app);
    if(app->credentials.count == 0) return 0;
    if(app->selected_cred >= app->credentials.count) return 0;
    return app->selected_cred;
}

const char* totp_app_live_code(const TotpApp* app) {
    furi_assert(app);
    if(!app->codes_live || app->credentials.count == 0) return NULL;
    uint64_t now = totp_app_unix_utc(app);
    if(app->code_expires_utc <= now) return NULL;
    const char* code = app->credentials.items[totp_app_cred_index(app)].code;
    if(!code || !code[0]) return NULL;
    for(const char* p = code; *p; p++) {
        if(*p < '0' || *p > '9') return NULL;
    }
    return code;
}

static void totp_app_hid_status(const char* status, void* context) {
    TotpApp* app = context;
    popup_reset(app->popup);
    popup_set_header(app->popup, "Type via USB", 64, 18, AlignCenter, AlignTop);
    popup_set_text(app->popup, status, 64, 36, AlignCenter, AlignTop);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewPopup);
}

TotpHidTypeResult totp_app_type_live_hid(TotpApp* app) {
    furi_assert(app);
    const char* code = totp_app_live_code(app);
    TotpHidTypeResult hid_rc = TotpHidTypeBadCode;
    if(code) {
        char digits[16];
        snprintf(digits, sizeof(digits), "%s", code);
        totp_app_hid_status("Switching USB…", app);
        hid_rc = totp_hid_type_digits(
            digits, TOTP_HID_ENTER_AFTER, totp_app_hid_status, app);
    }

    /* sequence_error = double beep — never use it here. */
    if(hid_rc == TotpHidTypeOk) {
        totp_app_hid_status("Typed", app);
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
        totp_app_hid_status(err, app);
        notification_message(app->notifications, &sequence_single_vibro);
        furi_delay_ms(1200);
    }
    popup_reset(app->popup);
    return hid_rc;
}

void totp_app_begin_scan(TotpApp* app, NfcSessionOp op) {
    app->pending_op = op;
    app->result_message[0] = '\0';
    nfc_session_set_op(app->nfc_session, op);

    switch(op) {
    case NfcSessionOpCalculate:
    case NfcSessionOpDelete:
        nfc_session_set_name(app->nfc_session, app->name_buffer);
        break;
    case NfcSessionOpPut: {
        YkoathPutParams put;
        memset(&put, 0, sizeof(put));
        strncpy(put.name, app->name_buffer, sizeof(put.name) - 1);
        put.digits = app->put_digits ? app->put_digits : 6;
        put.type_alg = YKOATH_TYPE_TOTP | YKOATH_ALG_SHA1;
        if(!ykoath_parse_secret(
               app->secret_buffer, put.key, sizeof(put.key), &put.key_len)) {
            totp_app_finish_put(app);
            snprintf(app->result_message, sizeof(app->result_message), "Cannot parse secret");
            scene_manager_next_scene(app->scene_manager, TotpSceneResult);
            return;
        }
        nfc_session_set_put(app->nfc_session, &put);
        memset(&put, 0, sizeof(put));
        totp_app_clear_secret(app);
        break;
    }
    case NfcSessionOpSetCode:
        strncpy(
            app->set_code_password,
            app->pin_buffer,
            sizeof(app->set_code_password) - 1);
        app->set_code_password[sizeof(app->set_code_password) - 1] = '\0';
        memset(app->pin_buffer, 0, sizeof(app->pin_buffer));
        nfc_session_set_new_password(app->nfc_session, app->set_code_password);
        break;
    case NfcSessionOpClearCode:
    case NfcSessionOpReset:
        nfc_session_clear_new_password(app->nfc_session);
        memset(app->set_code_password, 0, sizeof(app->set_code_password));
        break;
    case NfcSessionOpReadAll:
    case NfcSessionOpList:
    default:
        break;
    }
    scene_manager_next_scene(app->scene_manager, TotpSceneScan);
}

static void totp_app_on_cli_put(TotpApp* app) {
    /* Serialize onto app/GUI thread: same Add→scan PUT path. */
    if(nfc_session_is_running(app->nfc_session)) {
        totp_app_finish_put(app);
        snprintf(app->result_message, sizeof(app->result_message), "NFC busy");
        scene_manager_next_scene(app->scene_manager, TotpSceneResult);
        return;
    }
    totp_app_begin_scan(app, NfcSessionOpPut);
}

static bool totp_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    TotpApp* app = context;
    /* Only the USB CLI arms Put this way — never settings row indices. */
    if(event == TotpCustomEventCliPut) {
        totp_app_on_cli_put(app);
        return true;
    }
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool totp_app_back_event_callback(void* context) {
    furi_assert(context);
    TotpApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void totp_app_tick_event_callback(void* context) {
    furi_assert(context);
    TotpApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

static TotpApp* totp_app_alloc(void) {
    TotpApp* app = malloc(sizeof(TotpApp));
    memset(app, 0, sizeof(TotpApp));

    totp_prefs_load(&app->prefs);
    app->pending_op = NfcSessionOpReadAll;
    app->put_digits = 6;
    app->cli_mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&totp_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, totp_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, totp_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, totp_app_tick_event_callback, 500);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TotpViewSubmenu, submenu_get_view(app->submenu));

    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, TotpViewPopup, popup_get_view(app->popup));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TotpViewTextInput, text_input_get_view(app->text_input));

    app->number_input = number_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TotpViewNumberInput, number_input_get_view(app->number_input));

    app->codes_view = codes_view_alloc();
    codes_view_set_app(app->codes_view, app);
    view_dispatcher_add_view(
        app->view_dispatcher, TotpViewCodes, codes_view_get_view(app->codes_view));

    app->single_code_view = single_code_view_alloc();
    single_code_view_set_app(app->single_code_view, app);
    view_dispatcher_add_view(
        app->view_dispatcher,
        TotpViewSingleCode,
        single_code_view_get_view(app->single_code_view));

    app->settings_view = settings_view_alloc();
    settings_view_set_app(app->settings_view, app);
    view_dispatcher_add_view(
        app->view_dispatcher, TotpViewSettings, settings_view_get_view(app->settings_view));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TotpViewWidget, widget_get_view(app->widget));

    app->nfc_session = nfc_session_alloc();

    totp_cli_register(app);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    return app;
}

static void totp_app_free(TotpApp* app) {
    furi_assert(app);

    totp_cli_unregister(app);
    totp_app_finish_put(app);

    nfc_session_free(app->nfc_session);

    view_dispatcher_remove_view(app->view_dispatcher, TotpViewSubmenu);
    submenu_free(app->submenu);

    view_dispatcher_remove_view(app->view_dispatcher, TotpViewPopup);
    popup_free(app->popup);

    view_dispatcher_remove_view(app->view_dispatcher, TotpViewTextInput);
    text_input_free(app->text_input);

    view_dispatcher_remove_view(app->view_dispatcher, TotpViewNumberInput);
    number_input_free(app->number_input);

    view_dispatcher_remove_view(app->view_dispatcher, TotpViewCodes);
    codes_view_free(app->codes_view);

    view_dispatcher_remove_view(app->view_dispatcher, TotpViewSingleCode);
    single_code_view_free(app->single_code_view);

    view_dispatcher_remove_view(app->view_dispatcher, TotpViewSettings);
    settings_view_free(app->settings_view);

    view_dispatcher_remove_view(app->view_dispatcher, TotpViewWidget);
    widget_free(app->widget);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_mutex_free(app->cli_mutex);

    memset(app->session_password, 0, sizeof(app->session_password));
    memset(app->set_code_password, 0, sizeof(app->set_code_password));
    memset(app->pin_buffer, 0, sizeof(app->pin_buffer));

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t totp_nfc_app(void* p) {
    UNUSED(p);
    TotpApp* app = totp_app_alloc();
    scene_manager_next_scene(app->scene_manager, TotpSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    totp_app_free(app);
    return 0;
}
