#include "gl_transform_feedback.h"

#include "gl_buffer.h"
#include "gl_shader.h"

#include <string.h>

#ifndef GL_TRANSFORM_FEEDBACK
#define GL_TRANSFORM_FEEDBACK 0x8E22
#endif
#ifndef GL_INTERLEAVED_ATTRIBS
#define GL_INTERLEAVED_ATTRIBS 0x8C8C
#endif
#ifndef GL_SEPARATE_ATTRIBS
#define GL_SEPARATE_ATTRIBS 0x8C8D
#endif

#define GL33_MAX_TRANSFORM_FEEDBACK_OBJECTS 512

typedef struct {
  bool reserved;
  bool in_use;
  bool active;
  bool paused;
  bool ever_ended;
  GLenum primitive_mode;
  GLuint program;
  GLuint64 primitives_written;
  gl_uniform_buffer_binding_t bindings[GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS];
} GLTransformFeedbackObject;

static GLTransformFeedbackObject
    g_transform_feedback_objects[GL33_MAX_TRANSFORM_FEEDBACK_OBJECTS];

static GLTransformFeedbackObject *transform_feedback_object(GLuint id) {
  if (id >= GL33_MAX_TRANSFORM_FEEDBACK_OBJECTS) return NULL;
  if (!g_transform_feedback_objects[id].in_use) return NULL;
  return &g_transform_feedback_objects[id];
}

static GLTransformFeedbackObject *current_transform_feedback_object(void) {
  if (!g_gl_context) return NULL;
  return transform_feedback_object(g_gl_context->bound_transform_feedback);
}

static bool transform_feedback_name_is_bindable(GLuint id) {
  if (id == 0) return true;
  if (id >= GL33_MAX_TRANSFORM_FEEDBACK_OBJECTS) return false;
  return g_transform_feedback_objects[id].reserved ||
         g_transform_feedback_objects[id].in_use;
}

static GLTransformFeedbackObject *create_or_get_transform_feedback(GLuint id) {
  GLTransformFeedbackObject *object;

  if (!transform_feedback_name_is_bindable(id)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return NULL;
  }

  object = &g_transform_feedback_objects[id];
  if (!object->in_use) {
    memset(object, 0, sizeof(*object));
    object->reserved = true;
    object->in_use = true;
  }
  return object;
}

static bool allocate_transform_feedback_names(GLsizei n, GLuint *ids) {
  GLsizei found = 0;

  for (GLuint id = 1;
       id < GL33_MAX_TRANSFORM_FEEDBACK_OBJECTS && found < n;
       ++id) {
    if (!g_transform_feedback_objects[id].reserved &&
        !g_transform_feedback_objects[id].in_use) {
      ids[found++] = id;
    }
  }

  if (found != n) {
    for (GLsizei i = 0; i < n; ++i) ids[i] = 0;
    _gl_set_error(GL_OUT_OF_MEMORY);
    return false;
  }

  for (GLsizei i = 0; i < n; ++i) {
    g_transform_feedback_objects[ids[i]].reserved = true;
  }
  return true;
}

static bool is_transform_feedback_primitive_mode(GLenum mode) {
  return mode == GL_POINTS || mode == GL_LINES || mode == GL_TRIANGLES;
}

void gl_transform_feedback_init(void) {
  memset(g_transform_feedback_objects, 0, sizeof(g_transform_feedback_objects));
  g_transform_feedback_objects[0].in_use = true;
}

void gl_transform_feedback_shutdown(void) {
  gl_transform_feedback_init();
}

