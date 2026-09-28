#include "gl_context.h"

#include "gl_buffer.h"
#include "gl_transform_feedback.h"

void glEnablei(GLenum cap, GLuint index) {
  if (index != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (cap != GL_BLEND && cap != GL_SCISSOR_TEST) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  glEnable(cap);
}

void glDisablei(GLenum cap, GLuint index) {
  if (index != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (cap != GL_BLEND && cap != GL_SCISSOR_TEST) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  glDisable(cap);
}

GLboolean glIsEnabledi(GLenum cap, GLuint index) {
  if (index != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return GL_FALSE;
  }
  if (cap != GL_BLEND && cap != GL_SCISSOR_TEST) {
    _gl_set_error(GL_INVALID_ENUM);
    return GL_FALSE;
  }
  return glIsEnabled(cap);
}

void glColorMaski(GLuint index, GLboolean red, GLboolean green,
                  GLboolean blue, GLboolean alpha) {
  if (index != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  glColorMask(red, green, blue, alpha);
}

void glGetIntegeri_v(GLenum target, GLuint index, GLint *data) {
  if (!g_gl_context || !data) return;

  if (target == GL_SAMPLE_MASK_VALUE && index < GL33_MAX_SAMPLE_MASK_WORDS) {
    *data = (GLint)g_gl_context->sample_mask_value;
  } else if (target == GL_SAMPLE_MASK_VALUE) {
    _gl_set_error(GL_INVALID_VALUE);
  } else if (target == GL_UNIFORM_BUFFER_BINDING &&
             index < GL33_MAX_UNIFORM_BUFFER_BINDINGS) {
    *data = (GLint)g_gl_context->uniform_buffer_bindings[index].buffer;
  } else if (target == GL_UNIFORM_BUFFER_START &&
             index < GL33_MAX_UNIFORM_BUFFER_BINDINGS) {
    *data = g_gl_context->uniform_buffer_bindings[index].whole_buffer
                ? 0
                : (GLint)g_gl_context->uniform_buffer_bindings[index].offset;
  } else if (target == GL_UNIFORM_BUFFER_SIZE &&
             index < GL33_MAX_UNIFORM_BUFFER_BINDINGS) {
    *data = g_gl_context->uniform_buffer_bindings[index].whole_buffer
                ? (GLint)gl_buffer_get_size(
                      g_gl_context->uniform_buffer_bindings[index].buffer)
                : (GLint)g_gl_context->uniform_buffer_bindings[index].size;
  } else if (target == GL_UNIFORM_BUFFER_BINDING ||
             target == GL_UNIFORM_BUFFER_START ||
             target == GL_UNIFORM_BUFFER_SIZE) {
    _gl_set_error(GL_INVALID_VALUE);
  } else if (target == GL_TRANSFORM_FEEDBACK_BUFFER_BINDING &&
             index < GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS) {
    gl_uniform_buffer_binding_t *binding =
        gl_transform_feedback_current_buffer_binding(index);
    *data = binding ? (GLint)binding->buffer : 0;
  } else if (target == GL_TRANSFORM_FEEDBACK_BUFFER_START &&
             index < GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS) {
    gl_uniform_buffer_binding_t *binding =
        gl_transform_feedback_current_buffer_binding(index);
    *data = (!binding || binding->whole_buffer) ? 0 : (GLint)binding->offset;
  } else if (target == GL_TRANSFORM_FEEDBACK_BUFFER_SIZE &&
             index < GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS) {
    gl_uniform_buffer_binding_t *binding =
        gl_transform_feedback_current_buffer_binding(index);
    *data = (!binding || !binding->buffer)
                ? 0
                : (binding->whole_buffer
                       ? (GLint)gl_buffer_get_size(binding->buffer)
                       : (GLint)binding->size);
  } else if (target == GL_TRANSFORM_FEEDBACK_BUFFER_BINDING ||
             target == GL_TRANSFORM_FEEDBACK_BUFFER_START ||
             target == GL_TRANSFORM_FEEDBACK_BUFFER_SIZE) {
    _gl_set_error(GL_INVALID_VALUE);
  } else {
    _gl_set_error(GL_INVALID_ENUM);
  }
}

void glGetInteger64v(GLenum pname, GLint64 *data) {
  _gl_GetInteger64v(pname, data);
}

void glGetInteger64i_v(GLenum target, GLuint index, GLint64 *data) {
  gl_uniform_buffer_binding_t *binding;

  if (!g_gl_context || !data) return;
  if (target == GL_UNIFORM_BUFFER_START &&
      index < GL33_MAX_UNIFORM_BUFFER_BINDINGS) {
    binding = &g_gl_context->uniform_buffer_bindings[index];
    *data = binding->whole_buffer ? 0 : (GLint64)binding->offset;
    return;
  }
  if (target == GL_UNIFORM_BUFFER_SIZE &&
      index < GL33_MAX_UNIFORM_BUFFER_BINDINGS) {
    binding = &g_gl_context->uniform_buffer_bindings[index];
    *data = (!binding->buffer)
                ? 0
                : (binding->whole_buffer
                       ? (GLint64)gl_buffer_get_size(binding->buffer)
                       : (GLint64)binding->size);
    return;
  }
  if (target == GL_TRANSFORM_FEEDBACK_BUFFER_START &&
      index < GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS) {
    binding = gl_transform_feedback_current_buffer_binding(index);
    *data = (!binding || binding->whole_buffer) ? 0 : (GLint64)binding->offset;
    return;
  }
  if (target == GL_TRANSFORM_FEEDBACK_BUFFER_SIZE &&
      index < GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS) {
    binding = gl_transform_feedback_current_buffer_binding(index);
    *data = (!binding || !binding->buffer)
                ? 0
                : (binding->whole_buffer
                       ? (GLint64)gl_buffer_get_size(binding->buffer)
                       : (GLint64)binding->size);
    return;
  }

  GLint value = 0;
  glGetIntegeri_v(target, index, &value);
  *data = (GLint64)value;
}

void glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params) {
  GLint value = 0;

  if (!params) return;
  glGetBufferParameteriv(target, pname, &value);
  *params = (GLint64)value;
}
