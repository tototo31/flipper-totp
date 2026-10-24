#include "../app.h"
#include "scenes.h"

#include <furi_hal_rtc.h>
#include <stdio.h>
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

static void totp_scene_codes_enter_callback(void* context, uint32_t index) {
    TotpApp* app = context;
    /* Expired: OK rereads CALCULATE ALL (unchanged). Live: OK opens cred menu. */
    if(!app->codes_live) {
        view_dispatcher_send_custom_event(app->view_dispatcher, TotpCustomEventCodesRefresh);
        return;
    }
    if(index < app->codes_meta_count) {
        return; /* time-debug rows are not selectable */
    }
    size_t cred_i = (size_t)index - (size_t)app->codes_meta_count;
    if(app->credentials.count == 0 || cred_i >= app->credentials.count) {
        return;
    }
    /* CALCULATE ALL already omits HOTP / touch-required entries. */
    app->selected_cred = cred_i;
    strncpy(
        app->name_buffer,
        app->credentials.items[cred_i].name,
        sizeof(app->name_buffer) - 1);
    app->name_buffer[sizeof(app->name_buffer) - 1] = '\0';
    view_dispatcher_send_custom_event(app->view_dispatcher, TotpCustomEventCodesSelect);
}

static void totp_scene_codes_add_meta(TotpApp* app, VariableItemList* list) {
    uint64_t chal = app->code_challenge_utc;
    uint64_t period = chal / 30;
    int16_t tz = app->prefs.tz_offset_minutes;
    uint32_t rtc_local = furi_hal_rtc_get_timestamp();

    snprintf(app->codes_unix_val, sizeof(app->codes_unix_val), "%lu", (unsigned long)chal);
    snprintf(app->codes_period_val, sizeof(app->codes_period_val), "%lu", (unsigned long)period);
    snprintf(app->codes_tz_val, sizeof(app->codes_tz_val), "%+d", (int)tz);
    snprintf(app->codes_rtc_val, sizeof(app->codes_rtc_val), "%lu", (unsigned long)rtc_local);

    VariableItem* item;
    item = variable_item_list_add(list, "chal UTC", 1, NULL, NULL);
    variable_item_set_current_value_text(item, app->codes_unix_val);
    item = variable_item_list_add(list, "period", 1, NULL, NULL);
    variable_item_set_current_value_text(item, app->codes_period_val);
    item = variable_item_list_add(list, "tz min", 1, NULL, NULL);
    variable_item_set_current_value_text(item, app->codes_tz_val);
    item = variable_item_list_add(list, "RTC local", 1, NULL, NULL);
    variable_item_set_current_value_text(item, app->codes_rtc_val);
    app->codes_meta_count = 4;
}

static void totp_scene_codes_rebuild(TotpApp* app) {
    VariableItemList* list = app->var_list;
    variable_item_list_reset(list);
    app->codes_meta_count = 0;

    uint64_t utc = totp_app_unix_utc(app);
    if(app->codes_live) {
        uint64_t remain = 0;
        if(app->code_expires_utc > utc) {
            remain = app->code_expires_utc - utc;
        } else {
            totp_scene_codes_expire(app);
        }
        if(app->codes_live) {
            snprintf(
                app->codes_header,
                sizeof(app->codes_header),
                "Codes %lus p=%lu",
                (unsigned long)remain,
                (unsigned long)(app->code_challenge_utc / 30));
        }
    }
    if(!app->codes_live) {
        snprintf(app->codes_header, sizeof(app->codes_header), "Expired — OK to reread");
    }

    variable_item_list_set_header(list, app->codes_header);
    variable_item_list_set_enter_callback(list, totp_scene_codes_enter_callback, app);

    /* Always show how challenge time was derived (even when expired). */
    if(app->code_challenge_utc != 0 || app->credentials.count > 0) {
        totp_scene_codes_add_meta(app, list);
    }

    if(app->credentials.count == 0) {
        VariableItem* item = variable_item_list_add(list, "No TOTP credentials", 1, NULL, NULL);
        variable_item_set_current_value_text(item, "");
        return;
    }

    for(size_t i = 0; i < app->credentials.count; i++) {
        YkoathCredential* cred = &app->credentials.items[i];
        VariableItem* item = variable_item_list_add(list, cred->name, 1, NULL, NULL);
        /* cred->code is app-owned storage (stars after expiry). */
        variable_item_set_current_value_text(item, cred->code);
    }
}

void totp_scene_codes_on_enter(void* context) {
    TotpApp* app = context;
    uint64_t utc = totp_app_unix_utc(app);
    if(app->codes_live && utc >= app->code_expires_utc) {
        totp_scene_codes_expire(app);
    }
    totp_scene_codes_rebuild(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewVarList);
}

bool totp_scene_codes_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        uint64_t utc = totp_app_unix_utc(app);
        if(app->codes_live && utc >= app->code_expires_utc) {
            totp_scene_codes_expire(app);
        }
        totp_scene_codes_rebuild(app);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == TotpCustomEventCodesRefresh) {
            totp_app_begin_scan(app, NfcSessionOpReadAll);
        } else if(event.event == TotpCustomEventCodesSelect) {
            scene_manager_next_scene(app->scene_manager, TotpSceneCredMenu);
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        nfc_session_stop(app->nfc_session);
    }
    return consumed;
}

void totp_scene_codes_on_exit(void* context) {
    TotpApp* app = context;
    nfc_session_stop(app->nfc_session);
    variable_item_list_reset(app->var_list);
}
