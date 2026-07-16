#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "hlsystem.h"

#ifdef HL_MAC
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include <GL/glcorearb.h>
#endif

#include "angle.h"
#include "gl_api.h"

hl_gl_api glApi;
hl_gl_capabilities glCaps;

static const char *gl_vendor = "";
static const char *gl_renderer = "";
static const char *gl_version = "";
static const char *glsl_version = "";
static char gl_last_error[2048];

static void *load_function(const char *name) {
    return (void *)SDL_GL_GetProcAddress(name);
}

static void set_load_error(const char *missing_function) {
    const char *provider = hl_angle_is_enabled() ? "ANGLE" : "System";
    const char *angle_error = hl_angle_get_last_error();
    snprintf(
        gl_last_error,
        sizeof(gl_last_error),
        "Required GL function %s could not be loaded.\n"
        "Context provider: %s\n"
        "ANGLE backend: %s\n"
        "GL_VENDOR: %s\n"
        "GL_RENDERER: %s\n"
        "GL_VERSION: %s\n"
        "GLSL version: %s\n"
        "ANGLE revision: %s\n"
        "ANGLE error: %s\n"
        "SDL error: %s",
        missing_function,
        provider,
        hl_angle_backend_name(hl_angle_get_active_backend()),
        gl_vendor,
        gl_renderer,
        gl_version,
        glsl_version,
        hl_angle_get_revision(),
        angle_error == NULL ? "" : angle_error,
        SDL_GetError()
    );
}

bool hl_gl_api_has_extension(const char *name) {
    GLint extension_count = 0;

    if( glApi.GetStringi != NULL && glApi.GetIntegerv != NULL ) {
        glApi.GetIntegerv(GL_NUM_EXTENSIONS, &extension_count);
        for( GLint index = 0; index < extension_count; index++ ) {
            const char *extension = (const char *)glApi.GetStringi(GL_EXTENSIONS, (GLuint)index);
            if( extension != NULL && strcmp(extension, name) == 0 )
                return true;
        }
        return false;
    }

    if( glApi.GetString != NULL ) {
        const char *extensions = (const char *)glApi.GetString(GL_EXTENSIONS);
        const size_t name_length = strlen(name);
        const char *position = extensions;
        while( position != NULL && *position != '\0' ) {
            position = strstr(position, name);
            if( position == NULL )
                break;
            if(
                (position == extensions || position[-1] == ' ')
                && (position[name_length] == '\0' || position[name_length] == ' ')
            )
                return true;
            position += name_length;
        }
    }

    return false;
}

static bool version_at_least(int major, int minor) {
    return glCaps.major > major || (glCaps.major == major && glCaps.minor >= minor);
}

