#include "../app.h"
#include "scenes.h"

#include <stdio.h>
#include <string.h>

static void totp_scene_list_enter_callback(void* context, uint32_t index) {
    TotpApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void totp_scene_list_on_enter(void* context) {
    TotpApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "Credentials");

    if(app->credentials.count == 0) {
        submenu_add_item(submenu, "(empty)", 0, totp_scene_list_enter_callback, app);
    } else {
        for(size_t i = 0; i < app->credentials.count; i++) {
            YkoathCredential* cred = &app->credentials.items[i];
            char label[YKOATH_NAME_MAX + 8];
            const char* kind = ((cred->algo & 0xf0) == YKOATH_TYPE_HOTP) ? "HOTP" : "TOTP";
            snprintf(label, sizeof(label), "%s [%s]", cred->name, kind);
            submenu_add_item(submenu, label, (uint32_t)i, totp_scene_list_enter_callback, app);
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewSubmenu);
}

bool totp_scene_list_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(app->credentials.count == 0) {
            return consumed;
        }
        app->selected_cred = event.event;
        if(app->selected_cred < app->credentials.count) {
            strncpy(
                app->name_buffer,
                app->credentials.items[app->selected_cred].name,
                sizeof(app->name_buffer) - 1);
            scene_manager_next_scene(app->scene_manager, TotpSceneCredMenu);
        }
    }
    return consumed;
}

void totp_scene_list_on_exit(void* context) {
    TotpApp* app = context;
    submenu_reset(app->submenu);
}
