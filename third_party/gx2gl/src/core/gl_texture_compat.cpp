#include "gl_context.h"
#include "gl_buffer.h"
#include "gl_texture.h"

void glTexImage2DMultisample(GLenum target, GLsizei samples,
                             GLenum internalformat, GLsizei width,
                             GLsizei height,
                             GLboolean fixed_sample_locations) {
  _gl_TexImage2DMultisample(target, samples, internalformat, width, height,
                            fixed_sample_locations);
}

void glTexImage3DMultisample(GLenum target, GLsizei samples,
                             GLenum internalformat, GLsizei width,
                             GLsizei height, GLsizei depth,
                             GLboolean fixed_sample_locations) {
  _gl_TexImage3DMultisample(target, samples, internalformat, width, height,
                            depth, fixed_sample_locations);
}

void glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer) {
  (void)internalformat;
  if (target != GL_TEXTURE_BUFFER) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (buffer != 0 && !_gl_IsBuffer(buffer)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  _gl_set_error(GL_INVALID_OPERATION);
}

void glGetCompressedTexImage(GLenum target, GLint level, GLvoid *img) {
  _gl_GetCompressedTexImage(target, level, img);
}

void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type,
                   GLvoid *pixels) {
  _gl_GetTexImage(target, level, format, type, pixels);
}

void glGetSamplerParameterIiv(GLuint sampler, GLenum pname, GLint *params) {
  if (!params) return;
  glGetSamplerParameteriv(sampler, pname, params);
}

void glGetSamplerParameterIuiv(GLuint sampler, GLenum pname, GLuint *params) {
  GLint value = 0;

  if (!params) return;
  glGetSamplerParameteriv(sampler, pname, &value);
  *params = (GLuint)value;
}

void glGetTexParameterIiv(GLenum target, GLenum pname, GLint *params) {
  if (!params) return;
  glGetTexParameteriv(target, pname, params);
}

void glGetTexParameterIuiv(GLenum target, GLenum pname, GLuint *params) {
  GLint value = 0;

  if (!params) return;
  glGetTexParameteriv(target, pname, &value);
  *params = (GLuint)value;
}

void glSamplerParameterIiv(GLuint sampler, GLenum pname, const GLint *param) {
  if (!param) return;
  glSamplerParameteriv(sampler, pname, param);
}

void glSamplerParameterIuiv(GLuint sampler, GLenum pname, const GLuint *param) {
  GLint value;

  if (!param) return;
  value = (GLint)param[0];
  glSamplerParameteriv(sampler, pname, &value);
}

void glTexParameterIiv(GLenum target, GLenum pname, const GLint *params) {
  if (!params) return;
  glTexParameteriv(target, pname, params);
}

void glTexParameterIuiv(GLenum target, GLenum pname, const GLuint *params) {
  GLint value;

  if (!params) return;
  value = (GLint)params[0];
  glTexParameteriv(target, pname, &value);
}
