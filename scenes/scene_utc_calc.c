#include "../app.h"
#include "scenes.h"

#include <stdio.h>
#include <stdlib.h>

#define TOTP_TZ_MIN (-14 * 60)
#define TOTP_TZ_MAX (14 * 60)

static int16_t totp_tz_from_local_and_utc(uint32_t local, int32_t utc_unix) {
    int64_t diff = (int64_t)local - (int64_t)utc_unix;
    /* Round to nearest minute. */
    int64_t rounded = (diff >= 0) ? (diff + 30) / 60 : (diff - 30) / 60;
    if(rounded < TOTP_TZ_MIN) rounded = TOTP_TZ_MIN;
    if(rounded > TOTP_TZ_MAX) rounded = TOTP_TZ_MAX;
    return (int16_t)rounded;
}

static void totp_scene_utc_calc_callback(void* context, int32_t number) {
    TotpApp* app = context;
    app->utc_input = number;
    view_dispatcher_send_custom_event(app->view_dispatcher, TotpCustomEventNumberDone);
}

void totp_scene_utc_calc_on_enter(void* context) {
    TotpApp* app = context;
    number_input_set_header_text(app->number_input, "Phone UTC unix");
    /* Default to current chal UTC guess so user can nudge. */
    int32_t def = (int32_t)totp_app_unix_utc(app);
    if(def < 0) def = 0;
    app->utc_input = def;
    number_input_set_result_callback(
        app->number_input,
        totp_scene_utc_calc_callback,
        app,
        app->utc_input,
        0,
        2147483647);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewNumberInput);
}

bool totp_scene_utc_calc_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom && event.event == TotpCustomEventNumberDone) {
        consumed = true;
        uint32_t local = totp_app_rtc_local_unix();
        int16_t tz = totp_tz_from_local_and_utc(local, app->utc_input);
        app->prefs.tz_offset_minutes = tz;
        totp_prefs_save(&app->prefs);
        snprintf(
            app->result_message,
            sizeof(app->result_message),
            "TZ L-U %+d saved",
            (int)tz);
        scene_manager_next_scene(app->scene_manager, TotpSceneResult);
    }
    return consumed;
}

void totp_scene_utc_calc_on_exit(void* context) {
    UNUSED(context);
}
