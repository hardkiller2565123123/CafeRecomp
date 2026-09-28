#include "gl_context.h"
#include "gl_framebuffer.h"
#include "state/gl_state.h"

#include <gx2/clear.h>

namespace {

static const GLuint kMaxDrawBuffers = 8;

static GLfloat clamp01(GLfloat value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

static bool validate_color_drawbuffer(GLint drawbuffer) {
  if (drawbuffer < 0 || drawbuffer >= (GLint)kMaxDrawBuffers) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }
  return true;
}

static bool validate_depth_stencil_drawbuffer(GLint drawbuffer) {
  if (drawbuffer != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }
  return true;
}

static bool ensure_draw_framebuffer_complete(void) {
  if (_gl_CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
      GL_FRAMEBUFFER_COMPLETE) {
    _gl_set_error(GL_INVALID_FRAMEBUFFER_OPERATION);
    return false;
  }
  return true;
}

static void clear_color_buffer(GLint drawbuffer, const GLfloat value[4]) {
  GX2ColorBuffer *color;

  if (!validate_color_drawbuffer(drawbuffer) ||
      !ensure_draw_framebuffer_complete()) {
    return;
  }

  if (!gl_is_draw_color_buffer_enabled((GLuint)drawbuffer)) {
    return;
  }

  gl_flush_state();
  if (gl_state_cpu_clear_draw_color_buffer((GLuint)drawbuffer, value)) {
    gl_framebuffer_mark_bound_color_buffer_dirty((GLuint)drawbuffer);
    return;
  }

  color = gl_get_draw_color_buffer((GLuint)drawbuffer);
  if (!color) {
    return;
  }

  GX2ClearColor(color, value[0], value[1], value[2], value[3]);
  gl_framebuffer_mark_bound_color_buffer_dirty((GLuint)drawbuffer);
}

static void clear_depth_buffer(GLfloat depth_value) {
  GX2DepthBuffer *depth;
  GLfloat old_depth;

  if (!validate_depth_stencil_drawbuffer(0) ||
      !ensure_draw_framebuffer_complete()) {
    return;
  }

  old_depth = g_gl_context->clear_depth;
  g_gl_context->clear_depth = clamp01(depth_value);
  _gl_Clear(GL_DEPTH_BUFFER_BIT);
  g_gl_context->clear_depth = old_depth;

  depth = gl_get_draw_depth_buffer();
  if (depth) {
    depth->depthClear = g_gl_context->clear_depth;
  }
}

static void clear_stencil_buffer(GLint stencil_value) {
  GLint old_stencil;
  GX2DepthBuffer *depth;

  if (!validate_depth_stencil_drawbuffer(0) ||
      !ensure_draw_framebuffer_complete()) {
    return;
  }

  old_stencil = g_gl_context->clear_stencil;
  g_gl_context->clear_stencil = stencil_value;
  _gl_Clear(GL_STENCIL_BUFFER_BIT);
  g_gl_context->clear_stencil = old_stencil;

  depth = gl_get_draw_depth_buffer();
  if (depth) {
    depth->stencilClear = (uint8_t)g_gl_context->clear_stencil;
  }
}

} // namespace

void glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint *value) {
  GLfloat color[4];

  if (!g_gl_context || !value) return;

  if (buffer == GL_STENCIL) {
    if (!validate_depth_stencil_drawbuffer(drawbuffer)) return;
    clear_stencil_buffer(value[0]);
    return;
  }

  if (buffer == GL_COLOR) {
    color[0] = (GLfloat)value[0];
    color[1] = (GLfloat)value[1];
    color[2] = (GLfloat)value[2];
    color[3] = (GLfloat)value[3];
    clear_color_buffer(drawbuffer, color);
    return;
  }

  _gl_set_error(GL_INVALID_ENUM);
}

void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint *value) {
  GLfloat color[4];

  if (!g_gl_context || !value) return;

  if (buffer != GL_COLOR) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  color[0] = (GLfloat)value[0];
  color[1] = (GLfloat)value[1];
  color[2] = (GLfloat)value[2];
  color[3] = (GLfloat)value[3];
  clear_color_buffer(drawbuffer, color);
}

void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value) {
  GLfloat color[4];

  if (!g_gl_context || !value) return;

  if (buffer == GL_COLOR) {
    color[0] = value[0];
    color[1] = value[1];
    color[2] = value[2];
    color[3] = value[3];
    clear_color_buffer(drawbuffer, color);
    return;
  }

  if (buffer == GL_DEPTH) {
    if (!validate_depth_stencil_drawbuffer(drawbuffer)) return;
    clear_depth_buffer(value[0]);
    return;
  }

  _gl_set_error(GL_INVALID_ENUM);
}

void glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth,
                     GLint stencil) {
  GLfloat old_depth;
  GLint old_stencil;
  GX2DepthBuffer *depth_buffer;

  if (!g_gl_context) return;

  if (buffer != GL_DEPTH_STENCIL) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!validate_depth_stencil_drawbuffer(drawbuffer) ||
      !ensure_draw_framebuffer_complete()) {
    return;
  }

  old_depth = g_gl_context->clear_depth;
  old_stencil = g_gl_context->clear_stencil;
  g_gl_context->clear_depth = clamp01(depth);
  g_gl_context->clear_stencil = stencil;
  _gl_Clear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  g_gl_context->clear_depth = old_depth;
  g_gl_context->clear_stencil = old_stencil;

  depth_buffer = gl_get_draw_depth_buffer();
  if (depth_buffer) {
    depth_buffer->depthClear = g_gl_context->clear_depth;
    depth_buffer->stencilClear = (uint8_t)g_gl_context->clear_stencil;
  }
}