void _gl_GenTransformFeedbacks(GLsizei n, GLuint *ids) {
  if (n < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (n > 0 && !ids) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (n == 0) return;

  allocate_transform_feedback_names(n, ids);
}

void _gl_DeleteTransformFeedbacks(GLsizei n, const GLuint *ids) {
  if (n < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (n > 0 && !ids) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  for (GLsizei i = 0; i < n; ++i) {
    GLuint id = ids[i];
    GLTransformFeedbackObject *object;

    if (id == 0 || id >= GL33_MAX_TRANSFORM_FEEDBACK_OBJECTS) continue;
    object = &g_transform_feedback_objects[id];
    if (object->in_use && object->active) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }
  }

  for (GLsizei i = 0; i < n; ++i) {
    GLuint id = ids[i];

    if (id == 0 || id >= GL33_MAX_TRANSFORM_FEEDBACK_OBJECTS) continue;
    if (!g_transform_feedback_objects[id].reserved &&
        !g_transform_feedback_objects[id].in_use) {
      continue;
    }

    if (g_gl_context && g_gl_context->bound_transform_feedback == id) {
      g_gl_context->bound_transform_feedback = 0;
      memcpy(g_gl_context->transform_feedback_buffer_bindings,
             g_transform_feedback_objects[0].bindings,
             sizeof(g_gl_context->transform_feedback_buffer_bindings));
    }
    memset(&g_transform_feedback_objects[id], 0,
           sizeof(g_transform_feedback_objects[id]));
  }
}

gl_uniform_buffer_binding_t *gl_transform_feedback_current_buffer_binding(GLuint index) {
  GLTransformFeedbackObject *object;

  if (index >= GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS) return NULL;
  object = current_transform_feedback_object();
  if (!object) return NULL;
  return &object->bindings[index];
}

GLboolean gl_transform_feedback_current_active(void) {
  GLTransformFeedbackObject *object = current_transform_feedback_object();
  return (object && object->active) ? GL_TRUE : GL_FALSE;
}

GLboolean gl_transform_feedback_current_active_unpaused(void) {
  GLTransformFeedbackObject *object = current_transform_feedback_object();
  return (object && object->active && !object->paused) ? GL_TRUE : GL_FALSE;
}

GLboolean gl_transform_feedback_current_active_paused(void) {
  GLTransformFeedbackObject *object = current_transform_feedback_object();
  return (object && object->active && object->paused) ? GL_TRUE : GL_FALSE;
}

GLboolean gl_transform_feedback_program_active(GLuint program) {
  if (program == 0) return GL_FALSE;
  for (GLuint id = 0; id < GL33_MAX_TRANSFORM_FEEDBACK_OBJECTS; ++id) {
    GLTransformFeedbackObject *object = &g_transform_feedback_objects[id];
    if (object->in_use && object->active && object->program == program) {
      return GL_TRUE;
    }
  }
  return GL_FALSE;
}

void gl_transform_feedback_unbind_buffer(GLuint buffer) {
  if (buffer == 0) return;
  for (GLuint id = 0; id < GL33_MAX_TRANSFORM_FEEDBACK_OBJECTS; ++id) {
    GLTransformFeedbackObject *object = &g_transform_feedback_objects[id];
    if (!object->in_use) continue;
    for (GLuint binding = 0;
         binding < GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS;
         ++binding) {
      if (object->bindings[binding].buffer == buffer) {
        memset(&object->bindings[binding], 0,
               sizeof(object->bindings[binding]));
      }
    }
  }
  if (g_gl_context) {
    for (GLuint binding = 0;
         binding < GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS;
         ++binding) {
      if (g_gl_context->transform_feedback_buffer_bindings[binding].buffer == buffer) {
        memset(&g_gl_context->transform_feedback_buffer_bindings[binding], 0,
               sizeof(g_gl_context->transform_feedback_buffer_bindings[binding]));
      }
    }
  }
}

GLboolean gl_transform_feedback_validate_draw_mode(GLenum mode) {
  GLTransformFeedbackObject *object = current_transform_feedback_object();

  if (!object || !object->active || object->paused) return GL_TRUE;

  switch (object->primitive_mode) {
  case GL_POINTS:
    return mode == GL_POINTS ? GL_TRUE : GL_FALSE;
  case GL_LINES:
    return (mode == GL_LINES || mode == GL_LINE_LOOP ||
            mode == GL_LINE_STRIP)
               ? GL_TRUE
               : GL_FALSE;
  case GL_TRIANGLES:
    return (mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP ||
            mode == GL_TRIANGLE_FAN)
               ? GL_TRUE
               : GL_FALSE;
  default:
    return GL_FALSE;
  }
}

void glBeginTransformFeedback(GLenum primitiveMode) {
  GLTransformFeedbackObject *object;
  GLuint program;
  GLuint binding_count;

  if (!is_transform_feedback_primitive_mode(primitiveMode)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!g_gl_context) return;

  object = current_transform_feedback_object();
  if (!object || object->active) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  program = g_gl_context->bound_program;
  if (program == 0 || gl_program_transform_feedback_ready(program) != GL_TRUE) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  binding_count = gl_program_transform_feedback_binding_count(program);
  if (binding_count == 0 ||
      binding_count > GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  for (GLuint binding = 0; binding < binding_count; ++binding) {
    gl_uniform_buffer_binding_t *buffer_binding = &object->bindings[binding];
    GLsizeiptr size;

    if (buffer_binding->buffer == 0) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }
    size = buffer_binding->whole_buffer
               ? gl_buffer_get_size(buffer_binding->buffer)
               : buffer_binding->size;
    if (size <= 0 || gl_buffer_is_mapped(buffer_binding->buffer) == GL_TRUE) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }
  }

  _gl_set_error(GL_INVALID_OPERATION);
}

