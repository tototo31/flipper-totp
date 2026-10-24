#include "../app.h"
#include "scenes.h"

static void totp_scene_tz_callback(void* context, int32_t number) {
    TotpApp* app = context;
    app->prefs.tz_offset_minutes = (int16_t)number;
    totp_prefs_save(&app->prefs);
    view_dispatcher_send_custom_event(app->view_dispatcher, TotpCustomEventCodesRefresh);
}

void totp_scene_tz_on_enter(void* context) {
    TotpApp* app = context;
    number_input_set_header_text(app->number_input, "TZ offset (minutes)");
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
    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        scene_manager_previous_scene(app->scene_manager);
    }
    return consumed;
}

void totp_scene_tz_on_exit(void* context) {
    UNUSED(context);
}
