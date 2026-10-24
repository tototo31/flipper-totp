#include "../app.h"
#include "scenes.h"

void totp_scene_settings_on_enter(void* context) {
    TotpApp* app = context;
    settings_view_reset_selection(app->settings_view);
    settings_view_update(app->settings_view);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewSettings);
}

bool totp_scene_settings_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        settings_view_update(app->settings_view);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == SettingsViewItemFromUtc) {
            scene_manager_next_scene(app->scene_manager, TotpSceneUtcCalc);
        } else if(event.event == SettingsViewItemEditTz) {
            scene_manager_next_scene(app->scene_manager, TotpSceneTz);
        } else if(event.event == SettingsViewItemSetPassword) {
            scene_manager_next_scene(app->scene_manager, TotpSceneSetPassword);
        } else if(event.event == SettingsViewItemClearPassword) {
            totp_app_begin_scan(app, NfcSessionOpClearCode);
        } else if(event.event == SettingsViewItemReset) {
            scene_manager_next_scene(app->scene_manager, TotpSceneConfirmReset);
        } else {
            /* Status rows refresh values. */
            settings_view_update(app->settings_view);
        }
    }
    return consumed;
}

void totp_scene_settings_on_exit(void* context) {
    UNUSED(context);
}
