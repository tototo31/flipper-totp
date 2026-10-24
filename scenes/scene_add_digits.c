#include "../app.h"
#include "scenes.h"

static void totp_scene_add_digits_callback(void* context, int32_t number) {
    TotpApp* app = context;
    app->put_digits = (uint8_t)number;
    view_dispatcher_send_custom_event(app->view_dispatcher, TotpCustomEventNumberDone);
}

void totp_scene_add_digits_on_enter(void* context) {
    TotpApp* app = context;
    if(app->put_digits < 1 || app->put_digits > 9) {
        app->put_digits = 6;
    }
    number_input_set_header_text(app->number_input, "Digits (6 typical)");
    number_input_set_result_callback(
        app->number_input, totp_scene_add_digits_callback, app, app->put_digits, 6, 8);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewNumberInput);
}

bool totp_scene_add_digits_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom && event.event == TotpCustomEventNumberDone) {
        consumed = true;
        totp_app_begin_scan(app, NfcSessionOpPut);
    }
    return consumed;
}

void totp_scene_add_digits_on_exit(void* context) {
    UNUSED(context);
}
