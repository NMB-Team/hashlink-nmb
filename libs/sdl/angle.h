#ifndef HL_SDL_ANGLE_H
#define HL_SDL_ANGLE_H

#include <stdbool.h>

typedef enum {
    HL_ANGLE_BACKEND_AUTO = 0,
    HL_ANGLE_BACKEND_VULKAN = 1,
    HL_ANGLE_BACKEND_METAL = 2
} hl_angle_backend;

#ifdef HL_SDL_HAS_ANGLE

bool hl_angle_prepare_sdl(void);
bool hl_angle_is_enabled(void);
hl_angle_backend hl_angle_get_requested_backend(void);
hl_angle_backend hl_angle_get_active_backend(void);
const char *hl_angle_get_last_error(void);
const char *hl_angle_get_revision(void);
const char *hl_angle_backend_name(hl_angle_backend backend);
void hl_angle_set_last_error(const char *format, ...);

#else

static inline bool hl_angle_prepare_sdl(void) {
    return true;
}

static inline bool hl_angle_is_enabled(void) {
    return false;
}

static inline hl_angle_backend hl_angle_get_requested_backend(void) {
    return HL_ANGLE_BACKEND_AUTO;
}

static inline hl_angle_backend hl_angle_get_active_backend(void) {
    return HL_ANGLE_BACKEND_AUTO;
}

static inline const char *hl_angle_get_last_error(void) {
    return "";
}

static inline const char *hl_angle_get_revision(void) {
    return "";
}

static inline const char *hl_angle_backend_name(hl_angle_backend backend) {
    (void)backend;
    return "Unavailable";
}

static inline void hl_angle_set_last_error(const char *format, ...) {
    (void)format;
}

#endif

#endif
