#include "gl_vao.h"

#include "gl_context.h"

#include <stdint.h>

#ifndef GL_INT_2_10_10_10_REV
#define GL_INT_2_10_10_10_REV 0x8D9F
#endif
#ifndef GL_UNSIGNED_INT_2_10_10_10_REV
#define GL_UNSIGNED_INT_2_10_10_10_REV 0x8368
#endif

static GLint sign_extend_packed(GLuint value, uint32_t bits) {
  const GLuint sign_bit = 1u << (bits - 1u);
  const GLuint mask = (1u << bits) - 1u;
  value &= mask;
  return (GLint)((value ^ sign_bit) - sign_bit);
}

static GLfloat normalize_signed_vertex_value(GLint value, uint32_t bits) {
  const double max_encoded = (double)(((uint64_t)1 << bits) - 1u);
  return (GLfloat)((((double)value * 2.0) + 1.0) / max_encoded);
}

static GLfloat normalize_signed_packed_value(GLint value, uint32_t bits) {
  const GLint minimum = -(GLint)((uint32_t)1u << (bits - 1u));
  const GLint maximum = (GLint)(((uint32_t)1u << (bits - 1u)) - 1u);

  if (value <= minimum) {
    return -1.0f;
  }
  return (GLfloat)((double)value / (double)maximum);
}

static GLfloat normalize_unsigned_vertex_value(GLuint value, uint32_t bits) {
  const double max_encoded = (double)(((uint64_t)1 << bits) - 1u);
  return (GLfloat)((double)value / max_encoded);
}

static GLfloat unpack_signed_component(GLuint value, uint32_t bits,
                                       GLboolean normalized) {
  const GLint signed_value = sign_extend_packed(value, bits);

  if (!normalized) {
    return (GLfloat)signed_value;
  }

  return normalize_signed_packed_value(signed_value, bits);
}

static GLfloat unpack_unsigned_component(GLuint value, uint32_t bits,
                                         GLboolean normalized) {
  const GLuint mask = (1u << bits) - 1u;
  value &= mask;

  if (!normalized) {
    return (GLfloat)value;
  }

  return normalize_unsigned_vertex_value(value, bits);
}

