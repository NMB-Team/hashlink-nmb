#ifndef HL_SDL_GL_API_H
#define HL_SDL_GL_API_H

#include <stdbool.h>

#ifndef GL_DEBUG_TYPE_ERROR
#define GL_DEBUG_TYPE_ERROR 0x824C
#endif
#ifndef GL_DEBUG_TYPE_PERFORMANCE
#define GL_DEBUG_TYPE_PERFORMANCE 0x8250
#endif
#ifndef GL_DEBUG_TYPE_OTHER
#define GL_DEBUG_TYPE_OTHER 0x8251
#endif
#ifndef GL_DEBUG_OUTPUT
#define GL_DEBUG_OUTPUT 0x92E0
#endif

typedef void (APIENTRY *hl_gl_debug_proc)(
    GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar *message,
    const void *user_data
);

#define HL_GL_REQUIRED_FUNCTIONS(X) \
    X(void, Clear, (GLbitfield mask), glClear) \
    X(GLenum, GetError, (void), glGetError) \
    X(void, Enable, (GLenum cap), glEnable) \
    X(void, Disable, (GLenum cap), glDisable) \
    X(void, Scissor, (GLint x, GLint y, GLsizei width, GLsizei height), glScissor) \
    X(void, ClearColor, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha), glClearColor) \
    X(void, ClearStencil, (GLint value), glClearStencil) \
    X(void, Viewport, (GLint x, GLint y, GLsizei width, GLsizei height), glViewport) \
    X(void, Flush, (void), glFlush) \
    X(void, Finish, (void), glFinish) \
    X(const GLubyte *, GetString, (GLenum name), glGetString) \
    X(const GLubyte *, GetStringi, (GLenum name, GLuint index), glGetStringi) \
    X(void, GetIntegerv, (GLenum pname, GLint *data), glGetIntegerv) \
    X(void, PixelStorei, (GLenum pname, GLint param), glPixelStorei) \
    X(void, PolygonOffset, (GLfloat factor, GLfloat units), glPolygonOffset) \
    X(void, CullFace, (GLenum mode), glCullFace) \
    X(void, FrontFace, (GLenum mode), glFrontFace) \
    X(void, BlendFunc, (GLenum source, GLenum destination), glBlendFunc) \
    X(void, BlendFuncSeparate, (GLenum source_rgb, GLenum destination_rgb, GLenum source_alpha, GLenum destination_alpha), glBlendFuncSeparate) \
    X(void, BlendEquation, (GLenum mode), glBlendEquation) \
    X(void, BlendEquationSeparate, (GLenum mode_rgb, GLenum mode_alpha), glBlendEquationSeparate) \
    X(void, DepthMask, (GLboolean flag), glDepthMask) \
    X(void, DepthFunc, (GLenum function), glDepthFunc) \
    X(void, ColorMask, (GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha), glColorMask) \
    X(void, StencilMaskSeparate, (GLenum face, GLuint mask), glStencilMaskSeparate) \
    X(void, StencilFuncSeparate, (GLenum face, GLenum function, GLint reference, GLuint mask), glStencilFuncSeparate) \
    X(void, StencilOpSeparate, (GLenum face, GLenum stencil_fail, GLenum depth_fail, GLenum depth_pass), glStencilOpSeparate) \
    X(GLuint, CreateProgram, (void), glCreateProgram) \
    X(void, DeleteProgram, (GLuint program), glDeleteProgram) \
    X(void, LinkProgram, (GLuint program), glLinkProgram) \
    X(void, AttachShader, (GLuint program, GLuint shader), glAttachShader) \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei buffer_size, GLsizei *length, GLchar *info_log), glGetProgramInfoLog) \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar *name), glGetUniformLocation) \
    X(GLint, GetAttribLocation, (GLuint program, const GLchar *name), glGetAttribLocation) \
    X(void, UseProgram, (GLuint program), glUseProgram) \
    X(GLuint, CreateShader, (GLenum shader_type), glCreateShader) \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar *const *source, const GLint *length), glShaderSource) \
    X(void, CompileShader, (GLuint shader), glCompileShader) \
    X(void, DeleteShader, (GLuint shader), glDeleteShader) \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei buffer_size, GLsizei *length, GLchar *info_log), glGetShaderInfoLog) \
    X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint *params), glGetShaderiv) \
    X(void, GetProgramiv, (GLuint program, GLenum pname, GLint *params), glGetProgramiv) \
    X(void, GenTextures, (GLsizei count, GLuint *textures), glGenTextures) \
    X(void, ActiveTexture, (GLenum texture), glActiveTexture) \
    X(void, BindTexture, (GLenum target, GLuint texture), glBindTexture) \
    X(void, TexParameterf, (GLenum target, GLenum pname, GLfloat value), glTexParameterf) \
    X(void, TexParameteri, (GLenum target, GLenum pname, GLint value), glTexParameteri) \
    X(void, TexImage2D, (GLenum target, GLint level, GLint internal_format, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels), glTexImage2D) \
    X(void, TexImage3D, (GLenum target, GLint level, GLint internal_format, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void *pixels), glTexImage3D) \
    X(void, CompressedTexImage2D, (GLenum target, GLint level, GLenum internal_format, GLsizei width, GLsizei height, GLint border, GLsizei image_size, const void *data), glCompressedTexImage2D) \
    X(void, CompressedTexImage3D, (GLenum target, GLint level, GLenum internal_format, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei image_size, const void *data), glCompressedTexImage3D) \
    X(void, TexSubImage2D, (GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels), glTexSubImage2D) \
    X(void, TexSubImage3D, (GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels), glTexSubImage3D) \
    X(void, CompressedTexSubImage2D, (GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLsizei image_size, const void *data), glCompressedTexSubImage2D) \
    X(void, CompressedTexSubImage3D, (GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei image_size, const void *data), glCompressedTexSubImage3D) \
    X(void, DeleteTextures, (GLsizei count, const GLuint *textures), glDeleteTextures) \
    X(void, GenerateMipmap, (GLenum target), glGenerateMipmap) \
    X(void, TexStorage2D, (GLenum target, GLsizei levels, GLenum internal_format, GLsizei width, GLsizei height), glTexStorage2D) \
    X(void, TexStorage3D, (GLenum target, GLsizei levels, GLenum internal_format, GLsizei width, GLsizei height, GLsizei depth), glTexStorage3D) \
    X(void, BlitFramebuffer, (GLint source_x0, GLint source_y0, GLint source_x1, GLint source_y1, GLint destination_x0, GLint destination_y0, GLint destination_x1, GLint destination_y1, GLbitfield mask, GLenum filter), glBlitFramebuffer) \
    X(void, GenFramebuffers, (GLsizei count, GLuint *framebuffers), glGenFramebuffers) \
    X(void, BindFramebuffer, (GLenum target, GLuint framebuffer), glBindFramebuffer) \
    X(void, FramebufferTexture2D, (GLenum target, GLenum attachment, GLenum texture_target, GLuint texture, GLint level), glFramebufferTexture2D) \
    X(void, FramebufferTextureLayer, (GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer), glFramebufferTextureLayer) \
    X(void, DeleteFramebuffers, (GLsizei count, const GLuint *framebuffers), glDeleteFramebuffers) \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels), glReadPixels) \
    X(void, ReadBuffer, (GLenum source), glReadBuffer) \
    X(void, DrawBuffers, (GLsizei count, const GLenum *buffers), glDrawBuffers) \
    X(void, GenRenderbuffers, (GLsizei count, GLuint *renderbuffers), glGenRenderbuffers) \
    X(void, BindRenderbuffer, (GLenum target, GLuint renderbuffer), glBindRenderbuffer) \
    X(void, RenderbufferStorage, (GLenum target, GLenum internal_format, GLsizei width, GLsizei height), glRenderbufferStorage) \
    X(void, RenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum internal_format, GLsizei width, GLsizei height), glRenderbufferStorageMultisample) \
    X(void, FramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum renderbuffer_target, GLuint renderbuffer), glFramebufferRenderbuffer) \
    X(void, DeleteRenderbuffers, (GLsizei count, const GLuint *renderbuffers), glDeleteRenderbuffers) \
    X(void, GenBuffers, (GLsizei count, GLuint *buffers), glGenBuffers) \
    X(void, BindBuffer, (GLenum target, GLuint buffer), glBindBuffer) \
    X(void, BindBufferBase, (GLenum target, GLuint index, GLuint buffer), glBindBufferBase) \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage), glBufferData) \
    X(void, BufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void *data), glBufferSubData) \
    X(void *, MapBufferRange, (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access), glMapBufferRange) \
    X(GLboolean, UnmapBuffer, (GLenum target), glUnmapBuffer) \
    X(void, EnableVertexAttribArray, (GLuint index), glEnableVertexAttribArray) \
    X(void, DisableVertexAttribArray, (GLuint index), glDisableVertexAttribArray) \
    X(void, VertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer), glVertexAttribPointer) \
    X(void, VertexAttribIPointer, (GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer), glVertexAttribIPointer) \
    X(void, VertexAttribDivisor, (GLuint index, GLuint divisor), glVertexAttribDivisor) \
    X(void, DeleteBuffers, (GLsizei count, const GLuint *buffers), glDeleteBuffers) \
    X(void, Uniform1i, (GLint location, GLint value), glUniform1i) \
    X(void, Uniform3fv, (GLint location, GLsizei count, const GLfloat *value), glUniform3fv) \
    X(void, Uniform4fv, (GLint location, GLsizei count, const GLfloat *value), glUniform4fv) \
    X(void, UniformMatrix3fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value), glUniformMatrix3fv) \
    X(void, UniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value), glUniformMatrix4fv) \
    X(void, Uniform1f, (GLint location, GLfloat x), glUniform1f) \
    X(void, Uniform2f, (GLint location, GLfloat x, GLfloat y), glUniform2f) \
    X(void, Uniform3f, (GLint location, GLfloat x, GLfloat y, GLfloat z), glUniform3f) \
    X(void, Uniform4f, (GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w), glUniform4f) \
    X(void, DrawElements, (GLenum mode, GLsizei count, GLenum type, const void *indices), glDrawElements) \
    X(void, DrawArrays, (GLenum mode, GLint first, GLsizei count), glDrawArrays) \
    X(void, DrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instance_count), glDrawElementsInstanced) \
    X(void, DrawArraysInstanced, (GLenum mode, GLint first, GLsizei count, GLsizei instance_count), glDrawArraysInstanced) \
    X(void, GenVertexArrays, (GLsizei count, GLuint *arrays), glGenVertexArrays) \
    X(void, BindVertexArray, (GLuint array), glBindVertexArray) \
    X(void, DeleteVertexArrays, (GLsizei count, const GLuint *arrays), glDeleteVertexArrays) \
    X(GLuint, GetUniformBlockIndex, (GLuint program, const GLchar *name), glGetUniformBlockIndex) \
    X(void, UniformBlockBinding, (GLuint program, GLuint block_index, GLuint block_binding), glUniformBlockBinding)

