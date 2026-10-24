#include "../app.h"
#include "scenes.h"

#include <string.h>

static void totp_scene_set_password_callback(void* context) {
    TotpApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, TotpCustomEventTextDone);
}

void totp_scene_set_password_on_enter(void* context) {
    TotpApp* app = context;
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "New OATH password");
    app->pin_buffer[0] = '\0';
    text_input_set_result_callback(
        app->text_input,
        totp_scene_set_password_callback,
        app,
        app->pin_buffer,
        sizeof(app->pin_buffer),
        true);
    text_input_set_minimum_length(app->text_input, 1);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewTextInput);
}

bool totp_scene_set_password_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom && event.event == TotpCustomEventTextDone) {
        consumed = true;
        if(app->pin_buffer[0] == '\0') {
            snprintf(app->result_message, sizeof(app->result_message), "Password empty");
            scene_manager_next_scene(app->scene_manager, TotpSceneResult);
        } else {
            /* Current prefs password used for VALIDATE if card already locked. */
            totp_app_begin_scan(app, NfcSessionOpSetCode);
        }
    }
    return consumed;
}

void totp_scene_set_password_on_exit(void* context) {
    TotpApp* app = context;
    text_input_reset(app->text_input);
}
