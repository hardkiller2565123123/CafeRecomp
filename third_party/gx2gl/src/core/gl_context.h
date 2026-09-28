#ifndef GL33_CONTEXT_H
#define GL33_CONTEXT_H

#include "gl/gl.h"
#include <coreinit/mutex.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*glGenBuffers)(GLsizei, GLuint*);
    void (*glDeleteBuffers)(GLsizei, const GLuint*);
    GLboolean (*glIsBuffer)(GLuint);
    void (*glBindBuffer)(GLenum, GLuint);
    void (*glBindBufferBase)(GLenum, GLuint, GLuint);
    void (*glBindBufferRange)(GLenum, GLuint, GLuint, GLintptr, GLsizeiptr);
    void (*glBufferData)(GLenum, GLsizeiptr, const GLvoid*, GLenum);
    void (*glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const GLvoid*);
    void (*glGetBufferParameteriv)(GLenum, GLenum, GLint *);
    void (*glGetBufferPointerv)(GLenum, GLenum, GLvoid **);
    void (*glEnable)(GLenum);
    void (*glDisable)(GLenum);
    GLboolean (*glIsEnabled)(GLenum);
    void (*glClearColor)(GLclampf, GLclampf, GLclampf, GLclampf);
    void (*glClearDepth)(GLclampd);
    void (*glClearStencil)(GLint);
    void (*glClear)(GLbitfield);
    void (*glDrawArrays)(GLenum, GLint, GLsizei);
    void (*glDrawArraysInstanced)(GLenum, GLint, GLsizei, GLsizei);
    void (*glDrawElements)(GLenum, GLsizei, GLenum, const GLvoid*);
    void (*glDrawElementsInstanced)(GLenum, GLsizei, GLenum, const GLvoid*, GLsizei);
    void (*glGetIntegerv)(GLenum, GLint*);
    void (*glGetFloatv)(GLenum, GLfloat*);
    const GLubyte* (*glGetString)(GLenum);
    const GLubyte* (*glGetStringi)(GLenum, GLuint);
    void* (*glMapBuffer)(GLenum, GLenum);
    void* (*glMapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
    GLboolean (*glUnmapBuffer)(GLenum);
    void (*glGenTextures)(GLsizei, GLuint*);
    void (*glDeleteTextures)(GLsizei, const GLuint*);
    GLboolean (*glIsTexture)(GLuint);
    void (*glGenSamplers)(GLsizei, GLuint*);
    void (*glDeleteSamplers)(GLsizei, const GLuint*);
    GLboolean (*glIsSampler)(GLuint);
    void (*glBindTexture)(GLenum, GLuint);
    void (*glBindSampler)(GLuint, GLuint);
    void (*glActiveTexture)(GLenum);
    void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
    void (*glTexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
    void (*glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*);
    void (*glTexSubImage3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*);
    void (*glTexParameteri)(GLenum, GLenum, GLint);
    void (*glTexParameterf)(GLenum, GLenum, GLfloat);
    void (*glTexParameteriv)(GLenum, GLenum, const GLint *);
    void (*glTexParameterfv)(GLenum, GLenum, const GLfloat *);
    void (*glGetTexParameteriv)(GLenum, GLenum, GLint *);
    void (*glGetTexParameterfv)(GLenum, GLenum, GLfloat *);
    void (*glSamplerParameteriv)(GLuint, GLenum, const GLint *);
    void (*glSamplerParameterfv)(GLuint, GLenum, const GLfloat *);
    void (*glSamplerParameteri)(GLuint, GLenum, GLint);
    void (*glSamplerParameterf)(GLuint, GLenum, GLfloat);
    void (*glGetSamplerParameteriv)(GLuint, GLenum, GLint *);
    void (*glGetSamplerParameterfv)(GLuint, GLenum, GLfloat *);
    void (*glGenerateMipmap)(GLenum);
    GLuint (*glCreateShader)(GLenum);
    void (*glDeleteShader)(GLuint);
    GLboolean (*glIsShader)(GLuint);
    void (*glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void (*glGetShaderSource)(GLuint, GLsizei, GLsizei *, GLchar *);
    void (*glCompileShader)(GLuint);
    GLuint (*glCreateProgram)(void);
    void (*glDeleteProgram)(GLuint);
    GLboolean (*glIsProgram)(GLuint);
    void (*glAttachShader)(GLuint, GLuint);
    void (*glDetachShader)(GLuint, GLuint);
    void (*glLinkProgram)(GLuint);
    void (*glValidateProgram)(GLuint);
    void (*glUseProgram)(GLuint);
    void (*glGetShaderiv)(GLuint, GLenum, GLint *);
    void (*glGetProgramiv)(GLuint, GLenum, GLint *);
    void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void (*glBindAttribLocation)(GLuint, GLuint, const GLchar *);
    void (*glGetAttachedShaders)(GLuint, GLsizei, GLsizei *, GLuint *);
    void (*glGetActiveAttrib)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
    void (*glGetActiveUniform)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
    GLint (*glGetUniformLocation)(GLuint, const GLchar*);
    GLint (*glGetAttribLocation)(GLuint, const GLchar*);
    GLuint (*glGetUniformBlockIndex)(GLuint, const GLchar*);
    void (*glUniformBlockBinding)(GLuint, GLuint, GLuint);
    void (*glWiiULoadShaderGroup)(GLuint, const void*);
    void (*glWiiULoadShaderGroupGFD)(GLuint, GLuint, const void*);
    void (*glUniform1f)(GLint, GLfloat);
    void (*glUniform1fv)(GLint, GLsizei, const GLfloat*);
    void (*glUniform1i)(GLint, GLint);
    void (*glUniform1iv)(GLint, GLsizei, const GLint*);
    void (*glUniform2f)(GLint, GLfloat, GLfloat);
    void (*glUniform2fv)(GLint, GLsizei, const GLfloat*);
    void (*glUniform2i)(GLint, GLint, GLint);
    void (*glUniform2iv)(GLint, GLsizei, const GLint*);
    void (*glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
    void (*glUniform3fv)(GLint, GLsizei, const GLfloat*);
    void (*glUniform3i)(GLint, GLint, GLint, GLint);
    void (*glUniform3iv)(GLint, GLsizei, const GLint*);
    void (*glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*glUniform4fv)(GLint, GLsizei, const GLfloat*);
    void (*glUniform4i)(GLint, GLint, GLint, GLint, GLint);
    void (*glUniform4iv)(GLint, GLsizei, const GLint*);
    void (*glUniform1ui)(GLint, GLuint);
    void (*glUniform2ui)(GLint, GLuint, GLuint);
    void (*glUniform3ui)(GLint, GLuint, GLuint, GLuint);
    void (*glUniform4ui)(GLint, GLuint, GLuint, GLuint, GLuint);
    void (*glUniform1uiv)(GLint, GLsizei, const GLuint*);
    void (*glUniform2uiv)(GLint, GLsizei, const GLuint*);
    void (*glUniform3uiv)(GLint, GLsizei, const GLuint*);
    void (*glUniform4uiv)(GLint, GLsizei, const GLuint*);
    void (*glUniformMatrix2fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*glUniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*glUniformMatrix2x3fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*glUniformMatrix3x2fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*glUniformMatrix2x4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*glUniformMatrix4x2fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*glUniformMatrix3x4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*glUniformMatrix4x3fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*glGetUniformfv)(GLuint, GLint, GLfloat*);
    void (*glGetUniformiv)(GLuint, GLint, GLint*);
    void (*glGetUniformuiv)(GLuint, GLint, GLuint*);
    void (*glGenVertexArrays)(GLsizei, GLuint*);
    void (*glDeleteVertexArrays)(GLsizei, const GLuint*);
    GLboolean (*glIsVertexArray)(GLuint);
    void (*glBindVertexArray)(GLuint);
    void (*glEnableVertexAttribArray)(GLuint);
    void (*glDisableVertexAttribArray)(GLuint);
    void (*glGetVertexAttribfv)(GLuint, GLenum, GLfloat *);
    void (*glGetVertexAttribiv)(GLuint, GLenum, GLint *);
    void (*glGetVertexAttribPointerv)(GLuint, GLenum, GLvoid **);
    void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid*);
    void (*glVertexAttribIPointer)(GLuint, GLint, GLenum, GLsizei, const GLvoid*);
    void (*glGetVertexAttribIiv)(GLuint, GLenum, GLint *);
    void (*glGetVertexAttribIuiv)(GLuint, GLenum, GLuint *);
    void (*glVertexAttribI1i)(GLuint, GLint);
    void (*glVertexAttribI2i)(GLuint, GLint, GLint);
    void (*glVertexAttribI3i)(GLuint, GLint, GLint, GLint);
    void (*glVertexAttribI4i)(GLuint, GLint, GLint, GLint, GLint);
    void (*glVertexAttribI1ui)(GLuint, GLuint);
    void (*glVertexAttribI2ui)(GLuint, GLuint, GLuint);
    void (*glVertexAttribI3ui)(GLuint, GLuint, GLuint, GLuint);
    void (*glVertexAttribI4ui)(GLuint, GLuint, GLuint, GLuint, GLuint);
    void (*glVertexAttribI1iv)(GLuint, const GLint *);
    void (*glVertexAttribI2iv)(GLuint, const GLint *);
    void (*glVertexAttribI3iv)(GLuint, const GLint *);
    void (*glVertexAttribI4iv)(GLuint, const GLint *);
    void (*glVertexAttribI1uiv)(GLuint, const GLuint *);
    void (*glVertexAttribI2uiv)(GLuint, const GLuint *);
    void (*glVertexAttribI3uiv)(GLuint, const GLuint *);
    void (*glVertexAttribI4uiv)(GLuint, const GLuint *);
    void (*glVertexAttribDivisor)(GLuint, GLuint);
    void (*glGenFramebuffers)(GLsizei, GLuint*);
    void (*glDeleteFramebuffers)(GLsizei, const GLuint*);
    GLboolean (*glIsFramebuffer)(GLuint);
    void (*glGenRenderbuffers)(GLsizei, GLuint*);
    void (*glDeleteRenderbuffers)(GLsizei, const GLuint*);
    GLboolean (*glIsRenderbuffer)(GLuint);
    void (*glBindFramebuffer)(GLenum, GLuint);
    void (*glBindRenderbuffer)(GLenum, GLuint);
    GLenum (*glCheckFramebufferStatus)(GLenum);
    void (*glFramebufferTexture)(GLenum, GLenum, GLuint, GLint);
    void (*glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    void (*glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
    void (*glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
    void (*glRenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
    void (*glGetRenderbufferParameteriv)(GLenum, GLenum, GLint *);
    void (*glGetFramebufferAttachmentParameteriv)(GLenum, GLenum, GLenum, GLint *);
    void (*glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    void (*glDrawBuffer)(GLenum);
    void (*glDrawBuffers)(GLsizei, const GLenum*);
    void (*glReadBuffer)(GLenum);
    void (*glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*);
    void (*glFlush)(void);
    void (*glFinish)(void);
    void (*glGenQueries)(GLsizei, GLuint*);
    void (*glDeleteQueries)(GLsizei, const GLuint*);
    GLboolean (*glIsQuery)(GLuint);
    void (*glBeginQuery)(GLenum, GLuint);
    void (*glEndQuery)(GLenum);
    void (*glGetQueryiv)(GLenum, GLenum, GLint*);
    void (*glGetQueryObjectiv)(GLuint, GLenum, GLint*);
    void (*glGetQueryObjectuiv)(GLuint, GLenum, GLuint*);
    void (*glBeginConditionalRender)(GLuint, GLenum);
    void (*glEndConditionalRender)(void);
    void (*glBeginQueryIndexed)(GLenum, GLuint, GLuint);
    void (*glEndQueryIndexed)(GLenum, GLuint);
    void (*glGetQueryIndexediv)(GLenum, GLuint, GLenum, GLint*);
    void (*glGenTransformFeedbacks)(GLsizei, GLuint*);
    void (*glDeleteTransformFeedbacks)(GLsizei, const GLuint*);
    void (*glGetBooleanv)(GLenum, GLboolean *);
    void (*glGetDoublev)(GLenum, GLdouble *);
    void (*glLogicOp)(GLenum);
    void (*glPointSize)(GLfloat);
    void (*glFlushMappedBufferRange)(GLenum, GLintptr, GLsizeiptr);
    void (*glTexImage1D)(GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
    void (*glTexSubImage1D)(GLenum, GLint, GLint, GLsizei, GLenum, GLenum, const GLvoid*);
    void (*glCopyTexImage1D)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLint);
    void (*glCopyTexSubImage1D)(GLenum, GLint, GLint, GLint, GLint, GLsizei);
    void (*glCopyTexSubImage3D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
    void (*glCompressedTexImage1D)(GLenum, GLint, GLenum, GLsizei, GLint, GLsizei, const GLvoid*);
    void (*glCompressedTexImage3D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei, const GLvoid*);
    void (*glCompressedTexSubImage1D)(GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const GLvoid*);
    void (*glCompressedTexSubImage3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid*);
    void (*glGetTexLevelParameteriv)(GLenum, GLint, GLenum, GLint*);
    void (*glGetTexLevelParameterfv)(GLenum, GLint, GLenum, GLfloat*);
    void (*glGetActiveUniformBlockiv)(GLuint, GLuint, GLenum, GLint*);
    void (*glGetActiveUniformBlockName)(GLuint, GLuint, GLsizei, GLsizei*, GLchar*);
    void (*glGetActiveUniformsiv)(GLuint, GLsizei, const GLuint*, GLenum, GLint*);
    void (*glGetActiveUniformName)(GLuint, GLuint, GLsizei, GLsizei*, GLchar*);
    void (*glBlendFunc)(GLenum, GLenum);
    void (*glBlendEquation)(GLenum);
    void (*glBlendEquationSeparate)(GLenum, GLenum);
    void (*glBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
    void (*glBlendColor)(GLclampf, GLclampf, GLclampf, GLclampf);
    void (*glDepthFunc)(GLenum);
    void (*glDepthMask)(GLboolean);
    void (*glDepthRange)(GLclampd, GLclampd);
    void (*glStencilFunc)(GLenum, GLint, GLuint);
    void (*glStencilFuncSeparate)(GLenum, GLenum, GLint, GLuint);
    void (*glStencilOp)(GLenum, GLenum, GLenum);
    void (*glStencilOpSeparate)(GLenum, GLenum, GLenum, GLenum);
    void (*glStencilMask)(GLuint);
    void (*glStencilMaskSeparate)(GLenum, GLuint);
    void (*glCullFace)(GLenum);
    void (*glFrontFace)(GLenum);
    void (*glPolygonMode)(GLenum, GLenum);
    void (*glPolygonOffset)(GLfloat, GLfloat);
    void (*glViewport)(GLint, GLint, GLsizei, GLsizei);
    void (*glScissor)(GLint, GLint, GLsizei, GLsizei);
    void (*glColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void (*glLineWidth)(GLfloat);
    void (*glPixelStorei)(GLenum, GLint);
    void (*glPrimitiveRestartIndex)(GLuint);
} gl_dispatch_t;

#define GL_DIRTY_BLEND         (1 << 0)
#define GL_DIRTY_DEPTH_STENCIL (1 << 1)
#define GL_DIRTY_CULL          (1 << 2)
#define GL_DIRTY_SCISSOR       (1 << 3)
#define GL_DIRTY_VIEWPORT      (1 << 4)
#define GL_DIRTY_COLOR_MASK    (1 << 5)
#define GL_DIRTY_LINE_WIDTH    (1 << 6)
#define GL_DIRTY_FRONT_FACE    (1 << 7)
#define GL_DIRTY_POLYGON_MODE  (1 << 8)
#define GL_DIRTY_VAO           (1 << 9)
#define GL_DIRTY_PROGRAM       (1 << 10)
#define GL_DIRTY_TEXTURE_BINDINGS (1 << 11)
#define GL_DIRTY_FRAMEBUFFER      (1 << 12)
#define GL_DIRTY_UNIFORM_BINDINGS (1 << 13)
#define GL_DIRTY_LOGIC_OP       (1 << 14)
#define GL_DIRTY_POINT_SIZE     (1 << 15)
#define GL_DIRTY_PRIMITIVE_RESTART (1 << 16)
#define GL_DIRTY_MULTISAMPLE    (1 << 17)
#define GL_DIRTY_PROVOKING_VERTEX (1 << 18)
#define GL_DIRTY_RASTERIZER_DISCARD (1 << 19)

#define GL_ERROR_QUEUE_SIZE 8
#define GL33_MAX_VERTEX_ATTRIBS 16
#define GL33_MAX_TEXTURE_IMAGE_UNITS 16
#define GL33_MAX_COMBINED_TEXTURE_IMAGE_UNITS 48
#define GL33_MAX_UNIFORM_BUFFER_BINDINGS 36
#define GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS 4
#define GL33_MAX_SAMPLES 8
#define GL33_MAX_SAMPLE_MASK_WORDS 1
#define GL33_MAX_TEXTURE_UNITS GL33_MAX_COMBINED_TEXTURE_IMAGE_UNITS

typedef struct {
    GLuint buffer;
    GLintptr offset;
    GLsizeiptr size;
    GLboolean whole_buffer;
} gl_uniform_buffer_binding_t;

typedef struct {
    GLuint bound_array_buffer;
    GLuint bound_element_array_buffer;
    GLuint bound_uniform_buffer;
    GLuint bound_copy_read_buffer;
    GLuint bound_copy_write_buffer;
    GLuint bound_pixel_pack_buffer;
    GLuint bound_pixel_unpack_buffer;
    GLuint bound_texture_buffer;
    GLuint bound_transform_feedback_buffer;
    GLuint bound_transform_feedback;
    gl_uniform_buffer_binding_t uniform_buffer_bindings[GL33_MAX_UNIFORM_BUFFER_BINDINGS];
    gl_uniform_buffer_binding_t transform_feedback_buffer_bindings[GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS];
    GLuint active_texture;
    GLuint bound_texture_1d[GL33_MAX_TEXTURE_UNITS];
    GLuint bound_texture_2d[GL33_MAX_TEXTURE_UNITS];
    GLuint bound_texture_3d[GL33_MAX_TEXTURE_UNITS];
    GLuint bound_texture_cube[GL33_MAX_TEXTURE_UNITS];
    GLuint bound_texture_1d_array[GL33_MAX_TEXTURE_UNITS];
    GLuint bound_texture_2d_array[GL33_MAX_TEXTURE_UNITS];
    GLuint bound_texture_2d_multisample[GL33_MAX_TEXTURE_UNITS];
    GLuint bound_texture_2d_multisample_array[GL33_MAX_TEXTURE_UNITS];
    GLuint bound_sampler[GL33_MAX_TEXTURE_UNITS];
    GLuint bound_renderbuffer;
    GLuint bound_framebuffer;
    GLuint bound_read_framebuffer;
    GLuint bound_program;
    GLuint bound_vao;
    struct { GLint x, y; GLsizei width, height; GLfloat near_z, far_z; } viewport;
    struct { GLint x, y; GLsizei width, height; } scissor;
    GLint pack_alignment, pack_row_length, pack_skip_rows, pack_skip_pixels, pack_image_height, pack_skip_images;
    GLint unpack_alignment, unpack_row_length, unpack_skip_rows, unpack_skip_pixels, unpack_image_height, unpack_skip_images;
    GLenum blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha, blend_eq_rgb, blend_eq_alpha;
    GLfloat blend_color[4];
    GLenum depth_func;
    GLboolean depth_mask;
    GLuint stencil_compare_mask[2], stencil_write_mask[2];
    GLenum stencil_func[2], stencil_fail[2], stencil_zfail[2], stencil_zpass[2];
    GLint stencil_ref[2];
    GLenum cull_face_mode, front_face, polygon_mode, logic_op;
    GLenum provoking_vertex;
    GLenum clamp_read_color;
    GLfloat polygon_offset_factor, polygon_offset_units, line_width, point_size;
    GLboolean color_mask[4];
    GLfloat current_vertex_attrib[GL33_MAX_VERTEX_ATTRIBS][4];
    GLfloat clear_color[4], clear_depth;
    GLint clear_stencil;
    GLboolean depth_test_enabled, stencil_test_enabled, blend_enabled, cull_face_enabled, scissor_test_enabled, sample_coverage_enabled;
    GLboolean multisample_enabled, sample_alpha_to_coverage_enabled;
    GLboolean sample_alpha_to_one_enabled, sample_mask_enabled;
    GLboolean color_logic_op_enabled;
    GLboolean primitive_restart_enabled, rasterizer_discard_enabled;
    GLboolean polygon_offset_point_enabled, polygon_offset_line_enabled, polygon_offset_fill_enabled, sample_coverage_invert;
    GLfloat sample_coverage_value;
    GLuint sample_mask_value;
    GLuint primitive_restart_index;
    GLuint active_query_samples_passed;
    GLuint active_query_any_samples_passed;
    GLuint active_query_any_samples_passed_conservative;
    GLuint active_query_time_elapsed;
    GLuint active_query_primitives_generated;
    GLuint active_query_transform_feedback_primitives_written;
    GLuint conditional_render_query;
    GLenum conditional_render_mode;
    GLboolean conditional_render_active;
    GLenum generate_mipmap_hint, error;
    GLenum error_queue[GL_ERROR_QUEUE_SIZE];
    uint32_t error_head, error_tail;
    OSMutex error_mutex;
    uint32_t dirty_flags;
    gl_dispatch_t dispatch;
} gl_context_t;

extern gl_context_t *g_gl_context;
gl_context_t* gl_context_create(void);
void gl_context_destroy(gl_context_t *ctx);
void _gl_Flush(void);
void _gl_Finish(void);
void _gl_set_error(GLenum error);
const GLubyte* _gl_GetString(GLenum name);
const GLubyte* _gl_GetStringi(GLenum name, GLuint index);
void _gl_GetBooleanv(GLenum pname, GLboolean *params);
void _gl_GetDoublev(GLenum pname, GLdouble *params);
void _gl_GetIntegerv(GLenum pname, GLint *params);
void _gl_GetInteger64v(GLenum pname, GLint64 *params);
void _gl_GetFloatv(GLenum pname, GLfloat *params);
void _gl_GenQueries(GLsizei n, GLuint *ids);
void _gl_DeleteQueries(GLsizei n, const GLuint *ids);
GLboolean _gl_IsQuery(GLuint id);
void _gl_BeginQuery(GLenum target, GLuint id);
void _gl_EndQuery(GLenum target);
void _gl_GetQueryiv(GLenum target, GLenum pname, GLint *params);
void _gl_GetQueryObjectiv(GLuint id, GLenum pname, GLint *params);
void _gl_GetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params);
void _gl_BeginConditionalRender(GLuint id, GLenum mode);
void _gl_EndConditionalRender(void);
void _gl_BeginQueryIndexed(GLenum target, GLuint index, GLuint id);
void _gl_EndQueryIndexed(GLenum target, GLuint index);
void _gl_GetQueryIndexediv(GLenum target, GLuint index, GLenum pname, GLint *params);
void _gl_GenTransformFeedbacks(GLsizei n, GLuint *ids);
void _gl_DeleteTransformFeedbacks(GLsizei n, const GLuint *ids);
gl_uniform_buffer_binding_t *gl_transform_feedback_current_buffer_binding(GLuint index);
GLboolean gl_transform_feedback_current_active(void);
GLboolean gl_transform_feedback_current_active_unpaused(void);
GLboolean gl_transform_feedback_current_active_paused(void);
GLboolean gl_transform_feedback_program_active(GLuint program);
void gl_transform_feedback_unbind_buffer(GLuint buffer);
GLboolean gl_transform_feedback_validate_draw_mode(GLenum mode);

#ifdef __cplusplus
}
#endif

#endif
