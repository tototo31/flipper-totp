#include "../app.h"
#include "scenes.h"

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
    if(app->credentials.count == 0 || index >= app->credentials.count) {
        return;
    }
    /* CALCULATE ALL already omits HOTP / touch-required entries. */
    app->selected_cred = index;
    strncpy(
        app->name_buffer,
        app->credentials.items[index].name,
        sizeof(app->name_buffer) - 1);
    app->name_buffer[sizeof(app->name_buffer) - 1] = '\0';
    view_dispatcher_send_custom_event(app->view_dispatcher, TotpCustomEventCodesSelect);
}

static void totp_scene_codes_rebuild(TotpApp* app) {
    VariableItemList* list = app->var_list;
    variable_item_list_reset(list);

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
                "Codes (%lus)",
                (unsigned long)remain);
        }
    }
    if(!app->codes_live) {
        snprintf(app->codes_header, sizeof(app->codes_header), "Expired — OK to reread");
    }

    variable_item_list_set_header(list, app->codes_header);
    variable_item_list_set_enter_callback(list, totp_scene_codes_enter_callback, app);

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
