#include "../app.h"
#include "scenes.h"

#include <gui/modules/widget_elements/widget_element.h>
#include <stdio.h>
#include <string.h>

static void totp_scene_confirm_delete_button(
    GuiButtonType result,
    InputType type,
    void* context) {
    TotpApp* app = context;
    if(type != InputTypeShort) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void totp_scene_confirm_delete_on_enter(void* context) {
    TotpApp* app = context;
    char body[128];
    const char* name = app->name_buffer[0] ? app->name_buffer : "(unnamed)";
    snprintf(
        body,
        sizeof(body),
        "\e#Delete credential?\n"
        "%s\n"
        "\n"
        "Removes it from the card. "
        "Back cancels.",
        name);

    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 48, body);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        "Delete",
        totp_scene_confirm_delete_button,
        app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewWidget);
}

bool totp_scene_confirm_delete_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == GuiButtonTypeCenter) {
            totp_app_begin_scan(app, NfcSessionOpDelete);
        }
    }
    return consumed;
}

void totp_scene_confirm_delete_on_exit(void* context) {
    TotpApp* app = context;
    widget_reset(app->widget);
}
