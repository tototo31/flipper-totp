#include "../app.h"
#include "scenes.h"

#include <string.h>

static void totp_scene_pin_callback(void* context) {
    TotpApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, TotpCustomEventNfcSuccess);
}

void totp_scene_pin_on_enter(void* context) {
    TotpApp* app = context;
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "OATH password");
    app->pin_buffer[0] = '\0';
    text_input_set_result_callback(
        app->text_input,
        totp_scene_pin_callback,
        app,
        app->pin_buffer,
        sizeof(app->pin_buffer),
        true);
    text_input_set_minimum_length(app->text_input, 1);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewTextInput);
}

bool totp_scene_pin_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        strncpy(app->prefs.oath_password, app->pin_buffer, sizeof(app->prefs.oath_password) - 1);
        totp_prefs_save(&app->prefs);
        /* Re-scan with password. */
        scene_manager_next_scene(app->scene_manager, TotpSceneScan);
    }
    return consumed;
}

void totp_scene_pin_on_exit(void* context) {
    TotpApp* app = context;
    text_input_reset(app->text_input);
}
