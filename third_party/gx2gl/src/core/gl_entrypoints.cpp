#include "gl_context.h"
#include "gl_texture.h"
#include "gl_shader.h"
#include "gl_draw.h"
#include "gl_vao.h"
#include "state/gl_state.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void glGenBuffers(GLsizei n, GLuint *b) { if(g_gl_context) g_gl_context->dispatch.glGenBuffers(n, b); }
void glDeleteBuffers(GLsizei n, const GLuint *b) { if(g_gl_context) g_gl_context->dispatch.glDeleteBuffers(n, b); }
GLboolean glIsBuffer(GLuint b) { return g_gl_context ? g_gl_context->dispatch.glIsBuffer(b) : GL_FALSE; }
void glBindBuffer(GLenum t, GLuint b) { if(g_gl_context) g_gl_context->dispatch.glBindBuffer(t, b); }
void glBindBufferBase(GLenum t, GLuint i, GLuint b) { if(g_gl_context) g_gl_context->dispatch.glBindBufferBase(t, i, b); }
void glBindBufferRange(GLenum t, GLuint i, GLuint b, GLintptr o, GLsizeiptr s) { if(g_gl_context) g_gl_context->dispatch.glBindBufferRange(t, i, b, o, s); }
void glBufferData(GLenum t, GLsizeiptr s, const GLvoid *d, GLenum u) { if(g_gl_context) g_gl_context->dispatch.glBufferData(t, s, d, u); }
void glBufferSubData(GLenum t, GLintptr o, GLsizeiptr s, const GLvoid *d) { if(g_gl_context) g_gl_context->dispatch.glBufferSubData(t, o, s, d); }
void glGetBufferParameteriv(GLenum t, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetBufferParameteriv(t, p, v); }
void glGetBufferPointerv(GLenum t, GLenum p, GLvoid **v) { if(g_gl_context) g_gl_context->dispatch.glGetBufferPointerv(t, p, v); }
void* glMapBuffer(GLenum t, GLenum a) { return g_gl_context ? g_gl_context->dispatch.glMapBuffer(t, a) : NULL; }
void* glMapBufferRange(GLenum t, GLintptr o, GLsizeiptr l, GLbitfield a) { return g_gl_context ? g_gl_context->dispatch.glMapBufferRange(t, o, l, a) : NULL; }
GLboolean glUnmapBuffer(GLenum t) { return g_gl_context ? g_gl_context->dispatch.glUnmapBuffer(t) : GL_FALSE; }
void glEnable(GLenum c) { if(g_gl_context) g_gl_context->dispatch.glEnable(c); }
void glDisable(GLenum c) { if(g_gl_context) g_gl_context->dispatch.glDisable(c); }
GLboolean glIsEnabled(GLenum c) { return g_gl_context ? g_gl_context->dispatch.glIsEnabled(c) : GL_FALSE; }
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { if(g_gl_context) g_gl_context->dispatch.glClearColor(r, g, b, a); }
void glClearDepth(GLclampd d) { if(g_gl_context) g_gl_context->dispatch.glClearDepth(d); }
void glClearDepthf(GLclampf d) { glClearDepth((GLclampd)d); }
void glClearStencil(GLint s) { if(g_gl_context) g_gl_context->dispatch.glClearStencil(s); }
void glClear(GLbitfield m) { if(g_gl_context) g_gl_context->dispatch.glClear(m); }
void glGenTextures(GLsizei n, GLuint *t) { if(g_gl_context) g_gl_context->dispatch.glGenTextures(n, t); }
void glDeleteTextures(GLsizei n, const GLuint *t) { if(g_gl_context) g_gl_context->dispatch.glDeleteTextures(n, t); }
GLboolean glIsTexture(GLuint t) { return g_gl_context ? g_gl_context->dispatch.glIsTexture(t) : GL_FALSE; }
void glGenSamplers(GLsizei n, GLuint *s) { if(g_gl_context) g_gl_context->dispatch.glGenSamplers(n, s); }
void glDeleteSamplers(GLsizei n, const GLuint *s) { if(g_gl_context) g_gl_context->dispatch.glDeleteSamplers(n, s); }
GLboolean glIsSampler(GLuint s) { return g_gl_context ? g_gl_context->dispatch.glIsSampler(s) : GL_FALSE; }
void glBindTexture(GLenum t, GLuint i) { if(g_gl_context) g_gl_context->dispatch.glBindTexture(t, i); }
void glBindSampler(GLuint u, GLuint s) { if(g_gl_context) g_gl_context->dispatch.glBindSampler(u, s); }
void glActiveTexture(GLenum t) { if(g_gl_context) g_gl_context->dispatch.glActiveTexture(t); }
void glTexImage2D(GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum p, const GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glTexImage2D(t, l, i, w, h, b, f, p, px); }
void glTexImage3D(GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLsizei d, GLint b, GLenum f, GLenum p, const GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glTexImage3D(t, l, i, w, h, d, b, f, p, px); }
void glTexSubImage2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum p, const GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glTexSubImage2D(t, l, x, y, w, h, f, p, px); }
void glTexSubImage3D(GLenum t, GLint l, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum f, GLenum p, const GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glTexSubImage3D(t, l, x, y, z, w, h, d, f, p, px); }
void glTexParameteri(GLenum t, GLenum p, GLint v) { if(g_gl_context) g_gl_context->dispatch.glTexParameteri(t, p, v); }
void glTexParameterf(GLenum t, GLenum p, GLfloat v) { if(g_gl_context) g_gl_context->dispatch.glTexParameterf(t, p, v); }
void glTexParameteriv(GLenum t, GLenum p, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glTexParameteriv(t, p, v); }
void glTexParameterfv(GLenum t, GLenum p, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glTexParameterfv(t, p, v); }
void glGetTexParameteriv(GLenum t, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetTexParameteriv(t, p, v); }
void glGetTexParameterfv(GLenum t, GLenum p, GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glGetTexParameterfv(t, p, v); }
void glSamplerParameteriv(GLuint s, GLenum p, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glSamplerParameteriv(s, p, v); }
void glSamplerParameterfv(GLuint s, GLenum p, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glSamplerParameterfv(s, p, v); }
void glSamplerParameteri(GLuint s, GLenum p, GLint v) { if(g_gl_context) g_gl_context->dispatch.glSamplerParameteri(s, p, v); }
void glSamplerParameterf(GLuint s, GLenum p, GLfloat v) { if(g_gl_context) g_gl_context->dispatch.glSamplerParameterf(s, p, v); }
void glGetSamplerParameteriv(GLuint s, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetSamplerParameteriv(s, p, v); }
void glGetSamplerParameterfv(GLuint s, GLenum p, GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glGetSamplerParameterfv(s, p, v); }
void glGenerateMipmap(GLenum t) { if(g_gl_context) g_gl_context->dispatch.glGenerateMipmap(t); }
GLuint glCreateShader(GLenum t) { return g_gl_context ? g_gl_context->dispatch.glCreateShader(t) : 0; }
void glDeleteShader(GLuint s) { if(g_gl_context) g_gl_context->dispatch.glDeleteShader(s); }
GLboolean glIsShader(GLuint s) { return g_gl_context ? g_gl_context->dispatch.glIsShader(s) : GL_FALSE; }
void glShaderSource(GLuint s, GLsizei c, const GLchar *const *str, const GLint *l) { if(g_gl_context) g_gl_context->dispatch.glShaderSource(s, c, str, l); }
void glGetShaderSource(GLuint s, GLsizei b, GLsizei *l, GLchar *src) { if(g_gl_context) g_gl_context->dispatch.glGetShaderSource(s, b, l, src); }
void glCompileShader(GLuint s) { if(g_gl_context) g_gl_context->dispatch.glCompileShader(s); }
GLuint glCreateProgram(void) { return g_gl_context ? g_gl_context->dispatch.glCreateProgram() : 0; }
void glDeleteProgram(GLuint p) { if(g_gl_context) g_gl_context->dispatch.glDeleteProgram(p); }
GLboolean glIsProgram(GLuint p) { return g_gl_context ? g_gl_context->dispatch.glIsProgram(p) : GL_FALSE; }
void glAttachShader(GLuint p, GLuint s) { if(g_gl_context) g_gl_context->dispatch.glAttachShader(p, s); }
void glDetachShader(GLuint p, GLuint s) { if(g_gl_context) g_gl_context->dispatch.glDetachShader(p, s); }
void glLinkProgram(GLuint p) { if(g_gl_context) g_gl_context->dispatch.glLinkProgram(p); }
void glValidateProgram(GLuint p) { if(g_gl_context) g_gl_context->dispatch.glValidateProgram(p); }
void glUseProgram(GLuint p) { if(g_gl_context) g_gl_context->dispatch.glUseProgram(p); }
void glGetShaderiv(GLuint s, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetShaderiv(s, p, v); }
void glGetProgramiv(GLuint p, GLenum n, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetProgramiv(p, n, v); }
void glGetShaderInfoLog(GLuint s, GLsizei m, GLsizei *l, GLchar *i) { if(g_gl_context) g_gl_context->dispatch.glGetShaderInfoLog(s, m, l, i); }
void glGetProgramInfoLog(GLuint p, GLsizei m, GLsizei *l, GLchar *i) { if(g_gl_context) g_gl_context->dispatch.glGetProgramInfoLog(p, m, l, i); }
void glBindAttribLocation(GLuint p, GLuint i, const GLchar *n) { if(g_gl_context) g_gl_context->dispatch.glBindAttribLocation(p, i, n); }
void glGetAttachedShaders(GLuint p, GLsizei m, GLsizei *c, GLuint *s) { if(g_gl_context) g_gl_context->dispatch.glGetAttachedShaders(p, m, c, s); }
void glGetActiveAttrib(GLuint p, GLuint i, GLsizei b, GLsizei *l, GLint *s, GLenum *t, GLchar *n) { if(g_gl_context) g_gl_context->dispatch.glGetActiveAttrib(p, i, b, l, s, t, n); }
void glGetActiveUniform(GLuint p, GLuint i, GLsizei b, GLsizei *l, GLint *s, GLenum *t, GLchar *n) { if(g_gl_context) g_gl_context->dispatch.glGetActiveUniform(p, i, b, l, s, t, n); }
GLint glGetUniformLocation(GLuint p, const GLchar *n) { return g_gl_context ? g_gl_context->dispatch.glGetUniformLocation(p, n) : -1; }
GLint glGetAttribLocation(GLuint p, const GLchar *n) { return g_gl_context ? g_gl_context->dispatch.glGetAttribLocation(p, n) : -1; }
GLuint glGetUniformBlockIndex(GLuint p, const GLchar *n) { return g_gl_context ? g_gl_context->dispatch.glGetUniformBlockIndex(p, n) : GL_INVALID_INDEX; }
void glUniformBlockBinding(GLuint p, GLuint i, GLuint b) { if(g_gl_context) g_gl_context->dispatch.glUniformBlockBinding(p, i, b); }
void glUniform1f(GLint l, GLfloat v) { if(g_gl_context) g_gl_context->dispatch.glUniform1f(l, v); }
void glUniform1fv(GLint l, GLsizei c, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniform1fv(l, c, v); }
void glUniform1i(GLint l, GLint v) { if(g_gl_context) g_gl_context->dispatch.glUniform1i(l, v); }
void glUniform1iv(GLint l, GLsizei c, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform1iv(l, c, v); }
void glUniform2f(GLint l, GLfloat v0, GLfloat v1) { if(g_gl_context) g_gl_context->dispatch.glUniform2f(l, v0, v1); }
void glUniform2fv(GLint l, GLsizei c, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniform2fv(l, c, v); }
void glUniform2i(GLint l, GLint v0, GLint v1) { if(g_gl_context) g_gl_context->dispatch.glUniform2i(l, v0, v1); }
void glUniform2iv(GLint l, GLsizei c, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform2iv(l, c, v); }
void glUniform3f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2) { if(g_gl_context) g_gl_context->dispatch.glUniform3f(l, v0, v1, v2); }
void glUniform3fv(GLint l, GLsizei c, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniform3fv(l, c, v); }
void glUniform3i(GLint l, GLint v0, GLint v1, GLint v2) { if(g_gl_context) g_gl_context->dispatch.glUniform3i(l, v0, v1, v2); }
void glUniform3iv(GLint l, GLsizei c, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform3iv(l, c, v); }
void glUniform4f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { if(g_gl_context) g_gl_context->dispatch.glUniform4f(l, v0, v1, v2, v3); }
void glUniform4fv(GLint l, GLsizei c, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniform4fv(l, c, v); }
void glUniform4i(GLint l, GLint v0, GLint v1, GLint v2, GLint v3) { if(g_gl_context) g_gl_context->dispatch.glUniform4i(l, v0, v1, v2, v3); }
void glUniform4iv(GLint l, GLsizei c, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform4iv(l, c, v); }
void glUniform1ui(GLint l, GLuint v) { if(g_gl_context) g_gl_context->dispatch.glUniform1ui(l, v); }
void glUniform2ui(GLint l, GLuint v0, GLuint v1) { if(g_gl_context) g_gl_context->dispatch.glUniform2ui(l, v0, v1); }
void glUniform3ui(GLint l, GLuint v0, GLuint v1, GLuint v2) { if(g_gl_context) g_gl_context->dispatch.glUniform3ui(l, v0, v1, v2); }
void glUniform4ui(GLint l, GLuint v0, GLuint v1, GLuint v2, GLuint v3) { if(g_gl_context) g_gl_context->dispatch.glUniform4ui(l, v0, v1, v2, v3); }
void glUniform1uiv(GLint l, GLsizei c, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform1uiv(l, c, v); }
void glUniform2uiv(GLint l, GLsizei c, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform2uiv(l, c, v); }
void glUniform3uiv(GLint l, GLsizei c, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform3uiv(l, c, v); }
void glUniform4uiv(GLint l, GLsizei c, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glUniform4uiv(l, c, v); }
void glUniformMatrix2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix2fv(l, c, t, v); }
void glUniformMatrix3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix3fv(l, c, t, v); }
void glUniformMatrix4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix4fv(l, c, t, v); }
void glUniformMatrix2x3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix2x3fv(l, c, t, v); }
void glUniformMatrix3x2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix3x2fv(l, c, t, v); }
void glUniformMatrix2x4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix2x4fv(l, c, t, v); }
void glUniformMatrix4x2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix4x2fv(l, c, t, v); }
void glUniformMatrix3x4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix3x4fv(l, c, t, v); }
void glUniformMatrix4x3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glUniformMatrix4x3fv(l, c, t, v); }
void glGetUniformfv(GLuint p, GLint l, GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glGetUniformfv(p, l, v); }
void glGetUniformiv(GLuint p, GLint l, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetUniformiv(p, l, v); }
void glGetUniformuiv(GLuint p, GLint l, GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glGetUniformuiv(p, l, v); }
void glGenVertexArrays(GLsizei n, GLuint *a) { if(g_gl_context) g_gl_context->dispatch.glGenVertexArrays(n, a); }
void glDeleteVertexArrays(GLsizei n, const GLuint *a) { if(g_gl_context) g_gl_context->dispatch.glDeleteVertexArrays(n, a); }
GLboolean glIsVertexArray(GLuint a) { return g_gl_context ? g_gl_context->dispatch.glIsVertexArray(a) : GL_FALSE; }
void glBindVertexArray(GLuint a) { if(g_gl_context) g_gl_context->dispatch.glBindVertexArray(a); }
void glEnableVertexAttribArray(GLuint i) { if(g_gl_context) g_gl_context->dispatch.glEnableVertexAttribArray(i); }
void glDisableVertexAttribArray(GLuint i) { if(g_gl_context) g_gl_context->dispatch.glDisableVertexAttribArray(i); }
void glGetVertexAttribiv(GLuint i, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetVertexAttribiv(i, p, v); }
void glGetVertexAttribfv(GLuint i, GLenum p, GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glGetVertexAttribfv(i, p, v); }
void glGetVertexAttribPointerv(GLuint i, GLenum p, GLvoid **v) { if(g_gl_context) g_gl_context->dispatch.glGetVertexAttribPointerv(i, p, v); }
void glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei r, const GLvoid *p) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribPointer(i, s, t, n, r, p); }
void glVertexAttribIPointer(GLuint i, GLint s, GLenum t, GLsizei r, const GLvoid *p) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribIPointer(i, s, t, r, p); }
void glGetVertexAttribIiv(GLuint i, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetVertexAttribIiv(i, p, v); }
void glGetVertexAttribIuiv(GLuint i, GLenum p, GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glGetVertexAttribIuiv(i, p, v); }
void glVertexAttribI1i(GLuint i, GLint x) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI1i(i, x); }
void glVertexAttribI2i(GLuint i, GLint x, GLint y) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI2i(i, x, y); }
void glVertexAttribI3i(GLuint i, GLint x, GLint y, GLint z) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI3i(i, x, y, z); }
void glVertexAttribI4i(GLuint i, GLint x, GLint y, GLint z, GLint w) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI4i(i, x, y, z, w); }
void glVertexAttribI1ui(GLuint i, GLuint x) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI1ui(i, x); }
void glVertexAttribI2ui(GLuint i, GLuint x, GLuint y) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI2ui(i, x, y); }
void glVertexAttribI3ui(GLuint i, GLuint x, GLuint y, GLuint z) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI3ui(i, x, y, z); }
void glVertexAttribI4ui(GLuint i, GLuint x, GLuint y, GLuint z, GLuint w) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI4ui(i, x, y, z, w); }
void glVertexAttribI1iv(GLuint i, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI1iv(i, v); }
void glVertexAttribI2iv(GLuint i, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI2iv(i, v); }
void glVertexAttribI3iv(GLuint i, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI3iv(i, v); }
void glVertexAttribI4iv(GLuint i, const GLint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI4iv(i, v); }
void glVertexAttribI1uiv(GLuint i, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI1uiv(i, v); }
void glVertexAttribI2uiv(GLuint i, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI2uiv(i, v); }
void glVertexAttribI3uiv(GLuint i, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI3uiv(i, v); }
void glVertexAttribI4uiv(GLuint i, const GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribI4uiv(i, v); }
void glVertexAttribDivisor(GLuint i, GLuint d) { if(g_gl_context) g_gl_context->dispatch.glVertexAttribDivisor(i, d); }
void glGenFramebuffers(GLsizei n, GLuint *f) { if(g_gl_context) g_gl_context->dispatch.glGenFramebuffers(n, f); }
void glDeleteFramebuffers(GLsizei n, const GLuint *f) { if(g_gl_context) g_gl_context->dispatch.glDeleteFramebuffers(n, f); }
GLboolean glIsFramebuffer(GLuint f) { return g_gl_context ? g_gl_context->dispatch.glIsFramebuffer(f) : GL_FALSE; }
void glGenRenderbuffers(GLsizei n, GLuint *r) { if(g_gl_context) g_gl_context->dispatch.glGenRenderbuffers(n, r); }
void glDeleteRenderbuffers(GLsizei n, const GLuint *r) { if(g_gl_context) g_gl_context->dispatch.glDeleteRenderbuffers(n, r); }
GLboolean glIsRenderbuffer(GLuint r) { return g_gl_context ? g_gl_context->dispatch.glIsRenderbuffer(r) : GL_FALSE; }
void glBindFramebuffer(GLenum t, GLuint f) { if(g_gl_context) g_gl_context->dispatch.glBindFramebuffer(t, f); }
void glBindRenderbuffer(GLenum t, GLuint r) { if(g_gl_context) g_gl_context->dispatch.glBindRenderbuffer(t, r); }
GLenum glCheckFramebufferStatus(GLenum t) { return g_gl_context ? g_gl_context->dispatch.glCheckFramebufferStatus(t) : GL_FRAMEBUFFER_UNSUPPORTED; }
void glFramebufferTexture(GLenum t, GLenum a, GLuint x, GLint l) { if(g_gl_context) g_gl_context->dispatch.glFramebufferTexture(t, a, x, l); }
void glFramebufferTexture2D(GLenum t, GLenum a, GLenum s, GLuint x, GLint l) { if(g_gl_context) g_gl_context->dispatch.glFramebufferTexture2D(t, a, s, x, l); }
void glFramebufferRenderbuffer(GLenum t, GLenum a, GLenum r, GLuint b) { if(g_gl_context) g_gl_context->dispatch.glFramebufferRenderbuffer(t, a, r, b); }
void glRenderbufferStorage(GLenum t, GLenum i, GLsizei w, GLsizei h) { if(g_gl_context) g_gl_context->dispatch.glRenderbufferStorage(t, i, w, h); }
void glRenderbufferStorageMultisample(GLenum t, GLsizei s, GLenum i, GLsizei w, GLsizei h) { if(g_gl_context) g_gl_context->dispatch.glRenderbufferStorageMultisample(t, s, i, w, h); }
void glGetRenderbufferParameteriv(GLenum t, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetRenderbufferParameteriv(t, p, v); }
void glGetFramebufferAttachmentParameteriv(GLenum t, GLenum a, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetFramebufferAttachmentParameteriv(t, a, p, v); }
void glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield m, GLenum f) { if(g_gl_context) g_gl_context->dispatch.glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, m, f); }
void glDrawBuffer(GLenum b) { if(g_gl_context) g_gl_context->dispatch.glDrawBuffer(b); }
void glDrawBuffers(GLsizei n, const GLenum *b) { if(g_gl_context) g_gl_context->dispatch.glDrawBuffers(n, b); }
void glReadBuffer(GLenum s) { if(g_gl_context) g_gl_context->dispatch.glReadBuffer(s); }
void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum p, GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glReadPixels(x, y, w, h, f, p, px); }
void glFlush(void) { if(g_gl_context) g_gl_context->dispatch.glFlush(); }
void glFinish(void) { if(g_gl_context) g_gl_context->dispatch.glFinish(); }
void glGenQueries(GLsizei n, GLuint *i) { if(g_gl_context) g_gl_context->dispatch.glGenQueries(n, i); }
void glDeleteQueries(GLsizei n, const GLuint *i) { if(g_gl_context) g_gl_context->dispatch.glDeleteQueries(n, i); }
GLboolean glIsQuery(GLuint i) { return g_gl_context ? g_gl_context->dispatch.glIsQuery(i) : GL_FALSE; }
void glBeginQuery(GLenum t, GLuint i) { if(g_gl_context) g_gl_context->dispatch.glBeginQuery(t, i); }
void glEndQuery(GLenum t) { if(g_gl_context) g_gl_context->dispatch.glEndQuery(t); }
void glGetQueryiv(GLenum t, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetQueryiv(t, p, v); }
void glGetQueryObjectiv(GLuint i, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetQueryObjectiv(i, p, v); }
void glGetQueryObjectuiv(GLuint i, GLenum p, GLuint *v) { if(g_gl_context) g_gl_context->dispatch.glGetQueryObjectuiv(i, p, v); }
void glBeginConditionalRender(GLuint i, GLenum m) { if(g_gl_context) g_gl_context->dispatch.glBeginConditionalRender(i, m); }
void glEndConditionalRender(void) { if(g_gl_context) g_gl_context->dispatch.glEndConditionalRender(); }
void glBeginQueryIndexed(GLenum t, GLuint i, GLuint d) { if(g_gl_context) g_gl_context->dispatch.glBeginQueryIndexed(t, i, d); }
void glEndQueryIndexed(GLenum t, GLuint i) { if(g_gl_context) g_gl_context->dispatch.glEndQueryIndexed(t, i); }
void glGetQueryIndexediv(GLenum t, GLuint i, GLenum p, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetQueryIndexediv(t, i, p, v); }
void glGenTransformFeedbacks(GLsizei n, GLuint *i) { if(g_gl_context) g_gl_context->dispatch.glGenTransformFeedbacks(n, i); }
void glDeleteTransformFeedbacks(GLsizei n, const GLuint *i) { if(g_gl_context) g_gl_context->dispatch.glDeleteTransformFeedbacks(n, i); }
void glGetBooleanv(GLenum p, GLboolean *d) { if(g_gl_context) g_gl_context->dispatch.glGetBooleanv(p, d); }
void glGetDoublev(GLenum p, GLdouble *d) { if(g_gl_context) g_gl_context->dispatch.glGetDoublev(p, d); }
void glGetIntegerv(GLenum p, GLint *d) { if(g_gl_context) g_gl_context->dispatch.glGetIntegerv(p, d); }
void glGetFloatv(GLenum p, GLfloat *d) { if(g_gl_context) g_gl_context->dispatch.glGetFloatv(p, d); }
const GLubyte *glGetString(GLenum n) { return g_gl_context ? g_gl_context->dispatch.glGetString(n) : NULL; }
const GLubyte *glGetStringi(GLenum n, GLuint i) { return g_gl_context ? g_gl_context->dispatch.glGetStringi(n, i) : NULL; }
void glLogicOp(GLenum o) { if(g_gl_context) g_gl_context->dispatch.glLogicOp(o); }
void glPointSize(GLfloat s) { if(g_gl_context) g_gl_context->dispatch.glPointSize(s); }
void glFlushMappedBufferRange(GLenum t, GLintptr o, GLsizeiptr l) { if(g_gl_context) g_gl_context->dispatch.glFlushMappedBufferRange(t, o, l); }
void glTexImage1D(GLenum t, GLint l, GLint i, GLsizei w, GLint b, GLenum f, GLenum p, const GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glTexImage1D(t, l, i, w, b, f, p, px); }
void glTexSubImage1D(GLenum t, GLint l, GLint o, GLsizei w, GLenum f, GLenum p, const GLvoid *px) { if(g_gl_context) g_gl_context->dispatch.glTexSubImage1D(t, l, o, w, f, p, px); }
void glCopyTexImage1D(GLenum t, GLint l, GLenum i, GLint x, GLint y, GLsizei w, GLint b) { if(g_gl_context) g_gl_context->dispatch.glCopyTexImage1D(t, l, i, x, y, w, b); }
void glCopyTexSubImage1D(GLenum t, GLint l, GLint o, GLint x, GLint y, GLsizei w) { if(g_gl_context) g_gl_context->dispatch.glCopyTexSubImage1D(t, l, o, x, y, w); }
void glCopyTexSubImage3D(GLenum t, GLint l, GLint o, GLint y, GLint z, GLint x, GLint r, GLsizei w, GLsizei h) { if(g_gl_context) g_gl_context->dispatch.glCopyTexSubImage3D(t, l, o, y, z, x, r, w, h); }
void glCompressedTexImage1D(GLenum t, GLint l, GLenum i, GLsizei w, GLint b, GLsizei s, const GLvoid *d) { if(g_gl_context) g_gl_context->dispatch.glCompressedTexImage1D(t, l, i, w, b, s, d); }
void glCompressedTexImage3D(GLenum t, GLint l, GLenum i, GLsizei w, GLsizei h, GLsizei d, GLint b, GLsizei s, const GLvoid *x) { if(g_gl_context) g_gl_context->dispatch.glCompressedTexImage3D(t, l, i, w, h, d, b, s, x); }
void glCompressedTexSubImage1D(GLenum t, GLint l, GLint o, GLsizei w, GLenum f, GLsizei s, const GLvoid *d) { if(g_gl_context) g_gl_context->dispatch.glCompressedTexSubImage1D(t, l, o, w, f, s, d); }
void glCompressedTexSubImage3D(GLenum t, GLint l, GLint o, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum f, GLsizei s, const GLvoid *x) { if(g_gl_context) g_gl_context->dispatch.glCompressedTexSubImage3D(t, l, o, y, z, w, h, d, f, s, x); }
void glGetTexLevelParameteriv(GLenum t, GLint l, GLenum n, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetTexLevelParameteriv(t, l, n, v); }
void glGetTexLevelParameterfv(GLenum t, GLint l, GLenum n, GLfloat *v) { if(g_gl_context) g_gl_context->dispatch.glGetTexLevelParameterfv(t, l, n, v); }
void glGetActiveUniformBlockiv(GLuint p, GLuint i, GLenum n, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetActiveUniformBlockiv(p, i, n, v); }
void glGetActiveUniformBlockName(GLuint p, GLuint i, GLsizei s, GLsizei *l, GLchar *n) { if(g_gl_context) g_gl_context->dispatch.glGetActiveUniformBlockName(p, i, s, l, n); }
void glGetActiveUniformsiv(GLuint p, GLsizei c, const GLuint *i, GLenum n, GLint *v) { if(g_gl_context) g_gl_context->dispatch.glGetActiveUniformsiv(p, c, i, n, v); }
void glGetActiveUniformName(GLuint p, GLuint i, GLsizei s, GLsizei *l, GLchar *n) { if(g_gl_context) g_gl_context->dispatch.glGetActiveUniformName(p, i, s, l, n); }
void glDrawArrays(GLenum m, GLint f, GLsizei c) { if(g_gl_context) g_gl_context->dispatch.glDrawArrays(m, f, c); }
void glDrawArraysInstanced(GLenum m, GLint f, GLsizei c, GLsizei i) { if(g_gl_context) g_gl_context->dispatch.glDrawArraysInstanced(m, f, c, i); }
void glDrawElements(GLenum m, GLsizei c, GLenum t, const GLvoid *i) { if(g_gl_context) g_gl_context->dispatch.glDrawElements(m, c, t, i); }
void glDrawElementsInstanced(GLenum m, GLsizei c, GLenum t, const GLvoid *i, GLsizei s) { if(g_gl_context) g_gl_context->dispatch.glDrawElementsInstanced(m, c, t, i, s); }
void glBlendFunc(GLenum s, GLenum d) { if(g_gl_context) g_gl_context->dispatch.glBlendFunc(s, d); }
void glBlendEquation(GLenum m) { if(g_gl_context) g_gl_context->dispatch.glBlendEquation(m); }
void glBlendEquationSeparate(GLenum r, GLenum a) { if(g_gl_context) g_gl_context->dispatch.glBlendEquationSeparate(r, a); }
void glBlendFuncSeparate(GLenum r, GLenum d, GLenum a, GLenum e) { if(g_gl_context) g_gl_context->dispatch.glBlendFuncSeparate(r, d, a, e); }
void glBlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { if(g_gl_context) g_gl_context->dispatch.glBlendColor(r, g, b, a); }
void glDepthFunc(GLenum f) { if(g_gl_context) g_gl_context->dispatch.glDepthFunc(f); }
void glDepthMask(GLboolean f) { if(g_gl_context) g_gl_context->dispatch.glDepthMask(f); }
void glDepthRange(GLclampd n, GLclampd f) { if(g_gl_context) g_gl_context->dispatch.glDepthRange(n, f); }
void glStencilFunc(GLenum f, GLint r, GLuint m) { if(g_gl_context) g_gl_context->dispatch.glStencilFunc(f, r, m); }
void glStencilFuncSeparate(GLenum f, GLenum n, GLint r, GLuint m) { if(g_gl_context) g_gl_context->dispatch.glStencilFuncSeparate(f, n, r, m); }
void glStencilOp(GLenum f, GLenum z, GLenum p) { if(g_gl_context) g_gl_context->dispatch.glStencilOp(f, z, p); }
void glStencilOpSeparate(GLenum f, GLenum a, GLenum z, GLenum p) { if(g_gl_context) g_gl_context->dispatch.glStencilOpSeparate(f, a, z, p); }
void glStencilMask(GLuint m) { if(g_gl_context) g_gl_context->dispatch.glStencilMask(m); }
void glStencilMaskSeparate(GLenum f, GLuint m) { if(g_gl_context) g_gl_context->dispatch.glStencilMaskSeparate(f, m); }
void glCullFace(GLenum m) { if(g_gl_context) g_gl_context->dispatch.glCullFace(m); }
void glFrontFace(GLenum m) { if(g_gl_context) g_gl_context->dispatch.glFrontFace(m); }
void glPolygonMode(GLenum f, GLenum m) { if(g_gl_context) g_gl_context->dispatch.glPolygonMode(f, m); }
void glPolygonOffset(GLfloat f, GLfloat u) { if(g_gl_context) g_gl_context->dispatch.glPolygonOffset(f, u); }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { if(g_gl_context) g_gl_context->dispatch.glViewport(x, y, w, h); }
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { if(g_gl_context) g_gl_context->dispatch.glScissor(x, y, w, h); }
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { if(g_gl_context) g_gl_context->dispatch.glColorMask(r, g, b, a); }
void glLineWidth(GLfloat w) { if(g_gl_context) g_gl_context->dispatch.glLineWidth(w); }
void glPixelStorei(GLenum n, GLint p) { if(g_gl_context) g_gl_context->dispatch.glPixelStorei(n, p); }
void glPrimitiveRestartIndex(GLuint i) { if(g_gl_context) g_gl_context->dispatch.glPrimitiveRestartIndex(i); }
void glReleaseShaderCompiler(void) { _gl_ReleaseShaderCompiler(); }
void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat, const GLvoid *binary, GLsizei length) { _gl_ShaderBinary(count, shaders, binaryFormat, binary, length); }
void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision) { _gl_GetShaderPrecisionFormat(shadertype, precisiontype, range, precision); }
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data) { _gl_CompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data); }
void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) { _gl_CopyTexImage2D(target, level, internalformat, x, y, width, height, border); }
void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) { _gl_CopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height); }
void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data) { _gl_CompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data); }
void glHint(GLenum target, GLenum mode) { _gl_Hint(target, mode); }
void glSampleCoverage(GLclampf value, GLboolean invert) { _gl_SampleCoverage(value, invert); }
void glDepthRangef(GLclampf n, GLclampf f) { _gl_DepthRange((GLclampd)n, (GLclampd)f); }
void glWiiULoadShaderGroup(GLuint p, const void *g) { _gl_WiiULoadShaderGroup(p, g); }
void glWiiULoadShaderGroupGFD(GLuint p, GLuint i, const void *d) { _gl_WiiULoadShaderGroupGFD(p, i, d); }
void glVertexAttrib1f(GLuint i, GLfloat x) { _gl_VertexAttrib1f(i, x); }
void glVertexAttrib2f(GLuint i, GLfloat x, GLfloat y) { _gl_VertexAttrib2f(i, x, y); }
void glVertexAttrib3f(GLuint i, GLfloat x, GLfloat y, GLfloat z) { _gl_VertexAttrib3f(i, x, y, z); }
void glVertexAttrib4f(GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { _gl_VertexAttrib4f(i, x, y, z, w); }
void glVertexAttrib1fv(GLuint i, const GLfloat *v) { _gl_VertexAttrib1fv(i, v); }
void glVertexAttrib2fv(GLuint i, const GLfloat *v) { _gl_VertexAttrib2fv(i, v); }
void glVertexAttrib3fv(GLuint i, const GLfloat *v) { _gl_VertexAttrib3fv(i, v); }
void glVertexAttrib4fv(GLuint i, const GLfloat *v) { _gl_VertexAttrib4fv(i, v); }



void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *i) { _gl_DrawRangeElements(mode, start, end, count, type, i); }
void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *i, GLint bv) { _gl_DrawElementsBaseVertex(mode, count, type, i, bv); }
void glDrawRangeElementsBaseVertex(GLenum mode, GLuint s, GLuint e, GLsizei count, GLenum type, const GLvoid *i, GLint bv) { _gl_DrawRangeElementsBaseVertex(mode, s, e, count, type, i, bv); }
void glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *i, GLsizei ic, GLint bv) { _gl_DrawElementsInstancedBaseVertex(mode, count, type, i, ic, bv); }
void glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei dc) { _gl_MultiDrawArrays(mode, first, count, dc); }
void glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const GLvoid *const *i, GLsizei dc) { _gl_MultiDrawElements(mode, count, type, i, dc); }
void glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei *count, GLenum type, const GLvoid *const *i, GLsizei dc, const GLint *bv) { _gl_MultiDrawElementsBaseVertex(mode, count, type, i, dc, bv); }
#ifdef __cplusplus
}
#endif