#define HL_GL_OPTIONAL_FUNCTIONS(X) \
    X(void, ClearDepth, (GLdouble depth), glClearDepth) \
    X(void, ClearDepthf, (GLfloat depth), glClearDepthf) \
    X(void, PolygonMode, (GLenum face, GLenum mode), glPolygonMode) \
    X(void, ColorMaski, (GLuint index, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha), glColorMaski) \
    X(void, BindFragDataLocation, (GLuint program, GLuint color, const GLchar *name), glBindFragDataLocation) \
    X(void, BindImageTexture, (GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format), glBindImageTexture) \
    X(void, TexImage2DMultisample, (GLenum target, GLsizei samples, GLenum internal_format, GLsizei width, GLsizei height, GLboolean fixed_locations), glTexImage2DMultisample) \
    X(void, FramebufferTexture, (GLenum target, GLenum attachment, GLuint texture, GLint level), glFramebufferTexture) \
    X(void, GetBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, void *data), glGetBufferSubData) \
    X(void, DispatchCompute, (GLuint x, GLuint y, GLuint z), glDispatchCompute) \
    X(void, MemoryBarrier, (GLbitfield barriers), glMemoryBarrier) \
    X(GLuint, GetProgramResourceIndex, (GLuint program, GLenum interface_type, const GLchar *name), glGetProgramResourceIndex) \
    X(void, ShaderStorageBlockBinding, (GLuint program, GLuint block_index, GLuint block_binding), glShaderStorageBlockBinding) \
    X(void, MultiDrawElementsIndirect, (GLenum mode, GLenum type, const void *indirect, GLsizei draw_count, GLsizei stride), glMultiDrawElementsIndirect) \
    X(void, MultiDrawElementsIndirectCountARB, (GLenum mode, GLenum type, const void *indirect, GLintptr draw_count, GLsizei max_draw_count, GLsizei stride), glMultiDrawElementsIndirectCountARB) \
    X(void, DebugMessageCallback, (hl_gl_debug_proc callback, const void *user_data), glDebugMessageCallback) \
    X(void, DebugMessageControl, (GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint *ids, GLboolean enabled), glDebugMessageControl) \
    X(void, BeginQuery, (GLenum target, GLuint query), glBeginQuery) \
    X(void, EndQuery, (GLenum target), glEndQuery) \
    X(void, GenQueries, (GLsizei count, GLuint *queries), glGenQueries) \
    X(void, DeleteQueries, (GLsizei count, const GLuint *queries), glDeleteQueries) \
    X(void, GetQueryObjectiv, (GLuint query, GLenum pname, GLint *params), glGetQueryObjectiv) \
    X(void, GetQueryObjectuiv, (GLuint query, GLenum pname, GLuint *params), glGetQueryObjectuiv) \
    X(void, GetQueryObjectui64v, (GLuint query, GLenum pname, GLuint64 *params), glGetQueryObjectui64v) \
    X(void, QueryCounter, (GLuint query, GLenum target), glQueryCounter)

