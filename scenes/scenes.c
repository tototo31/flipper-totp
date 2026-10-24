#include "scenes.h"

/** Generate handler arrays from config. */
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const totp_on_enter_handlers[])(void*) = {
#include "scenes_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const totp_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "scenes_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const totp_on_exit_handlers[])(void* context) = {
#include "scenes_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers totp_scene_handlers = {
    .on_enter_handlers = totp_on_enter_handlers,
    .on_event_handlers = totp_on_event_handlers,
    .on_exit_handlers = totp_on_exit_handlers,
    .scene_num = TotpSceneCount,
};
