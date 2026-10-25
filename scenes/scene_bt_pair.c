#include "../app.h"
#include "scenes.h"

#include <stdio.h>

static void totp_scene_bt_pair_rebuild(TotpApp* app) {
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Bluetooth Pairing");
    widget_add_string_element(
        app->widget, 4, 16, AlignLeft, AlignTop, FontSecondary, "1. Phone/PC BT settings");
    widget_add_string_element(
        app->widget, 4, 26, AlignLeft, AlignTop, FontSecondary, "2. Find HID-<Flipper>");
    widget_add_string_element(
        app->widget, 4, 36, AlignLeft, AlignTop, FontSecondary, "3. Pair, then Type via BT");

    const char* status = "Status: Off";
    if(totp_hid_bt_is_connected(app->hid_bt)) {
        status = "Status: Connected!";
    } else if(totp_hid_bt_is_enabled(app->hid_bt)) {
        status = "Status: Advertising…";
    }
    widget_add_string_element(app->widget, 4, 50, AlignLeft, AlignTop, FontSecondary, status);
}

void totp_scene_bt_pair_on_enter(void* context) {
    TotpApp* app = context;
    totp_hid_bt_enable(app->hid_bt);
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    totp_scene_bt_pair_rebuild(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewWidget);
}

bool totp_scene_bt_pair_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    if(event.type == SceneManagerEventTypeTick) {
        totp_scene_bt_pair_rebuild(app);
        return true;
    }
    UNUSED(app);
    return false;
}

void totp_scene_bt_pair_on_exit(void* context) {
    TotpApp* app = context;
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
    widget_reset(app->widget);
}