#define HL_GL_DECLARE_TYPE(return_type, field, arguments, symbol) \
    typedef return_type (APIENTRYP hl_gl_##field##_proc) arguments;
HL_GL_REQUIRED_FUNCTIONS(HL_GL_DECLARE_TYPE)
HL_GL_OPTIONAL_FUNCTIONS(HL_GL_DECLARE_TYPE)
#undef HL_GL_DECLARE_TYPE

typedef struct {
#define HL_GL_DECLARE_FIELD(return_type, field, arguments, symbol) hl_gl_##field##_proc field;
    HL_GL_REQUIRED_FUNCTIONS(HL_GL_DECLARE_FIELD)
    HL_GL_OPTIONAL_FUNCTIONS(HL_GL_DECLARE_FIELD)
#undef HL_GL_DECLARE_FIELD
} hl_gl_api;

typedef enum {
    HL_GL_PROFILE_DESKTOP,
    HL_GL_PROFILE_GLES
} hl_gl_profile;

typedef struct {
    hl_gl_profile profile;
    bool isANGLE;
    int major;
    int minor;
    bool hasCompute;
    bool hasSSBO;
    bool hasBufferReadback;
    bool hasMapBufferRange;
    bool hasTextureMultisample;
    bool hasDrawBuffers;
    bool hasDrawBuffersIndexed;
    bool hasFramebufferTexture;
    bool hasPolygonMode;
    bool hasIndirectDraw;
    bool hasIndirectCount;
    bool hasRGTC;
    bool hasTextureCubeMapSeamless;
    bool hasQueries;
} hl_gl_capabilities;

extern hl_gl_api glApi;
extern hl_gl_capabilities glCaps;

bool hl_gl_api_load(void);
bool hl_gl_api_has_extension(const char *name);
const char *hl_gl_api_get_last_error(void);
const char *hl_gl_api_get_vendor(void);
const char *hl_gl_api_get_renderer(void);
const char *hl_gl_api_get_version(void);
const char *hl_gl_api_get_glsl_version(void);

#ifdef HL_GL_API_REMAP
#define glClear glApi.Clear
#define glGetError glApi.GetError
#define glEnable glApi.Enable
#define glDisable glApi.Disable
#define glScissor glApi.Scissor
#define glClearColor glApi.ClearColor
#define glClearDepth glApi.ClearDepth
#define glClearDepthf glApi.ClearDepthf
#define glClearStencil glApi.ClearStencil
#define glViewport glApi.Viewport
#define glFlush glApi.Flush
#define glFinish glApi.Finish
#define glGetString glApi.GetString
#define glGetStringi glApi.GetStringi
#define glGetIntegerv glApi.GetIntegerv
#define glPixelStorei glApi.PixelStorei
#define glPolygonMode glApi.PolygonMode
#define glPolygonOffset glApi.PolygonOffset
#define glCullFace glApi.CullFace
#define glFrontFace glApi.FrontFace
#define glBlendFunc glApi.BlendFunc
#define glBlendFuncSeparate glApi.BlendFuncSeparate
#define glBlendEquation glApi.BlendEquation
#define glBlendEquationSeparate glApi.BlendEquationSeparate
#define glDepthMask glApi.DepthMask
#define glDepthFunc glApi.DepthFunc
#define glColorMask glApi.ColorMask
#define glColorMaski glApi.ColorMaski
#define glBindFragDataLocation glApi.BindFragDataLocation
#define glStencilMaskSeparate glApi.StencilMaskSeparate
#define glStencilFuncSeparate glApi.StencilFuncSeparate
#define glStencilOpSeparate glApi.StencilOpSeparate
#define glCreateProgram glApi.CreateProgram
#define glDeleteProgram glApi.DeleteProgram
#define glLinkProgram glApi.LinkProgram
#define glAttachShader glApi.AttachShader
#define glGetProgramInfoLog glApi.GetProgramInfoLog
#define glGetUniformLocation glApi.GetUniformLocation
#define glGetAttribLocation glApi.GetAttribLocation
#define glUseProgram glApi.UseProgram
#define glCreateShader glApi.CreateShader
#define glShaderSource glApi.ShaderSource
#define glCompileShader glApi.CompileShader
#define glDeleteShader glApi.DeleteShader
#define glGetShaderInfoLog glApi.GetShaderInfoLog
#define glGetShaderiv glApi.GetShaderiv
#define glGetProgramiv glApi.GetProgramiv
#define glGenTextures glApi.GenTextures
#define glActiveTexture glApi.ActiveTexture
#define glBindTexture glApi.BindTexture
#define glTexParameterf glApi.TexParameterf
#define glTexParameteri glApi.TexParameteri
#define glTexImage2D glApi.TexImage2D
#define glTexImage3D glApi.TexImage3D
#define glCompressedTexImage2D glApi.CompressedTexImage2D
#define glCompressedTexImage3D glApi.CompressedTexImage3D
#define glTexSubImage2D glApi.TexSubImage2D
#define glTexSubImage3D glApi.TexSubImage3D
#define glCompressedTexSubImage2D glApi.CompressedTexSubImage2D
#define glCompressedTexSubImage3D glApi.CompressedTexSubImage3D
#define glDeleteTextures glApi.DeleteTextures
#define glGenerateMipmap glApi.GenerateMipmap
#define glTexStorage2D glApi.TexStorage2D
#define glTexStorage3D glApi.TexStorage3D
#define glTexImage2DMultisample glApi.TexImage2DMultisample
#define glBlitFramebuffer glApi.BlitFramebuffer
#define glGenFramebuffers glApi.GenFramebuffers
#define glBindFramebuffer glApi.BindFramebuffer
#define glFramebufferTexture glApi.FramebufferTexture
#define glFramebufferTexture2D glApi.FramebufferTexture2D
#define glFramebufferTextureLayer glApi.FramebufferTextureLayer
#define glDeleteFramebuffers glApi.DeleteFramebuffers
#define glReadPixels glApi.ReadPixels
#define glReadBuffer glApi.ReadBuffer
#define glDrawBuffers glApi.DrawBuffers
#define glGenRenderbuffers glApi.GenRenderbuffers
#define glBindRenderbuffer glApi.BindRenderbuffer
#define glRenderbufferStorage glApi.RenderbufferStorage
#define glRenderbufferStorageMultisample glApi.RenderbufferStorageMultisample
#define glFramebufferRenderbuffer glApi.FramebufferRenderbuffer
#define glDeleteRenderbuffers glApi.DeleteRenderbuffers
#define glGenBuffers glApi.GenBuffers
#define glBindBuffer glApi.BindBuffer
#define glBindBufferBase glApi.BindBufferBase
#define glBufferData glApi.BufferData
#define glBufferSubData glApi.BufferSubData
#define glMapBufferRange glApi.MapBufferRange
#define glUnmapBuffer glApi.UnmapBuffer
#define glGetBufferSubData glApi.GetBufferSubData
#define glEnableVertexAttribArray glApi.EnableVertexAttribArray
#define glDisableVertexAttribArray glApi.DisableVertexAttribArray
#define glVertexAttribPointer glApi.VertexAttribPointer
#define glVertexAttribIPointer glApi.VertexAttribIPointer
#define glVertexAttribDivisor glApi.VertexAttribDivisor
#define glDeleteBuffers glApi.DeleteBuffers
#define glUniform1i glApi.Uniform1i
#define glUniform3fv glApi.Uniform3fv
#define glUniform4fv glApi.Uniform4fv
#define glUniformMatrix3fv glApi.UniformMatrix3fv
#define glUniformMatrix4fv glApi.UniformMatrix4fv
#define glUniform1f glApi.Uniform1f
#define glUniform2f glApi.Uniform2f
#define glUniform3f glApi.Uniform3f
#define glUniform4f glApi.Uniform4f
#define glBindImageTexture glApi.BindImageTexture
#define glDispatchCompute glApi.DispatchCompute
#define glMemoryBarrier glApi.MemoryBarrier
#define glDrawElements glApi.DrawElements
#define glDrawArrays glApi.DrawArrays
#define glDrawElementsInstanced glApi.DrawElementsInstanced
#define glDrawArraysInstanced glApi.DrawArraysInstanced
#define glMultiDrawElementsIndirect glApi.MultiDrawElementsIndirect
#define glMultiDrawElementsIndirectCountARB glApi.MultiDrawElementsIndirectCountARB
#define glGenVertexArrays glApi.GenVertexArrays
#define glBindVertexArray glApi.BindVertexArray
#define glDeleteVertexArrays glApi.DeleteVertexArrays
#define glGetUniformBlockIndex glApi.GetUniformBlockIndex
#define glUniformBlockBinding glApi.UniformBlockBinding
#define glGetProgramResourceIndex glApi.GetProgramResourceIndex
#define glShaderStorageBlockBinding glApi.ShaderStorageBlockBinding
#define glDebugMessageCallback glApi.DebugMessageCallback
#define glDebugMessageControl glApi.DebugMessageControl
#define glBeginQuery glApi.BeginQuery
#define glEndQuery glApi.EndQuery
#define glGenQueries glApi.GenQueries
#define glDeleteQueries glApi.DeleteQueries
#define glGetQueryObjectiv glApi.GetQueryObjectiv
#define glGetQueryObjectuiv glApi.GetQueryObjectuiv
#define glGetQueryObjectui64v glApi.GetQueryObjectui64v
#define glQueryCounter glApi.QueryCounter
#endif

#endif
