#include "../app.h"
#include "scenes.h"

void totp_scene_result_on_enter(void* context) {
    TotpApp* app = context;
    popup_reset(app->popup);
    popup_set_header(app->popup, "Result", 64, 22, AlignCenter, AlignCenter);
    popup_set_text(
        app->popup,
        app->result_message[0] ? app->result_message : "Done",
        64,
        40,
        AlignCenter,
        AlignTop);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewPopup);
}

bool totp_scene_result_on_event(void* context, SceneManagerEvent event) {
    TotpApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeBack) {
        consumed = true;
        if(scene_manager_search_and_switch_to_previous_scene(
               app->scene_manager, TotpSceneSettings)) {
            /* Prefer Settings after TZ calculator. */
        } else if(!scene_manager_search_and_switch_to_previous_scene(
                      app->scene_manager, TotpSceneStart)) {
            scene_manager_next_scene(app->scene_manager, TotpSceneStart);
        }
    }
    return consumed;
}

void totp_scene_result_on_exit(void* context) {
    TotpApp* app = context;
    popup_reset(app->popup);
}
