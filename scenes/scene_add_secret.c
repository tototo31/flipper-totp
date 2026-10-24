#include "../app.h"
#include "scenes.h"

static void totp_scene_add_secret_callback(void* context) {
    TotpApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, TotpCustomEventTextDone);
}

void totp_scene_add_secret_on_enter(void* context) {
    TotpApp* app = context;
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Secret (Base32/hex)");
    app->secret_buffer[0] = '\0';
    text_input_set_result_callback(
        app->text_input,
        totp_scene_add_secret_callback,
        app,
        app->secret_buffer,
        sizeof(app->secret_buffer),
        true);
    text_input_set_minimum_length(app->text_input, 1);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewTextInput);
}

bool totp_scene_add_secret_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom && event.event == TotpCustomEventTextDone) {
        consumed = true;
        scene_manager_next_scene(app->scene_manager, TotpSceneAddDigits);
    }
    return consumed;
}

void totp_scene_add_secret_on_exit(void* context) {
    TotpApp* app = context;
    text_input_reset(app->text_input);
}
