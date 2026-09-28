#include "gl_shader.h"

#include <string.h>

void glGetUniformIndices(GLuint program, GLsizei count,
                         const GLchar *const *names, GLuint *indices) {
  GLint active_uniforms = 0;
  GLchar active_name[256];
  GLsizei active_name_length = 0;

  if (!indices || !names || count < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!_gl_IsProgram(program)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  for (GLsizei i = 0; i < count; i++) indices[i] = GL_INVALID_INDEX;
  _gl_GetProgramiv(program, GL_ACTIVE_UNIFORMS, &active_uniforms);

  for (GLsizei i = 0; i < count; ++i) {
    if (!names[i]) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    for (GLint uniform_index = 0;
         uniform_index < active_uniforms;
         ++uniform_index) {
      memset(active_name, 0, sizeof(active_name));
      active_name_length = 0;
      _gl_GetActiveUniformName(program, (GLuint)uniform_index,
                               (GLsizei)sizeof(active_name),
                               &active_name_length, active_name);
      if (strcmp(active_name, names[i]) == 0) {
        indices[i] = (GLuint)uniform_index;
        break;
      }
    }
  }
}

void glBindFragDataLocation(GLuint program, GLuint color, const GLchar *name) {
  if (!_gl_IsProgram(program)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (color >= 8 || !name) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  _gl_set_error(GL_INVALID_OPERATION);
}

GLint glGetFragDataLocation(GLuint program, const GLchar *name) {
  if (!_gl_IsProgram(program) || !name) {
    _gl_set_error(GL_INVALID_VALUE);
    return -1;
  }
  _gl_set_error(GL_INVALID_OPERATION);
  return -1;
}

void glBindFragDataLocationIndexed(GLuint program, GLuint color_number,
                                   GLuint index, const GLchar *name) {
  if (!_gl_IsProgram(program)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (color_number >= 8 || index > 0 || !name) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  _gl_set_error(GL_INVALID_OPERATION);
}

GLint glGetFragDataIndex(GLuint program, const GLchar *name) {
  if (!_gl_IsProgram(program) || !name) {
    _gl_set_error(GL_INVALID_VALUE);
    return -1;
  }
  _gl_set_error(GL_INVALID_OPERATION);
  return -1;
}
