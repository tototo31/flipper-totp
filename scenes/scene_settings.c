#include "../app.h"
#include "scenes.h"

#include <stdio.h>

typedef enum {
    SettingsItemFromUtc,
    SettingsItemEditTz,
} SettingsItem;

static void totp_scene_settings_callback(void* context, uint32_t index) {
    TotpApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void totp_scene_settings_on_enter(void* context) {
    TotpApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "Settings");

    uint32_t local = totp_app_rtc_local_unix();
    uint64_t utc = totp_app_unix_utc(app);
    snprintf(
        app->settings_local_label,
        sizeof(app->settings_local_label),
        "Local %lu",
        (unsigned long)local);
    snprintf(
        app->settings_utc_label,
        sizeof(app->settings_utc_label),
        "UTC now %lu",
        (unsigned long)utc);
    snprintf(
        app->settings_tz_label,
        sizeof(app->settings_tz_label),
        "TZ L-U %+d",
        (int)app->prefs.tz_offset_minutes);

    /* Read-only status rows (index unused). */
    submenu_add_item(submenu, app->settings_local_label, 100, totp_scene_settings_callback, app);
    submenu_add_item(submenu, app->settings_utc_label, 101, totp_scene_settings_callback, app);
    submenu_add_item(submenu, app->settings_tz_label, 102, totp_scene_settings_callback, app);
    submenu_add_item(
        submenu, "Set TZ from UTC unix", SettingsItemFromUtc, totp_scene_settings_callback, app);
    submenu_add_item(
        submenu, "Edit TZ minutes", SettingsItemEditTz, totp_scene_settings_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewSubmenu);
}

bool totp_scene_settings_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == SettingsItemFromUtc) {
            scene_manager_next_scene(app->scene_manager, TotpSceneUtcCalc);
        } else if(event.event == SettingsItemEditTz) {
            scene_manager_next_scene(app->scene_manager, TotpSceneTz);
        } else {
            /* Status rows — refresh display. */
            totp_scene_settings_on_enter(app);
        }
    }
    return consumed;
}

void totp_scene_settings_on_exit(void* context) {
    TotpApp* app = context;
    submenu_reset(app->submenu);
}
