#ifndef GL33_VAO_H
#define GL33_VAO_H

#include "core/gl_context.h"
#include <gx2/shaders.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    GLuint buffer;
    GLint size;
    GLenum type;
    GLboolean normalized;
    GLboolean enabled;
    GLboolean integer_input;
    GLsizei stride;
    const GLvoid *pointer;
    GLuint divisor;
} gl_vao_attrib_state_t;

void gl_vao_init(void);
void gl_vao_shutdown(void);

void _gl_GenVertexArrays(GLsizei n, GLuint *arrays);
void _gl_DeleteVertexArrays(GLsizei n, const GLuint *arrays);
GLboolean _gl_IsVertexArray(GLuint array);
void _gl_BindVertexArray(GLuint array);
void _gl_EnableVertexAttribArray(GLuint index);
void _gl_DisableVertexAttribArray(GLuint index);
void _gl_GetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params);
void _gl_GetVertexAttribiv(GLuint index, GLenum pname, GLint *params);
void _gl_GetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid **pointer);
void _gl_VertexAttrib1f(GLuint index, GLfloat x);
void _gl_VertexAttrib1fv(GLuint index, const GLfloat *v);
void _gl_VertexAttrib2f(GLuint index, GLfloat x, GLfloat y);
void _gl_VertexAttrib2fv(GLuint index, const GLfloat *v);
void _gl_VertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z);
void _gl_VertexAttrib3fv(GLuint index, const GLfloat *v);
void _gl_VertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                        GLfloat w);
void _gl_VertexAttrib4fv(GLuint index, const GLfloat *v);
void _gl_VertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
void _gl_VertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void _gl_GetVertexAttribIiv(GLuint index, GLenum pname, GLint *params);
void _gl_GetVertexAttribIuiv(GLuint index, GLenum pname, GLuint *params);
void _gl_VertexAttribI1i(GLuint index, GLint x);
void _gl_VertexAttribI2i(GLuint index, GLint x, GLint y);
void _gl_VertexAttribI3i(GLuint index, GLint x, GLint y, GLint z);
void _gl_VertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w);
void _gl_VertexAttribI1ui(GLuint index, GLuint x);
void _gl_VertexAttribI2ui(GLuint index, GLuint x, GLuint y);
void _gl_VertexAttribI3ui(GLuint index, GLuint x, GLuint y, GLuint z);
void _gl_VertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w);
void _gl_VertexAttribI1iv(GLuint index, const GLint *v);
void _gl_VertexAttribI2iv(GLuint index, const GLint *v);
void _gl_VertexAttribI3iv(GLuint index, const GLint *v);
void _gl_VertexAttribI4iv(GLuint index, const GLint *v);
void _gl_VertexAttribI1uiv(GLuint index, const GLuint *v);
void _gl_VertexAttribI2uiv(GLuint index, const GLuint *v);
void _gl_VertexAttribI3uiv(GLuint index, const GLuint *v);
void _gl_VertexAttribI4uiv(GLuint index, const GLuint *v);

void _gl_VertexAttribDivisor(GLuint index, GLuint divisor);

void gl_vao_set_element_array_buffer(GLuint buffer);
GLuint gl_vao_get_bound_vertex_array(void);
GLuint gl_vao_get_element_array_buffer(void);
void gl_vao_unbind_buffer(GLuint buffer);
GLboolean gl_vao_has_bound_array(void);
GLboolean gl_vao_get_attrib_state(GLuint index, gl_vao_attrib_state_t *state);

void gl_bind_vao(void);

#ifdef __cplusplus
}
#endif

#endif
