#include "gl_vao.h"
#include "gl_context.h"
#include "state/gl_state.h"
#include <stdint.h>
#include <string.h>

#define MAX_VAOS 256

typedef struct {
  GLuint buffer;
  GLint size;
  GLenum type;
  GLboolean normalized;
  GLboolean integer;
  GLsizei stride;
  const GLvoid *pointer;
  GLuint divisor;
  GLboolean enabled;
} GLVertexAttrib;

typedef struct {
  bool reserved;
  bool in_use;
  bool dirty;
  GLuint element_array_buffer;
  GLVertexAttrib attribs[GL33_MAX_VERTEX_ATTRIBS];
} GLVAO;

static GLVAO g_vaos[MAX_VAOS];
static GLuint g_bound_vao = 0;
static GLint g_current_attrib_i[GL33_MAX_VERTEX_ATTRIBS][4];
static GLuint g_current_attrib_ui[GL33_MAX_VERTEX_ATTRIBS][4];

static void init_attrib(GLVertexAttrib *attrib) {
  memset(attrib, 0, sizeof(*attrib));
  attrib->size = 4;
  attrib->type = GL_FLOAT;
  attrib->normalized = GL_FALSE;
  attrib->integer = GL_FALSE;
  attrib->stride = 0;
  attrib->pointer = NULL;
  attrib->divisor = 0;
  attrib->enabled = GL_FALSE;
}

static void init_vao(GLVAO *vao, bool live) {
  memset(vao, 0, sizeof(*vao));
  vao->in_use = live;
  for (GLuint i = 0; i < GL33_MAX_VERTEX_ATTRIBS; ++i) {
    init_attrib(&vao->attribs[i]);
  }
}

static GLVAO *current_vao(bool require_live) {
  GLuint id;

  if (!g_gl_context) return NULL;

  id = g_bound_vao;
  if (id >= MAX_VAOS || !g_vaos[id].in_use) {
    if (require_live) _gl_set_error(GL_INVALID_OPERATION);
    return NULL;
  }

  return &g_vaos[id];
}

static bool vertex_attrib_pointer_type(GLenum type) {
  switch (type) {
  case GL_BYTE:
  case GL_UNSIGNED_BYTE:
  case GL_SHORT:
  case GL_UNSIGNED_SHORT:
  case GL_INT:
  case GL_UNSIGNED_INT:
  case GL_HALF_FLOAT:
  case GL_FLOAT:
    return true;
  default:
    return false;
  }
}

static bool vertex_attrib_ipointer_type(GLenum type) {
  switch (type) {
  case GL_BYTE:
  case GL_UNSIGNED_BYTE:
  case GL_SHORT:
  case GL_UNSIGNED_SHORT:
  case GL_INT:
  case GL_UNSIGNED_INT:
    return true;
  default:
    return false;
  }
}

static bool valid_attrib_size(GLint size) {
  return size >= 1 && size <= 4;
}

