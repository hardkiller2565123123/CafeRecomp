#ifndef GL33_BUFFER_H
#define GL33_BUFFER_H

#include "core/gl_context.h"
#include <gx2/clear.h>
#include <gx2/mem.h>
#include <gx2r/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif
void gl_buffer_init(void);
void gl_buffer_shutdown(void);


void gl_buffer_end_frame(void);


void _gl_GenBuffers(GLsizei n, GLuint *buffers);
void _gl_DeleteBuffers(GLsizei n, const GLuint *buffers);
GLboolean _gl_IsBuffer(GLuint buffer);
void _gl_BindBuffer(GLenum target, GLuint buffer);
void _gl_BindBufferBase(GLenum target, GLuint index, GLuint buffer);
void _gl_BindBufferRange(GLenum target, GLuint index, GLuint buffer,
                         GLintptr offset, GLsizeiptr size);
void _gl_BufferData(GLenum target, GLsizeiptr size, const GLvoid *data,
                    GLenum usage);
void _gl_BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                       const GLvoid *data);
void _gl_GetBufferParameteriv(GLenum target, GLenum pname, GLint *params);
void _gl_GetBufferPointerv(GLenum target, GLenum pname, GLvoid **params);
void *_gl_MapBuffer(GLenum target, GLenum access);
void *_gl_MapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                         GLbitfield access);
GLboolean _gl_UnmapBuffer(GLenum target);
void _gl_FlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length);

void gl_buffer_copy_sub_data(GLenum readTarget, GLenum writeTarget,
                             GLintptr readOffset, GLintptr writeOffset,
                             GLsizeiptr size);
void gl_buffer_get_sub_data(GLenum target, GLintptr offset, GLsizeiptr size,
                            GLvoid *data);
GLboolean gl_buffer_is_mapped(GLuint buffer);
void *gl_buffer_get_data(GLuint buffer);
GLboolean gl_buffer_get_read_range(GLuint buffer, GLintptr offset,
                                   GLsizeiptr size, const GLvoid **data);
GLboolean gl_buffer_get_write_range(GLuint buffer, GLintptr offset,
                                    GLsizeiptr size, GLvoid **data);
GLboolean gl_buffer_flush_range(GLuint buffer, GLintptr offset,
                                GLsizeiptr size);
void *gl_buffer_get_uniform_block_data(GLuint buffer, GLintptr offset,
                                       GLsizeiptr size);
GLsizeiptr gl_buffer_get_size(GLuint buffer);
GX2RBuffer *gl_buffer_get_gx2r_buffer(GLuint buffer);

#ifdef __cplusplus
}
#endif

#endif
