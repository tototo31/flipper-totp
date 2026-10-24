#include "app.h"
#include "scenes/scenes.h"

#include <furi_hal_rtc.h>
#include <storage/storage.h>

#include <string.h>

uint64_t totp_app_unix_utc(const TotpApp* app) {
    uint32_t local = furi_hal_rtc_get_timestamp();
    int32_t offset_sec = (int32_t)app->prefs.tz_offset_minutes * 60;
    /* Prefer signed math so UTC+ offsets subtract from local RTC. */
    int64_t utc = (int64_t)local - (int64_t)offset_sec;
    if(utc < 0) utc = 0;
    return (uint64_t)utc;
}

void totp_app_copy_credentials(TotpApp* app) {
    const YkoathCredentialList* list = nfc_session_get_credentials(app->nfc_session);
    memcpy(&app->credentials, list, sizeof(app->credentials));
    uint64_t utc = totp_app_unix_utc(app);
    app->code_expires_utc = ((utc / 30) + 1) * 30;
    app->codes_live = true;
}

void totp_app_begin_scan(TotpApp* app, NfcSessionOp op) {
    app->pending_op = op;
    nfc_session_set_op(app->nfc_session, op);
    if(op == NfcSessionOpCalculate || op == NfcSessionOpDelete) {
        nfc_session_set_name(app->nfc_session, app->name_buffer);
    } else if(op == NfcSessionOpPut) {
        YkoathPutParams put;
        memset(&put, 0, sizeof(put));
        strncpy(put.name, app->name_buffer, sizeof(put.name) - 1);
        put.digits = app->put_digits ? app->put_digits : 6;
        put.type_alg = YKOATH_TYPE_TOTP | YKOATH_ALG_SHA1;
        if(!ykoath_parse_secret(
               app->secret_buffer, put.key, sizeof(put.key), &put.key_len)) {
            snprintf(app->result_message, sizeof(app->result_message), "Bad secret");
            scene_manager_next_scene(app->scene_manager, TotpSceneResult);
            return;
        }
        nfc_session_set_put(app->nfc_session, &put);
    }
    scene_manager_next_scene(app->scene_manager, TotpSceneScan);
}

static bool totp_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    TotpApp* app = context;
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

    app->var_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TotpViewVarList, variable_item_list_get_view(app->var_list));

    app->nfc_session = nfc_session_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    return app;
}

static void totp_app_free(TotpApp* app) {
    furi_assert(app);

    nfc_session_free(app->nfc_session);

    view_dispatcher_remove_view(app->view_dispatcher, TotpViewSubmenu);
    submenu_free(app->submenu);

    view_dispatcher_remove_view(app->view_dispatcher, TotpViewPopup);
    popup_free(app->popup);

    view_dispatcher_remove_view(app->view_dispatcher, TotpViewTextInput);
    text_input_free(app->text_input);

    view_dispatcher_remove_view(app->view_dispatcher, TotpViewNumberInput);
    number_input_free(app->number_input);

    view_dispatcher_remove_view(app->view_dispatcher, TotpViewVarList);
    variable_item_list_free(app->var_list);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

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
