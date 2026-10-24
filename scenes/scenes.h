#pragma once

#include <gui/scene_manager.h>

/** Generate scene id enum from config. */
#define ADD_SCENE(prefix, name, id) TotpScene##id,
typedef enum {
#include "scenes_config.h"
    TotpSceneCount,
} TotpScene;
#undef ADD_SCENE

extern const SceneManagerHandlers totp_scene_handlers;

/** Scene handlers declared via config. */
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "scenes_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "scenes_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "scenes_config.h"
#undef ADD_SCENE
