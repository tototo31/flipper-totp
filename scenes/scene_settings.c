#include "../app.h"
#include "scenes.h"

#include <string.h>

/** Apply Bluetooth master switch: quiet radio when Off; optional HID when On. */
static void totp_scene_settings_apply_bt_radio(TotpApp* app) {
    if(!app->prefs.bt_enabled) {
        app->prefs.type_via_bt = false;
        totp_hid_bt_radio_quiet(app->hid_bt);
    } else if(app->prefs.type_via_bt) {
        totp_hid_bt_enable(app->hid_bt);
    } else {
        /* On but Type over USB — no HID profile; keep Flipper default advertising. */
        totp_hid_bt_disable(app->hid_bt, true);
    }
}

void totp_scene_settings_on_enter(void* context) {
    TotpApp* app = context;
    if(app->settings_focus_pending) {
        settings_view_set_selection(app->settings_view, app->settings_focus_item);
        app->settings_focus_pending = false;
    } else {
        settings_view_reset_selection(app->settings_view);
    }
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
        } else if(event.event == SettingsViewItemAutoUnlock) {
            if(app->prefs.auto_unlock) {
                app->prefs.auto_unlock = false;
                if(app->prefs.oath_password[0] != '\0') {
                    memset(app->prefs.oath_password, 0, sizeof(app->prefs.oath_password));
                }
                totp_prefs_save(&app->prefs);
                settings_view_update(app->settings_view);
            } else {
                scene_manager_next_scene(app->scene_manager, TotpSceneConfirmAutoUnlock);
            }
        } else if(event.event == SettingsViewItemBtRadio) {
            app->prefs.bt_enabled = !app->prefs.bt_enabled;
            totp_scene_settings_apply_bt_radio(app);
            totp_prefs_save(&app->prefs);
            settings_view_update(app->settings_view);
            notification_message(
                app->notifications,
                app->prefs.bt_enabled ? &sequence_success : &sequence_single_vibro);
        } else if(event.event == SettingsViewItemTypeVia) {
            if(!app->prefs.bt_enabled) {
                notification_message(app->notifications, &sequence_single_vibro);
            } else {
                app->prefs.type_via_bt = !app->prefs.type_via_bt;
                totp_prefs_save(&app->prefs);
                if(app->prefs.type_via_bt) {
                    totp_hid_bt_enable(app->hid_bt);
                } else {
                    totp_hid_bt_disable(app->hid_bt, true);
                }
                settings_view_update(app->settings_view);
            }
        } else if(event.event == SettingsViewItemBtPair) {
            if(!app->prefs.bt_enabled) {
                notification_message(app->notifications, &sequence_single_vibro);
            } else {
                totp_hid_bt_enable(app->hid_bt);
                scene_manager_next_scene(app->scene_manager, TotpSceneBtPair);
            }
        } else if(event.event == SettingsViewItemBtForget) {
            if(!app->prefs.bt_enabled) {
                notification_message(app->notifications, &sequence_single_vibro);
            } else {
                scene_manager_next_scene(app->scene_manager, TotpSceneConfirmForgetBt);
            }
        } else if(event.event == SettingsViewItemSetPassword) {
            scene_manager_next_scene(app->scene_manager, TotpSceneSetPassword);
        } else if(event.event == SettingsViewItemClearPassword) {
            totp_app_begin_scan(app, NfcSessionOpClearCode);
        } else if(event.event == SettingsViewItemReset) {
            scene_manager_next_scene(app->scene_manager, TotpSceneConfirmReset);
        } else {
            settings_view_update(app->settings_view);
        }
    }
    return consumed;
}

void totp_scene_settings_on_exit(void* context) {
    UNUSED(context);
}
