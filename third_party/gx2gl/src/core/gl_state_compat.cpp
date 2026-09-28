#include "gl_context.h"
#include "gl_framebuffer.h"
#include "state/gl_state.h"

#ifndef GL_SAMPLE_POSITION
#define GL_SAMPLE_POSITION 0x8E50
#endif
#ifndef GL_POINT_SIZE_MIN
#define GL_POINT_SIZE_MIN 0x8126
#endif
#ifndef GL_POINT_SIZE_MAX
#define GL_POINT_SIZE_MAX 0x8127
#endif
#ifndef GL_POINT_FADE_THRESHOLD_SIZE
#define GL_POINT_FADE_THRESHOLD_SIZE 0x8128
#endif
#ifndef GL_POINT_DISTANCE_ATTENUATION
#define GL_POINT_DISTANCE_ATTENUATION 0x8129
#endif
#ifndef GL_FIRST_VERTEX_CONVENTION
#define GL_FIRST_VERTEX_CONVENTION 0x8E4D
#endif
#ifndef GL_LAST_VERTEX_CONVENTION
#define GL_LAST_VERTEX_CONVENTION 0x8E4E
#endif

void glClampColor(GLenum target, GLenum clamp) {
  if (target != GL_CLAMP_READ_COLOR) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (clamp != GL_TRUE && clamp != GL_FALSE && clamp != GL_FIXED_ONLY) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!g_gl_context) return;
  g_gl_context->clamp_read_color = clamp;
}

void glProvokingVertex(GLenum mode) {
  if (mode != GL_FIRST_VERTEX_CONVENTION &&
      mode != GL_LAST_VERTEX_CONVENTION) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!g_gl_context) return;
  if (g_gl_context->provoking_vertex != mode) {
    g_gl_context->provoking_vertex = mode;
    g_gl_context->dirty_flags |= GL_DIRTY_PROVOKING_VERTEX;
  }
}

void glGetMultisamplefv(GLenum pname, GLuint index, GLfloat *values) {
  if (pname != GL_SAMPLE_POSITION) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!g_gl_context || !values) return;
  GLsizei samples = gl_get_draw_sample_count();
  if (index >= (GLuint)samples) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (gl_get_multisample_position(samples, index, values) != GL_TRUE) {
    _gl_set_error(GL_INVALID_OPERATION);
  }
}

void glSampleMaski(GLuint mask_number, GLbitfield mask) {
  if (!g_gl_context) return;
  if (mask_number >= GL33_MAX_SAMPLE_MASK_WORDS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  g_gl_context->sample_mask_value = mask;
  g_gl_context->dirty_flags |= GL_DIRTY_MULTISAMPLE;
}

void glGetBooleani_v(GLenum target, GLuint index, GLboolean *data) {
  if (!g_gl_context || !data) return;

  switch (target) {
  case GL_BLEND:
    if (index != 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    *data = g_gl_context->blend_enabled;
    break;
  case GL_SCISSOR_TEST:
    if (index != 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    *data = g_gl_context->scissor_test_enabled;
    break;
  case GL_COLOR_WRITEMASK:
    if (index != 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    data[0] = g_gl_context->color_mask[0];
    data[1] = g_gl_context->color_mask[1];
    data[2] = g_gl_context->color_mask[2];
    data[3] = g_gl_context->color_mask[3];
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void glPixelStoref(GLenum pname, GLfloat param) {
  glPixelStorei(pname, (GLint)param);
}

void glPointParameterf(GLenum pname, GLfloat param) {
  (void)param;
  if (pname != GL_POINT_SIZE_MIN &&
      pname != GL_POINT_SIZE_MAX &&
      pname != GL_POINT_FADE_THRESHOLD_SIZE) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  _gl_set_error(GL_INVALID_OPERATION);
}

void glPointParameterfv(GLenum pname, const GLfloat *params) {
  (void)params;
  if (pname != GL_POINT_DISTANCE_ATTENUATION) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  _gl_set_error(GL_INVALID_OPERATION);
}

void glPointParameteri(GLenum pname, GLint param) {
  glPointParameterf(pname, (GLfloat)param);
}

void glPointParameteriv(GLenum pname, const GLint *params) {
  (void)params;
  if (pname != GL_POINT_DISTANCE_ATTENUATION) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  _gl_set_error(GL_INVALID_OPERATION);
}