static void detect_capabilities(void) {
    GLint major = 0;
    GLint minor = 0;

    gl_vendor = (const char *)glApi.GetString(GL_VENDOR);
    gl_renderer = (const char *)glApi.GetString(GL_RENDERER);
    gl_version = (const char *)glApi.GetString(GL_VERSION);
    glsl_version = (const char *)glApi.GetString(GL_SHADING_LANGUAGE_VERSION);

    if( gl_vendor == NULL )
        gl_vendor = "";
    if( gl_renderer == NULL )
        gl_renderer = "";
    if( gl_version == NULL )
        gl_version = "";
    if( glsl_version == NULL )
        glsl_version = "";

    glCaps.profile = strstr(gl_version, "OpenGL ES") != NULL
        ? HL_GL_PROFILE_GLES
        : HL_GL_PROFILE_DESKTOP;
    glCaps.isANGLE = hl_angle_is_enabled() || strstr(gl_renderer, "ANGLE") != NULL;

    glApi.GetIntegerv(GL_MAJOR_VERSION, &major);
    glApi.GetIntegerv(GL_MINOR_VERSION, &minor);
    if( major <= 0 ) {
        if( glCaps.profile == HL_GL_PROFILE_GLES )
            sscanf(gl_version, "OpenGL ES %d.%d", &major, &minor);
        else
            sscanf(gl_version, "%d.%d", &major, &minor);
    }
    glCaps.major = major;
    glCaps.minor = minor;

    glCaps.hasMapBufferRange = glApi.MapBufferRange != NULL && glApi.UnmapBuffer != NULL;
    glCaps.hasBufferReadback = glApi.GetBufferSubData != NULL || glCaps.hasMapBufferRange;
    glCaps.hasTextureMultisample = glApi.TexImage2DMultisample != NULL;
    glCaps.hasDrawBuffers = glApi.DrawBuffers != NULL;
    glCaps.hasDrawBuffersIndexed = glApi.ColorMaski != NULL;
    glCaps.hasFramebufferTexture = glApi.FramebufferTexture != NULL;
    glCaps.hasPolygonMode = glCaps.profile == HL_GL_PROFILE_DESKTOP && glApi.PolygonMode != NULL;
    glCaps.hasCompute = glApi.DispatchCompute != NULL
        && glApi.MemoryBarrier != NULL
        && (
            (glCaps.profile == HL_GL_PROFILE_GLES && version_at_least(3, 1))
            || (glCaps.profile == HL_GL_PROFILE_DESKTOP && version_at_least(4, 3))
        );
    glCaps.hasSSBO = glCaps.hasCompute
        && glApi.GetProgramResourceIndex != NULL
        && glApi.ShaderStorageBlockBinding != NULL;
    glCaps.hasIndirectDraw = glApi.MultiDrawElementsIndirect != NULL;
    glCaps.hasIndirectCount = glApi.MultiDrawElementsIndirectCountARB != NULL;
    glCaps.hasRGTC = hl_gl_api_has_extension("GL_EXT_texture_compression_rgtc")
        || hl_gl_api_has_extension("GL_ARB_texture_compression_rgtc");
    glCaps.hasTextureCubeMapSeamless = glCaps.profile == HL_GL_PROFILE_DESKTOP
        && (version_at_least(3, 2) || hl_gl_api_has_extension("GL_ARB_seamless_cube_map"));
    glCaps.hasQueries = glApi.GenQueries != NULL
        && glApi.DeleteQueries != NULL
        && glApi.BeginQuery != NULL
        && glApi.EndQuery != NULL
        && (glApi.GetQueryObjectiv != NULL || glApi.GetQueryObjectuiv != NULL);
}

bool hl_gl_api_load(void) {
    const char *missing_function = NULL;
    memset(&glApi, 0, sizeof(glApi));
    memset(&glCaps, 0, sizeof(glCaps));
    gl_last_error[0] = '\0';

#define HL_GL_LOAD_REQUIRED(return_type, field, arguments, symbol) \
    glApi.field = (hl_gl_##field##_proc)load_function(#symbol); \
    if( glApi.field == NULL && missing_function == NULL ) missing_function = #symbol;
    HL_GL_REQUIRED_FUNCTIONS(HL_GL_LOAD_REQUIRED)
#undef HL_GL_LOAD_REQUIRED

#define HL_GL_LOAD_OPTIONAL(return_type, field, arguments, symbol) \
    glApi.field = (hl_gl_##field##_proc)load_function(#symbol);
    HL_GL_OPTIONAL_FUNCTIONS(HL_GL_LOAD_OPTIONAL)
#undef HL_GL_LOAD_OPTIONAL

    if( glApi.GetString != NULL && glApi.GetIntegerv != NULL )
        detect_capabilities();

    if( missing_function == NULL ) {
        if( glCaps.profile == HL_GL_PROFILE_GLES && glApi.ClearDepthf == NULL )
            missing_function = "glClearDepthf";
        else if( glCaps.profile == HL_GL_PROFILE_DESKTOP && glApi.ClearDepth == NULL )
            missing_function = "glClearDepth";
    }

    if( missing_function != NULL ) {
        set_load_error(missing_function);
        return false;
    }

    return true;
}

const char *hl_gl_api_get_last_error(void) {
    return gl_last_error[0] == '\0' ? NULL : gl_last_error;
}

const char *hl_gl_api_get_vendor(void) {
    return gl_vendor;
}

const char *hl_gl_api_get_renderer(void) {
    return gl_renderer;
}

const char *hl_gl_api_get_version(void) {
    return gl_version;
}

const char *hl_gl_api_get_glsl_version(void) {
    return glsl_version;
}