void glEndTransformFeedback(void) {
  GLTransformFeedbackObject *object = current_transform_feedback_object();

  if (!object || !object->active) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  object->active = false;
  object->paused = false;
  object->ever_ended = true;
}

void glPauseTransformFeedback(void) {
  GLTransformFeedbackObject *object = current_transform_feedback_object();

  if (!object || !object->active || object->paused) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  object->paused = true;
}

void glResumeTransformFeedback(void) {
  GLTransformFeedbackObject *object = current_transform_feedback_object();

  if (!object || !object->active || !object->paused ||
      !g_gl_context || g_gl_context->bound_program != object->program) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  object->paused = false;
}

void glBindTransformFeedback(GLenum target, GLuint id) {
  GLTransformFeedbackObject *object;

  if (target != GL_TRANSFORM_FEEDBACK) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!g_gl_context) return;
  if (gl_transform_feedback_current_active_unpaused() == GL_TRUE) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  object = create_or_get_transform_feedback(id);
  if (!object) return;

  g_gl_context->bound_transform_feedback = id;
  memcpy(g_gl_context->transform_feedback_buffer_bindings,
         object->bindings,
         sizeof(g_gl_context->transform_feedback_buffer_bindings));
}

GLboolean glIsTransformFeedback(GLuint id) {
  if (id == 0) return GL_FALSE;
  return transform_feedback_object(id) ? GL_TRUE : GL_FALSE;
}

void glTransformFeedbackVaryings(GLuint program, GLsizei count,
                                 const GLchar *const *varyings,
                                 GLenum bufferMode) {
  if (!_gl_IsProgram(program) || count < 0 || (count > 0 && !varyings)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (bufferMode != GL_INTERLEAVED_ATTRIBS &&
      bufferMode != GL_SEPARATE_ATTRIBS) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (bufferMode == GL_SEPARATE_ATTRIBS &&
      count > GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (gl_transform_feedback_current_active() == GL_TRUE) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  _gl_TransformFeedbackVaryings(program, count, varyings, bufferMode);
}

void glGetTransformFeedbackVarying(GLuint program, GLuint index,
                                   GLsizei bufSize, GLsizei *length,
                                   GLsizei *size, GLenum *type,
                                   GLchar *name) {
  if (!_gl_IsProgram(program) || bufSize < 0 || (bufSize > 0 && !name)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  _gl_GetTransformFeedbackVarying(program, index, bufSize, length, size, type,
                                  name);
}

void glDrawTransformFeedback(GLenum mode, GLuint id) {
  GLTransformFeedbackObject *object;

  if (!is_transform_feedback_primitive_mode(mode)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (id == 0 || id >= GL33_MAX_TRANSFORM_FEEDBACK_OBJECTS ||
      !g_transform_feedback_objects[id].in_use) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  object = &g_transform_feedback_objects[id];
  if (!object->ever_ended) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  if (object->primitives_written == 0) {
    return;
  }

  _gl_set_error(GL_INVALID_OPERATION);
}
