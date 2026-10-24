#include "../app.h"
#include "scenes.h"

typedef enum {
    StartItemRead,
    StartItemList,
    StartItemAdd,
    StartItemTimezone,
    StartItemHelp,
    StartItemClearPassword,
} StartItem;

static void totp_scene_start_submenu_callback(void* context, uint32_t index) {
    TotpApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void totp_scene_start_on_enter(void* context) {
    TotpApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "NFC TOTP");
    submenu_add_item(submenu, "Read TOTP", StartItemRead, totp_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu, "List credentials", StartItemList, totp_scene_start_submenu_callback, app);
    submenu_add_item(submenu, "Add credential", StartItemAdd, totp_scene_start_submenu_callback, app);

    snprintf(app->tz_label, sizeof(app->tz_label), "TZ %+d min", (int)app->prefs.tz_offset_minutes);
    submenu_add_item(
        submenu, app->tz_label, StartItemTimezone, totp_scene_start_submenu_callback, app);
    submenu_add_item(submenu, "Help", StartItemHelp, totp_scene_start_submenu_callback, app);

    if(app->prefs.oath_password[0] != '\0') {
        submenu_add_item(
            submenu,
            "Clear OATH password",
            StartItemClearPassword,
            totp_scene_start_submenu_callback,
            app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewSubmenu);
}

bool totp_scene_start_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == StartItemRead) {
            totp_app_begin_scan(app, NfcSessionOpReadAll);
        } else if(event.event == StartItemList) {
            totp_app_begin_scan(app, NfcSessionOpList);
        } else if(event.event == StartItemAdd) {
            scene_manager_next_scene(app->scene_manager, TotpSceneAddName);
        } else if(event.event == StartItemTimezone) {
            scene_manager_next_scene(app->scene_manager, TotpSceneTz);
        } else if(event.event == StartItemHelp) {
            scene_manager_next_scene(app->scene_manager, TotpSceneHelp);
        } else if(event.event == StartItemClearPassword) {
            app->prefs.oath_password[0] = '\0';
            totp_prefs_save(&app->prefs);
            totp_scene_start_on_enter(app);
        }
    }
    return consumed;
}

void totp_scene_start_on_exit(void* context) {
    TotpApp* app = context;
    submenu_reset(app->submenu);
}
