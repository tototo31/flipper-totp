#include "../app.h"
#include "scenes.h"

static void totp_scene_tz_callback(void* context, int32_t number) {
    TotpApp* app = context;
    app->prefs.tz_offset_minutes = (int16_t)number;
    totp_prefs_save(&app->prefs);
    view_dispatcher_send_custom_event(app->view_dispatcher, TotpCustomEventNumberDone);
}

void totp_scene_tz_on_enter(void* context) {
    TotpApp* app = context;
    number_input_set_header_text(app->number_input, "Local-UTC minutes");
    number_input_set_result_callback(
        app->number_input,
        totp_scene_tz_callback,
        app,
        app->prefs.tz_offset_minutes,
        -14 * 60,
        14 * 60);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewNumberInput);
}

bool totp_scene_tz_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom && event.event == TotpCustomEventNumberDone) {
        consumed = true;
        if(!scene_manager_search_and_switch_to_previous_scene(
               app->scene_manager, TotpSceneSettings)) {
            scene_manager_search_and_switch_to_previous_scene(app->scene_manager, TotpSceneStart);
        }
    }
    return consumed;
}

void totp_scene_tz_on_exit(void* context) {
    UNUSED(context);
}
