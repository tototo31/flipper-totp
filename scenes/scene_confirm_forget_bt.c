#include "../app.h"
#include "scenes.h"

#include <gui/modules/widget_elements/widget_element.h>

static void totp_scene_confirm_forget_bt_button(
    GuiButtonType result,
    InputType type,
    void* context) {
    TotpApp* app = context;
    if(type != InputTypeShort) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void totp_scene_confirm_forget_bt_on_enter(void* context) {
    TotpApp* app = context;
    const char* body =
        "\e#Forget BT bonds?\n"
        "Removes HID pairings for "
        "this app. Phones/PCs must "
        "pair again.\n"
        "\n"
        "Back cancels.";

    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 48, body);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        "Forget",
        totp_scene_confirm_forget_bt_button,
        app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewWidget);
}

bool totp_scene_confirm_forget_bt_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == GuiButtonTypeCenter) {
            totp_hid_bt_forget_bonds(app->hid_bt);
            notification_message(app->notifications, &sequence_success);

            popup_reset(app->popup);
            popup_set_header(app->popup, "Bonds cleared", 64, 18, AlignCenter, AlignTop);
            popup_set_text(
                app->popup, "Pair again in Settings", 64, 36, AlignCenter, AlignTop);
            view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewPopup);
            furi_delay_ms(700);
            popup_reset(app->popup);

            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, TotpSceneSettings);
        }
    }
    return consumed;
}

void totp_scene_confirm_forget_bt_on_exit(void* context) {
    TotpApp* app = context;
    widget_reset(app->widget);
}