static void mark_vao_dirty(GLVAO *vao) {
  if (vao) vao->dirty = true;
  if (g_gl_context) g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

static void set_current_float(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                              GLfloat w) {
  if (!g_gl_context) return;
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  g_gl_context->current_vertex_attrib[index][0] = x;
  g_gl_context->current_vertex_attrib[index][1] = y;
  g_gl_context->current_vertex_attrib[index][2] = z;
  g_gl_context->current_vertex_attrib[index][3] = w;
  g_current_attrib_i[index][0] = (GLint)x;
  g_current_attrib_i[index][1] = (GLint)y;
  g_current_attrib_i[index][2] = (GLint)z;
  g_current_attrib_i[index][3] = (GLint)w;
  g_current_attrib_ui[index][0] = (GLuint)g_current_attrib_i[index][0];
  g_current_attrib_ui[index][1] = (GLuint)g_current_attrib_i[index][1];
  g_current_attrib_ui[index][2] = (GLuint)g_current_attrib_i[index][2];
  g_current_attrib_ui[index][3] = (GLuint)g_current_attrib_i[index][3];
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

static void set_current_signed(GLuint index, GLint x, GLint y, GLint z,
                               GLint w) {
  if (!g_gl_context) return;
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  g_current_attrib_i[index][0] = x;
  g_current_attrib_i[index][1] = y;
  g_current_attrib_i[index][2] = z;
  g_current_attrib_i[index][3] = w;
  g_current_attrib_ui[index][0] = (GLuint)x;
  g_current_attrib_ui[index][1] = (GLuint)y;
  g_current_attrib_ui[index][2] = (GLuint)z;
  g_current_attrib_ui[index][3] = (GLuint)w;
  g_gl_context->current_vertex_attrib[index][0] = (GLfloat)x;
  g_gl_context->current_vertex_attrib[index][1] = (GLfloat)y;
  g_gl_context->current_vertex_attrib[index][2] = (GLfloat)z;
  g_gl_context->current_vertex_attrib[index][3] = (GLfloat)w;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

static void set_current_unsigned(GLuint index, GLuint x, GLuint y, GLuint z,
                                 GLuint w) {
  if (!g_gl_context) return;
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  g_current_attrib_ui[index][0] = x;
  g_current_attrib_ui[index][1] = y;
  g_current_attrib_ui[index][2] = z;
  g_current_attrib_ui[index][3] = w;
  g_current_attrib_i[index][0] = (GLint)x;
  g_current_attrib_i[index][1] = (GLint)y;
  g_current_attrib_i[index][2] = (GLint)z;
  g_current_attrib_i[index][3] = (GLint)w;
  g_gl_context->current_vertex_attrib[index][0] = (GLfloat)x;
  g_gl_context->current_vertex_attrib[index][1] = (GLfloat)y;
  g_gl_context->current_vertex_attrib[index][2] = (GLfloat)z;
  g_gl_context->current_vertex_attrib[index][3] = (GLfloat)w;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

static bool store_pointer(GLuint index, GLint size, GLenum type,
                          GLboolean normalized, GLsizei stride,
                          const GLvoid *pointer, GLboolean integer) {
  GLVAO *vao;
  GLVertexAttrib *attrib;

  if (!g_gl_context) return false;
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }
  if (!valid_attrib_size(size)) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }
  if (integer) {
    if (!vertex_attrib_ipointer_type(type)) {
      _gl_set_error(GL_INVALID_ENUM);
      return false;
    }
  } else if (!vertex_attrib_pointer_type(type)) {
    _gl_set_error(GL_INVALID_ENUM);
    return false;
  }
  if (stride < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }

  vao = current_vao(true);
  if (!vao) return false;

  if (g_gl_context->bound_array_buffer == 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  attrib = &vao->attribs[index];
  attrib->buffer = g_gl_context->bound_array_buffer;
  attrib->size = size;
  attrib->type = type;
  attrib->normalized = integer ? GL_FALSE : normalized;
  attrib->integer = integer;
  attrib->stride = stride;
  attrib->pointer = pointer;
  mark_vao_dirty(vao);
  return true;
}

void gl_vao_init(void) {
  memset(g_vaos, 0, sizeof(g_vaos));
  g_bound_vao = 0;
  init_vao(&g_vaos[0], true);

  memset(g_current_attrib_i, 0, sizeof(g_current_attrib_i));
  memset(g_current_attrib_ui, 0, sizeof(g_current_attrib_ui));
  for (GLuint i = 0; i < GL33_MAX_VERTEX_ATTRIBS; ++i) {
    g_current_attrib_i[i][3] = 1;
    g_current_attrib_ui[i][3] = 1;
  }
}

void gl_vao_shutdown(void) {
  memset(g_vaos, 0, sizeof(g_vaos));
  memset(g_current_attrib_i, 0, sizeof(g_current_attrib_i));
  memset(g_current_attrib_ui, 0, sizeof(g_current_attrib_ui));
  g_bound_vao = 0;
}

#ifdef __cplusplus
extern "C" {
#endif

void _gl_GenVertexArrays(GLsizei n, GLuint *arrays) {
  GLsizei count = 0;

  if (n < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (n > 0 && !arrays) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  for (GLuint id = 1; id < MAX_VAOS && count < n; ++id) {
    if (!g_vaos[id].reserved && !g_vaos[id].in_use) {
      init_vao(&g_vaos[id], false);
      g_vaos[id].reserved = true;
      arrays[count++] = id;
    }
  }

  if (count < n) {
    for (GLsizei i = count; i < n; ++i) arrays[i] = 0;
    _gl_set_error(GL_OUT_OF_MEMORY);
  }
}

void _gl_DeleteVertexArrays(GLsizei n, const GLuint *arrays) {
  if (n < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (n > 0 && !arrays) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  for (GLsizei i = 0; i < n; ++i) {
    GLuint id = arrays[i];
    if (id == 0 || id >= MAX_VAOS) continue;
    if (!g_vaos[id].reserved && !g_vaos[id].in_use) continue;

    if (g_gl_context && g_bound_vao == id) {
      g_bound_vao = 0;
      g_gl_context->bound_vao = 0;
      g_gl_context->dirty_flags |= GL_DIRTY_VAO;
    }
    memset(&g_vaos[id], 0, sizeof(g_vaos[id]));
  }
}

GLboolean _gl_IsVertexArray(GLuint array) {
  if (array == 0 || array >= MAX_VAOS) return GL_FALSE;
  return g_vaos[array].in_use ? GL_TRUE : GL_FALSE;
}

void _gl_BindVertexArray(GLuint array) {
  if (!g_gl_context) return;
  if (array >= MAX_VAOS) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (array != 0 && !g_vaos[array].reserved && !g_vaos[array].in_use) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  if (array != 0 && !g_vaos[array].in_use) {
    init_vao(&g_vaos[array], true);
  }

  g_bound_vao = array;
  g_gl_context->bound_vao = array;
  g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

void _gl_EnableVertexAttribArray(GLuint index) {
  GLVAO *vao;

  if (!g_gl_context) return;
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  vao = current_vao(true);
  if (!vao) return;

  vao->attribs[index].enabled = GL_TRUE;
  mark_vao_dirty(vao);
}

void _gl_DisableVertexAttribArray(GLuint index) {
  GLVAO *vao;

  if (!g_gl_context) return;
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  vao = current_vao(true);
  if (!vao) return;

  vao->attribs[index].enabled = GL_FALSE;
  mark_vao_dirty(vao);
}

void _gl_GetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params) {
  GLVAO *vao;
  GLVertexAttrib *attrib;

  if (!g_gl_context || !params) return;
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  if (pname == GL_CURRENT_VERTEX_ATTRIB) {
    memcpy(params, g_gl_context->current_vertex_attrib[index],
           4 * sizeof(GLfloat));
    return;
  }

  vao = current_vao(true);
  if (!vao) return;
  attrib = &vao->attribs[index];

  switch (pname) {
  case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
    *params = (GLfloat)attrib->enabled;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_SIZE:
    *params = (GLfloat)attrib->size;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
    *params = (GLfloat)attrib->stride;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_TYPE:
    *params = (GLfloat)attrib->type;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
    *params = (GLfloat)attrib->normalized;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
    *params = (GLfloat)attrib->integer;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
    *params = (GLfloat)attrib->buffer;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
    *params = (GLfloat)attrib->divisor;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_GetVertexAttribiv(GLuint index, GLenum pname, GLint *params) {
  GLVAO *vao;
  GLVertexAttrib *attrib;

  if (!g_gl_context || !params) return;
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  if (pname == GL_CURRENT_VERTEX_ATTRIB) {
    params[0] = (GLint)g_gl_context->current_vertex_attrib[index][0];
    params[1] = (GLint)g_gl_context->current_vertex_attrib[index][1];
    params[2] = (GLint)g_gl_context->current_vertex_attrib[index][2];
    params[3] = (GLint)g_gl_context->current_vertex_attrib[index][3];
    return;
  }

  vao = current_vao(true);
  if (!vao) return;
  attrib = &vao->attribs[index];

  switch (pname) {
  case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
    *params = attrib->enabled;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_SIZE:
    *params = attrib->size;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
    *params = attrib->stride;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_TYPE:
    *params = (GLint)attrib->type;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
    *params = attrib->normalized;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
    *params = attrib->integer;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
    *params = (GLint)attrib->buffer;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
    *params = (GLint)attrib->divisor;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_GetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid **pointer) {
  GLVAO *vao;

  if (!g_gl_context || !pointer) return;
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (pname != GL_VERTEX_ATTRIB_ARRAY_POINTER) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  vao = current_vao(true);
  if (!vao) return;
  *pointer = (GLvoid *)vao->attribs[index].pointer;
}

void _gl_VertexAttribPointer(GLuint index, GLint size, GLenum type,
                             GLboolean normalized, GLsizei stride,
                             const GLvoid *pointer) {
  store_pointer(index, size, type, normalized, stride, pointer, GL_FALSE);
}

void _gl_VertexAttribIPointer(GLuint index, GLint size, GLenum type,
                              GLsizei stride, const GLvoid *pointer) {
  store_pointer(index, size, type, GL_FALSE, stride, pointer, GL_TRUE);
}

void _gl_GetVertexAttribIiv(GLuint index, GLenum pname, GLint *params) {
  if (!g_gl_context || !params) return;
  if (pname != GL_CURRENT_VERTEX_ATTRIB) {
    _gl_GetVertexAttribiv(index, pname, params);
    return;
  }
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  memcpy(params, g_current_attrib_i[index], 4 * sizeof(GLint));
}

void _gl_GetVertexAttribIuiv(GLuint index, GLenum pname, GLuint *params) {
  GLVAO *vao;
  GLVertexAttrib *attrib;

  if (!g_gl_context || !params) return;
  if (pname != GL_CURRENT_VERTEX_ATTRIB) {
    if (index >= GL33_MAX_VERTEX_ATTRIBS) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    vao = current_vao(true);
    if (!vao) return;
    attrib = &vao->attribs[index];
    switch (pname) {
    case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
      *params = attrib->enabled;
      break;
    case GL_VERTEX_ATTRIB_ARRAY_SIZE:
      *params = (GLuint)attrib->size;
      break;
    case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
      *params = (GLuint)attrib->stride;
      break;
    case GL_VERTEX_ATTRIB_ARRAY_TYPE:
      *params = attrib->type;
      break;
    case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
      *params = attrib->normalized;
      break;
    case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
      *params = attrib->integer;
      break;
    case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
      *params = attrib->buffer;
      break;
    case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
      *params = attrib->divisor;
      break;
    default:
      _gl_set_error(GL_INVALID_ENUM);
      break;
    }
    return;
  }
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  memcpy(params, g_current_attrib_ui[index], 4 * sizeof(GLuint));
}

void _gl_VertexAttrib1f(GLuint index, GLfloat x) {
  set_current_float(index, x, 0.0f, 0.0f, 1.0f);
}

void _gl_VertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
  set_current_float(index, x, y, 0.0f, 1.0f);
}

void _gl_VertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
  set_current_float(index, x, y, z, 1.0f);
}

void _gl_VertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                        GLfloat w) {
  set_current_float(index, x, y, z, w);
}

void _gl_VertexAttrib1fv(GLuint index, const GLfloat *v) {
  if (!v) return;
  _gl_VertexAttrib1f(index, v[0]);
}

void _gl_VertexAttrib2fv(GLuint index, const GLfloat *v) {
  if (!v) return;
  _gl_VertexAttrib2f(index, v[0], v[1]);
}

void _gl_VertexAttrib3fv(GLuint index, const GLfloat *v) {
  if (!v) return;
  _gl_VertexAttrib3f(index, v[0], v[1], v[2]);
}

void _gl_VertexAttrib4fv(GLuint index, const GLfloat *v) {
  if (!v) return;
  _gl_VertexAttrib4f(index, v[0], v[1], v[2], v[3]);
}

void _gl_VertexAttribI1i(GLuint index, GLint x) {
  set_current_signed(index, x, 0, 0, 1);
}

void _gl_VertexAttribI2i(GLuint index, GLint x, GLint y) {
  set_current_signed(index, x, y, 0, 1);
}

void _gl_VertexAttribI3i(GLuint index, GLint x, GLint y, GLint z) {
  set_current_signed(index, x, y, z, 1);
}

void _gl_VertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) {
  set_current_signed(index, x, y, z, w);
}

void _gl_VertexAttribI1ui(GLuint index, GLuint x) {
  set_current_unsigned(index, x, 0, 0, 1);
}

void _gl_VertexAttribI2ui(GLuint index, GLuint x, GLuint y) {
  set_current_unsigned(index, x, y, 0, 1);
}

void _gl_VertexAttribI3ui(GLuint index, GLuint x, GLuint y, GLuint z) {
  set_current_unsigned(index, x, y, z, 1);
}

void _gl_VertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z,
                          GLuint w) {
  set_current_unsigned(index, x, y, z, w);
}

void _gl_VertexAttribI1iv(GLuint index, const GLint *v) {
  if (v) _gl_VertexAttribI1i(index, v[0]);
}

void _gl_VertexAttribI2iv(GLuint index, const GLint *v) {
  if (v) _gl_VertexAttribI2i(index, v[0], v[1]);
}

void _gl_VertexAttribI3iv(GLuint index, const GLint *v) {
  if (v) _gl_VertexAttribI3i(index, v[0], v[1], v[2]);
}

void _gl_VertexAttribI4iv(GLuint index, const GLint *v) {
  if (v) _gl_VertexAttribI4i(index, v[0], v[1], v[2], v[3]);
}

void _gl_VertexAttribI1uiv(GLuint index, const GLuint *v) {
  if (v) _gl_VertexAttribI1ui(index, v[0]);
}

void _gl_VertexAttribI2uiv(GLuint index, const GLuint *v) {
  if (v) _gl_VertexAttribI2ui(index, v[0], v[1]);
}

void _gl_VertexAttribI3uiv(GLuint index, const GLuint *v) {
  if (v) _gl_VertexAttribI3ui(index, v[0], v[1], v[2]);
}

void _gl_VertexAttribI4uiv(GLuint index, const GLuint *v) {
  if (v) _gl_VertexAttribI4ui(index, v[0], v[1], v[2], v[3]);
}

void _gl_VertexAttribDivisor(GLuint index, GLuint divisor) {
  GLVAO *vao;

  if (!g_gl_context) return;
  if (index >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  vao = current_vao(true);
  if (!vao) return;

  vao->attribs[index].divisor = divisor;
  mark_vao_dirty(vao);
}

void gl_bind_vao(void) {
  GLVAO *vao = current_vao(false);
  if (vao) vao->dirty = false;
}

GLboolean gl_vao_has_bound_array(void) {
  return current_vao(false) ? GL_TRUE : GL_FALSE;
}

void gl_vao_set_element_array_buffer(GLuint buffer) {
  GLVAO *vao;

  if (!g_gl_context) return;
  vao = current_vao(true);
  if (!vao) return;

  vao->element_array_buffer = buffer;
  mark_vao_dirty(vao);
}

void gl_vao_unbind_buffer(GLuint buffer) {
  if (buffer == 0) return;

  for (GLuint vao_id = 0; vao_id < MAX_VAOS; ++vao_id) {
    GLVAO *vao = &g_vaos[vao_id];
    if (!vao->in_use) continue;
    if (vao->element_array_buffer == buffer) {
      vao->element_array_buffer = 0;
      vao->dirty = true;
    }
    for (GLuint attrib = 0; attrib < GL33_MAX_VERTEX_ATTRIBS; ++attrib) {
      if (vao->attribs[attrib].buffer == buffer) {
        vao->attribs[attrib].buffer = 0;
        vao->dirty = true;
      }
    }
  }

  if (g_gl_context) g_gl_context->dirty_flags |= GL_DIRTY_VAO;
}

GLuint gl_vao_get_element_array_buffer(void) {
  GLVAO *vao = current_vao(false);
  return vao ? vao->element_array_buffer : 0;
}

GLuint gl_vao_get_bound_vertex_array(void) {
  return g_bound_vao;
}

GLboolean gl_vao_get_attrib_state(GLuint index, gl_vao_attrib_state_t *state) {
  GLVAO *vao;
  GLVertexAttrib *attrib;

  if (!state || index >= GL33_MAX_VERTEX_ATTRIBS) return GL_FALSE;

  vao = current_vao(false);
  if (!vao) return GL_FALSE;

  attrib = &vao->attribs[index];
  memset(state, 0, sizeof(*state));
  state->buffer = attrib->buffer;
  state->size = attrib->size;
  state->type = attrib->type;
  state->normalized = attrib->normalized;
  state->enabled = attrib->enabled;
  state->integer_input = attrib->integer;
  state->stride = attrib->stride;
  state->pointer = attrib->pointer;
  state->divisor = attrib->divisor;
  return GL_TRUE;
}

#ifdef __cplusplus
}
#endif
