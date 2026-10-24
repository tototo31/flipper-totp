#include "../app.h"
#include "scenes.h"

typedef enum {
    ConfirmResetItemGo,
} ConfirmResetItem;

static void totp_scene_confirm_reset_callback(void* context, uint32_t index) {
    TotpApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void totp_scene_confirm_reset_on_enter(void* context) {
    TotpApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "Wipe OATH?");
    submenu_add_item(
        submenu,
        "Confirm RESET",
        ConfirmResetItemGo,
        totp_scene_confirm_reset_callback,
        app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewSubmenu);
}

bool totp_scene_confirm_reset_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == ConfirmResetItemGo) {
            totp_app_begin_scan(app, NfcSessionOpReset);
        }
    }
    return consumed;
}

void totp_scene_confirm_reset_on_exit(void* context) {
    TotpApp* app = context;
    submenu_reset(app->submenu);
}