static void set_vertex_attrib_packed(GLuint index, GLuint packed, GLenum type,
                                     GLboolean normalized,
                                     GLuint component_count) {
  GLfloat components[4] = {0.0f, 0.0f, 0.0f, 1.0f};

  if (type == GL_INT_2_10_10_10_REV) {
    components[0] = unpack_signed_component(packed >> 0u, 10u, normalized);
    components[1] = unpack_signed_component(packed >> 10u, 10u, normalized);
    components[2] = unpack_signed_component(packed >> 20u, 10u, normalized);
    components[3] = unpack_signed_component(packed >> 30u, 2u, normalized);
  } else if (type == GL_UNSIGNED_INT_2_10_10_10_REV) {
    components[0] = unpack_unsigned_component(packed >> 0u, 10u, normalized);
    components[1] = unpack_unsigned_component(packed >> 10u, 10u, normalized);
    components[2] = unpack_unsigned_component(packed >> 20u, 10u, normalized);
    components[3] = unpack_unsigned_component(packed >> 30u, 2u, normalized);
  } else {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  switch (component_count) {
  case 1:
    _gl_VertexAttrib4f(index, components[0], 0.0f, 0.0f, 1.0f);
    break;
  case 2:
    _gl_VertexAttrib4f(index, components[0], components[1], 0.0f, 1.0f);
    break;
  case 3:
    _gl_VertexAttrib4f(index, components[0], components[1], components[2], 1.0f);
    break;
  case 4:
    _gl_VertexAttrib4f(index, components[0], components[1], components[2],
                       components[3]);
    break;
  default:
    _gl_set_error(GL_INVALID_VALUE);
    break;
  }
}

void glVertexAttribP1ui(GLuint index, GLenum type, GLboolean normalized,
                        GLuint value) {
  set_vertex_attrib_packed(index, value, type, normalized, 1);
}

void glVertexAttribP2ui(GLuint index, GLenum type, GLboolean normalized,
                        GLuint value) {
  set_vertex_attrib_packed(index, value, type, normalized, 2);
}

void glVertexAttribP3ui(GLuint index, GLenum type, GLboolean normalized,
                        GLuint value) {
  set_vertex_attrib_packed(index, value, type, normalized, 3);
}

void glVertexAttribP4ui(GLuint index, GLenum type, GLboolean normalized,
                        GLuint value) {
  set_vertex_attrib_packed(index, value, type, normalized, 4);
}

void glVertexAttribP1uiv(GLuint index, GLenum type, GLboolean normalized,
                         const GLuint *value) {
  if (!value) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  glVertexAttribP1ui(index, type, normalized, *value);
}

void glVertexAttribP2uiv(GLuint index, GLenum type, GLboolean normalized,
                         const GLuint *value) {
  if (!value) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  glVertexAttribP2ui(index, type, normalized, *value);
}

void glVertexAttribP3uiv(GLuint index, GLenum type, GLboolean normalized,
                         const GLuint *value) {
  if (!value) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  glVertexAttribP3ui(index, type, normalized, *value);
}

void glVertexAttribP4uiv(GLuint index, GLenum type, GLboolean normalized,
                         const GLuint *value) {
  if (!value) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  glVertexAttribP4ui(index, type, normalized, *value);
}

void glGetVertexAttribdv(GLuint index, GLenum pname, GLdouble *params) {
  GLfloat values[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  if (!params) return;
  glGetVertexAttribfv(index, pname, values);
  if (pname == GL_CURRENT_VERTEX_ATTRIB) {
    params[0] = (GLdouble)values[0];
    params[1] = (GLdouble)values[1];
    params[2] = (GLdouble)values[2];
    params[3] = (GLdouble)values[3];
  } else {
    params[0] = (GLdouble)values[0];
  }
}

void glVertexAttrib1d(GLuint index, GLdouble x) {
  glVertexAttrib1f(index, (GLfloat)x);
}

void glVertexAttrib1dv(GLuint index, const GLdouble *v) {
  if (v) glVertexAttrib1f(index, (GLfloat)v[0]);
}

void glVertexAttrib1s(GLuint index, GLshort x) {
  glVertexAttrib1f(index, (GLfloat)x);
}

void glVertexAttrib1sv(GLuint index, const GLshort *v) {
  if (v) glVertexAttrib1f(index, (GLfloat)v[0]);
}

void glVertexAttrib2d(GLuint index, GLdouble x, GLdouble y) {
  glVertexAttrib2f(index, (GLfloat)x, (GLfloat)y);
}

void glVertexAttrib2dv(GLuint index, const GLdouble *v) {
  if (v) glVertexAttrib2f(index, (GLfloat)v[0], (GLfloat)v[1]);
}

void glVertexAttrib2s(GLuint index, GLshort x, GLshort y) {
  glVertexAttrib2f(index, (GLfloat)x, (GLfloat)y);
}

void glVertexAttrib2sv(GLuint index, const GLshort *v) {
  if (v) glVertexAttrib2f(index, (GLfloat)v[0], (GLfloat)v[1]);
}

void glVertexAttrib3d(GLuint index, GLdouble x, GLdouble y, GLdouble z) {
  glVertexAttrib3f(index, (GLfloat)x, (GLfloat)y, (GLfloat)z);
}

void glVertexAttrib3dv(GLuint index, const GLdouble *v) {
  if (v) glVertexAttrib3f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2]);
}

void glVertexAttrib3s(GLuint index, GLshort x, GLshort y, GLshort z) {
  glVertexAttrib3f(index, (GLfloat)x, (GLfloat)y, (GLfloat)z);
}

void glVertexAttrib3sv(GLuint index, const GLshort *v) {
  if (v) glVertexAttrib3f(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2]);
}

void glVertexAttrib4Nbv(GLuint index, const GLbyte *v) {
  if (v) {
    glVertexAttrib4f(index,
                     normalize_signed_vertex_value(v[0], 8),
                     normalize_signed_vertex_value(v[1], 8),
                     normalize_signed_vertex_value(v[2], 8),
                     normalize_signed_vertex_value(v[3], 8));
  }
}

