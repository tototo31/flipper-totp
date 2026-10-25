#include "../app.h"
#include "scenes.h"

typedef enum {
    CredItemTypeUsb,
    CredItemCalculate,
    CredItemDelete,
} CredItem;

static void totp_scene_cred_menu_callback(void* context, uint32_t index) {
    TotpApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void totp_scene_cred_menu_on_enter(void* context) {
    TotpApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, app->name_buffer);
    /* Type when this account already has a live digit code (e.g. after Read All). */
    if(totp_app_live_code(app)) {
        submenu_add_item(
            submenu, "Type via USB", CredItemTypeUsb, totp_scene_cred_menu_callback, app);
    }
    submenu_add_item(
        submenu, "Calculate code", CredItemCalculate, totp_scene_cred_menu_callback, app);
    submenu_add_item(submenu, "Delete", CredItemDelete, totp_scene_cred_menu_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewSubmenu);
}

bool totp_scene_cred_menu_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == CredItemTypeUsb) {
            totp_app_type_live_hid(app);
            /* Stay on submenu; rebuild so Type remains if still live. */
            totp_scene_cred_menu_on_enter(app);
        } else if(event.event == CredItemCalculate) {
            totp_app_begin_scan(app, NfcSessionOpCalculate);
        } else if(event.event == CredItemDelete) {
            scene_manager_next_scene(app->scene_manager, TotpSceneConfirmDelete);
        }
    }
    return consumed;
}

void totp_scene_cred_menu_on_exit(void* context) {
    TotpApp* app = context;
    submenu_reset(app->submenu);
}
