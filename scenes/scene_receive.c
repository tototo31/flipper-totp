#include "../app.h"
#include "scenes.h"

void totp_scene_receive_on_enter(void* context) {
    TotpApp* app = context;
    popup_reset(app->popup);
    popup_set_header(app->popup, "Receive secret", 64, 18, AlignCenter, AlignCenter);
    popup_set_text(
        app->popup,
        "USB: totp_put name secret\n"
        "Then present card",
        64,
        36,
        AlignCenter,
        AlignTop);
    view_dispatcher_switch_to_view(app->view_dispatcher, TotpViewPopup);
}

bool totp_scene_receive_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    /* CLI PUT is handled at app level and navigates to Scan. */
    return false;
}

void totp_scene_receive_on_exit(void* context) {
    TotpApp* app = context;
    popup_reset(app->popup);
}