void glVertexAttrib4Niv(GLuint index, const GLint *v) {
  if (v) {
    glVertexAttrib4f(index,
                     normalize_signed_vertex_value(v[0], 32),
                     normalize_signed_vertex_value(v[1], 32),
                     normalize_signed_vertex_value(v[2], 32),
                     normalize_signed_vertex_value(v[3], 32));
  }
}

void glVertexAttrib4Nsv(GLuint index, const GLshort *v) {
  if (v) {
    glVertexAttrib4f(index,
                     normalize_signed_vertex_value(v[0], 16),
                     normalize_signed_vertex_value(v[1], 16),
                     normalize_signed_vertex_value(v[2], 16),
                     normalize_signed_vertex_value(v[3], 16));
  }
}

void glVertexAttrib4Nub(GLuint index, GLubyte x, GLubyte y, GLubyte z,
                        GLubyte w) {
  glVertexAttrib4f(index,
                   normalize_unsigned_vertex_value(x, 8),
                   normalize_unsigned_vertex_value(y, 8),
                   normalize_unsigned_vertex_value(z, 8),
                   normalize_unsigned_vertex_value(w, 8));
}

void glVertexAttrib4Nubv(GLuint index, const GLubyte *v) {
  if (v) glVertexAttrib4Nub(index, v[0], v[1], v[2], v[3]);
}

void glVertexAttrib4Nuiv(GLuint index, const GLuint *v) {
  if (v) {
    glVertexAttrib4f(index,
                     normalize_unsigned_vertex_value(v[0], 32),
                     normalize_unsigned_vertex_value(v[1], 32),
                     normalize_unsigned_vertex_value(v[2], 32),
                     normalize_unsigned_vertex_value(v[3], 32));
  }
}

void glVertexAttrib4Nusv(GLuint index, const GLushort *v) {
  if (v) {
    glVertexAttrib4f(index,
                     normalize_unsigned_vertex_value(v[0], 16),
                     normalize_unsigned_vertex_value(v[1], 16),
                     normalize_unsigned_vertex_value(v[2], 16),
                     normalize_unsigned_vertex_value(v[3], 16));
  }
}

void glVertexAttrib4bv(GLuint index, const GLbyte *v) {
  if (v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1],
                          (GLfloat)v[2], (GLfloat)v[3]);
}

void glVertexAttrib4d(GLuint index, GLdouble x, GLdouble y, GLdouble z,
                      GLdouble w) {
  glVertexAttrib4f(index, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

void glVertexAttrib4dv(GLuint index, const GLdouble *v) {
  if (v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1],
                          (GLfloat)v[2], (GLfloat)v[3]);
}

void glVertexAttrib4iv(GLuint index, const GLint *v) {
  if (v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1],
                          (GLfloat)v[2], (GLfloat)v[3]);
}

void glVertexAttrib4s(GLuint index, GLshort x, GLshort y, GLshort z,
                      GLshort w) {
  glVertexAttrib4f(index, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

void glVertexAttrib4sv(GLuint index, const GLshort *v) {
  if (v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1],
                          (GLfloat)v[2], (GLfloat)v[3]);
}

void glVertexAttrib4ubv(GLuint index, const GLubyte *v) {
  if (v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1],
                          (GLfloat)v[2], (GLfloat)v[3]);
}

void glVertexAttrib4uiv(GLuint index, const GLuint *v) {
  if (v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1],
                          (GLfloat)v[2], (GLfloat)v[3]);
}

void glVertexAttrib4usv(GLuint index, const GLushort *v) {
  if (v) glVertexAttrib4f(index, (GLfloat)v[0], (GLfloat)v[1],
                          (GLfloat)v[2], (GLfloat)v[3]);
}

void glVertexAttribI4bv(GLuint index, const GLbyte *v) {
  if (v) glVertexAttribI4i(index, v[0], v[1], v[2], v[3]);
}

void glVertexAttribI4sv(GLuint index, const GLshort *v) {
  if (v) glVertexAttribI4i(index, v[0], v[1], v[2], v[3]);
}

void glVertexAttribI4ubv(GLuint index, const GLubyte *v) {
  if (v) glVertexAttribI4ui(index, v[0], v[1], v[2], v[3]);
}

void glVertexAttribI4usv(GLuint index, const GLushort *v) {
  if (v) glVertexAttribI4ui(index, v[0], v[1], v[2], v[3]);
}
