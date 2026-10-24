#include "../app.h"
#include "scenes.h"

#include <gui/modules/widget_elements/widget_element.h>
#include <string.h>

static void totp_scene_confirm_auto_unlock_button(
    GuiButtonType result,
    InputType type,
    void* context) {
    TotpApp* app = context;
    if(type != InputTypeShort) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void totp_scene_confirm_auto_unlock_on_enter(void* context) {
    TotpApp* app = context;
    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget,
        0,
        0,
        128,
        48,
        "\e#Auto-unlock\n"
        "OATH password will be "
        "stored on Flipper "
        "storage (app data / SD: "
        "totp_nfc_prefs).\n"
        "Anyone with the Flipper "
        "can unlock your card.\n"
        "Back cancels.");
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        "Enable",
        totp_scene_confirm_auto_unlock_button,
        app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewWidget);
}

bool totp_scene_confirm_auto_unlock_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == GuiButtonTypeCenter) {
            app->prefs.auto_unlock = true;
            if(app->session_password[0] != '\0') {
                totp_app_store_oath_password(app, app->session_password);
            } else {
                totp_prefs_save(&app->prefs);
            }
            if(!scene_manager_search_and_switch_to_previous_scene(
                   app->scene_manager, TotpSceneSettings)) {
                scene_manager_next_scene(app->scene_manager, TotpSceneSettings);
            }
        }
    }
    return consumed;
}

void totp_scene_confirm_auto_unlock_on_exit(void* context) {
    TotpApp* app = context;
    widget_reset(app->widget);
}
