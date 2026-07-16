#define HL_NAME(n) sdl_##n

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <hl.h>

#include "angle.h"

#ifndef HL_ANGLE_REVISION
#define HL_ANGLE_REVISION "unknown"
#endif

typedef struct {
    bool enabled;
    bool debugLayers;
    hl_angle_backend requestedBackend;
    hl_angle_backend activeBackend;
    char lastError[1024];
} hl_angle_config;

static hl_angle_config angle_config = {
    false,
    false,
    HL_ANGLE_BACKEND_AUTO,
    HL_ANGLE_BACKEND_AUTO,
    { 0 }
};

static hl_angle_backend resolve_backend(hl_angle_backend requested_backend) {
    if( requested_backend != HL_ANGLE_BACKEND_AUTO )
        return requested_backend;
#ifdef HL_MAC
    return HL_ANGLE_BACKEND_METAL;
#else
    return HL_ANGLE_BACKEND_VULKAN;
#endif
}

const char *hl_angle_backend_name(hl_angle_backend backend) {
    switch( backend ) {
    case HL_ANGLE_BACKEND_AUTO:
        return "Auto";
    case HL_ANGLE_BACKEND_VULKAN:
        return "Vulkan";
    case HL_ANGLE_BACKEND_METAL:
        return "Metal";
    default:
        return "Unknown";
    }
}

void hl_angle_set_last_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(angle_config.lastError, sizeof(angle_config.lastError), format, args);
    va_end(args);
}

static void validate_backend(hl_angle_backend backend) {
    switch( backend ) {
    case HL_ANGLE_BACKEND_AUTO:
        return;
    case HL_ANGLE_BACKEND_VULKAN:
#ifdef HL_MAC
        hl_error("ANGLE Vulkan is unavailable on macOS.");
        return;
#else
        return;
#endif
    case HL_ANGLE_BACKEND_METAL:
#ifdef HL_MAC
        return;
#else
        hl_error("ANGLE Metal is unavailable on this platform.");
        return;
#endif
    default:
        hl_error("Unknown ANGLE backend value: %d", backend);
    }
}

static SDL_EGLAttrib *SDLCALL get_platform_attributes(void *userdata) {
    hl_angle_config *config = (hl_angle_config *)userdata;
    const size_t attribute_count = config->debugLayers ? 7 : 5;
    SDL_EGLAttrib *attributes = (SDL_EGLAttrib *)SDL_malloc(sizeof(SDL_EGLAttrib) * attribute_count);
    size_t index = 0;

    if( attributes == NULL ) {
        hl_angle_set_last_error("ANGLE EGL attribute allocation failed.");
        return NULL;
    }

    attributes[index++] = EGL_PLATFORM_ANGLE_TYPE_ANGLE;
    attributes[index++] = config->activeBackend == HL_ANGLE_BACKEND_METAL
        ? EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE
        : EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE;
    attributes[index++] = EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE;
    attributes[index++] = EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE;
    if( config->debugLayers ) {
        attributes[index++] = EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE;
        attributes[index++] = EGL_TRUE;
    }
    attributes[index] = EGL_NONE;

    return attributes;
}

bool hl_angle_prepare_sdl(void) {
    if( !angle_config.enabled )
        return true;

    SDL_SetHint(SDL_HINT_VIDEO_FORCE_EGL, "1");
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
    SDL_EGL_SetAttributeCallbacks(get_platform_attributes, NULL, NULL, &angle_config);
    return true;
}

bool hl_angle_is_enabled(void) {
    return angle_config.enabled;
}

hl_angle_backend hl_angle_get_requested_backend(void) {
    return angle_config.requestedBackend;
}

hl_angle_backend hl_angle_get_active_backend(void) {
    return angle_config.activeBackend;
}

const char *hl_angle_get_last_error(void) {
    return angle_config.lastError[0] == '\0' ? NULL : angle_config.lastError;
}

const char *hl_angle_get_revision(void) {
    return HL_ANGLE_REVISION;
}

HL_PRIM void HL_NAME(angle_configure)(int backend, bool debug_layers) {
    hl_angle_backend requested_backend = (hl_angle_backend)backend;

    if( SDL_WasInit(SDL_INIT_VIDEO) != 0 )
        hl_error("ANGLE was selected after SDL video initialization.");

    validate_backend(requested_backend);
    angle_config.enabled = true;
    angle_config.debugLayers = debug_layers;
    angle_config.requestedBackend = requested_backend;
    angle_config.activeBackend = resolve_backend(requested_backend);
    angle_config.lastError[0] = '\0';
}

HL_PRIM bool HL_NAME(angle_is_available)(void) {
    return true;
}

HL_PRIM bool HL_NAME(angle_is_enabled)(void) {
    return angle_config.enabled;
}

HL_PRIM int HL_NAME(angle_get_requested_backend)(void) {
    return angle_config.requestedBackend;
}

HL_PRIM int HL_NAME(angle_get_active_backend)(void) {
    return angle_config.activeBackend;
}

HL_PRIM const char *HL_NAME(angle_get_last_error)(void) {
    return hl_angle_get_last_error();
}

HL_PRIM const char *HL_NAME(angle_get_revision)(void) {
    return hl_angle_get_revision();
}

DEFINE_PRIM(_VOID, angle_configure, _I32 _BOOL);
DEFINE_PRIM(_BOOL, angle_is_available, _NO_ARG);
DEFINE_PRIM(_BOOL, angle_is_enabled, _NO_ARG);
DEFINE_PRIM(_I32, angle_get_requested_backend, _NO_ARG);
DEFINE_PRIM(_I32, angle_get_active_backend, _NO_ARG);
DEFINE_PRIM(_BYTES, angle_get_last_error, _NO_ARG);
DEFINE_PRIM(_BYTES, angle_get_revision, _NO_ARG);
