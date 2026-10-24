#include "../app.h"
#include "scenes.h"

#include <stdio.h>

typedef enum {
    SettingsItemLocal,
    SettingsItemUtc,
    SettingsItemOffset,
    SettingsItemFromUtc,
    SettingsItemEditTz,
} SettingsItem;

static void totp_scene_settings_enter(void* context, uint32_t index) {
    TotpApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void totp_scene_settings_on_enter(void* context) {
    TotpApp* app = context;
    VariableItemList* vil = app->variable_item_list;
    variable_item_list_reset(vil);
    variable_item_list_set_header(vil, "Settings");

    uint32_t local = totp_app_rtc_local_unix();
    uint64_t utc = totp_app_unix_utc(app);
    snprintf(
        app->settings_local_value,
        sizeof(app->settings_local_value),
        "%lu",
        (unsigned long)local);
    snprintf(
        app->settings_utc_value, sizeof(app->settings_utc_value), "%lu", (unsigned long)utc);
    snprintf(
        app->settings_tz_value,
        sizeof(app->settings_tz_value),
        "%d",
        (int)app->prefs.tz_offset_minutes);

    VariableItem* item;

    item = variable_item_list_add(vil, "Local", 1, NULL, app);
    variable_item_set_current_value_index(item, 0);
    variable_item_set_current_value_text(item, app->settings_local_value);

    item = variable_item_list_add(vil, "UTC now", 1, NULL, app);
    variable_item_set_current_value_index(item, 0);
    variable_item_set_current_value_text(item, app->settings_utc_value);

    item = variable_item_list_add(vil, "Offset Mins", 1, NULL, app);
    variable_item_set_current_value_index(item, 0);
    variable_item_set_current_value_text(item, app->settings_tz_value);

    variable_item_list_add(vil, "Set TZ from UTC unix", 0, NULL, app);
    variable_item_list_add(vil, "Edit TZ minutes", 0, NULL, app);

    variable_item_list_set_enter_callback(vil, totp_scene_settings_enter, app);
    variable_item_list_set_selected_item(vil, SettingsItemOffset);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewVarList);
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
        } else if(
            event.event == SettingsItemLocal || event.event == SettingsItemUtc ||
            event.event == SettingsItemOffset) {
            totp_scene_settings_on_enter(app);
        }
    }
    return consumed;
}

void totp_scene_settings_on_exit(void* context) {
    TotpApp* app = context;
    variable_item_list_reset(app->variable_item_list);
}
