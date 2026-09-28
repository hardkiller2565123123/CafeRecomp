#include "gl_draw.h"
#include "state/gl_state.h"
#include "core/gl_context.h"
#include "core/gl_vao.h"
#include "core/gl_buffer.h"
#include "core/gl_framebuffer.h"
#include <gx2/draw.h>
#include <gx2/enum.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  GX2IndexType gx2_type;
  uint32_t element_size;
  bool widen_u8;
} IndexTypeInfo;

typedef struct {
  const void *indices;
  uint16_t *owned_indices;
  GX2IndexType gx2_type;
} PreparedIndices;

static bool map_draw_mode(GLenum mode, GX2PrimitiveMode *prim) {
  if (!prim) return false;

  switch (mode) {
  case GL_POINTS:
    *prim = GX2_PRIMITIVE_MODE_POINTS;
    return true;
  case GL_LINES:
    *prim = GX2_PRIMITIVE_MODE_LINES;
    return true;
  case GL_LINE_LOOP:
    *prim = GX2_PRIMITIVE_MODE_LINE_LOOP;
    return true;
  case GL_LINE_STRIP:
    *prim = GX2_PRIMITIVE_MODE_LINE_STRIP;
    return true;
  case GL_TRIANGLES:
    *prim = GX2_PRIMITIVE_MODE_TRIANGLES;
    return true;
  case GL_TRIANGLE_STRIP:
    *prim = GX2_PRIMITIVE_MODE_TRIANGLE_STRIP;
    return true;
  case GL_TRIANGLE_FAN:
    *prim = GX2_PRIMITIVE_MODE_TRIANGLE_FAN;
    return true;
  case GL_LINES_ADJACENCY:
    *prim = GX2_PRIMITIVE_MODE_LINES_ADJACENCY;
    return true;
  case GL_LINE_STRIP_ADJACENCY:
    *prim = GX2_PRIMITIVE_MODE_LINE_STRIP_ADJACENCY;
    return true;
  case GL_TRIANGLES_ADJACENCY:
    *prim = GX2_PRIMITIVE_MODE_TRIANGLES_ADJACENCY;
    return true;
  case GL_TRIANGLE_STRIP_ADJACENCY:
    *prim = GX2_PRIMITIVE_MODE_TRIANGLE_STRIP_ADJACENCY;
    return true;
  default:
    return false;
  }
}

static bool get_index_type(GLenum type, IndexTypeInfo *info) {
  if (!info) return false;

  switch (type) {
  case GL_UNSIGNED_BYTE:
    info->gx2_type = GX2_INDEX_TYPE_U16;
    info->element_size = 1;
    info->widen_u8 = true;
    return true;
  case GL_UNSIGNED_SHORT:
    info->gx2_type = GX2_INDEX_TYPE_U16;
    info->element_size = 2;
    info->widen_u8 = false;
    return true;
  case GL_UNSIGNED_INT:
    info->gx2_type = GX2_INDEX_TYPE_U32;
    info->element_size = 4;
    info->widen_u8 = false;
    return true;
  default:
    return false;
  }
}

static bool multiply_u32(GLsizei count, uint32_t size, uint32_t *bytes) {
  uint64_t result;

  if (!bytes || count < 0) return false;
  result = (uint64_t)(uint32_t)count * (uint64_t)size;
  if (result > UINT32_MAX) return false;
  *bytes = (uint32_t)result;
  return true;
}

static bool validate_common_draw_state(GLenum mode, GX2PrimitiveMode *prim) {
  GLenum framebuffer_status;

  if (!g_gl_context) return false;
  if (!map_draw_mode(mode, prim)) {
    _gl_set_error(GL_INVALID_ENUM);
    return false;
  }
  if (!g_gl_context->bound_program || !gl_vao_has_bound_array()) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }
  if (gl_transform_feedback_validate_draw_mode(mode) != GL_TRUE) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  framebuffer_status = _gl_CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
  if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE) {
    _gl_set_error(GL_INVALID_FRAMEBUFFER_OPERATION);
    return false;
  }

  return true;
}

static bool validate_mapped_buffers(GLuint element_buffer) {
  if (!g_gl_context) return false;

  for (GLuint index = 0; index < GL33_MAX_VERTEX_ATTRIBS; ++index) {
    gl_vao_attrib_state_t state;
    if (!gl_vao_get_attrib_state(index, &state)) return false;
    if (!state.enabled) continue;
    if (state.buffer == 0 ||
        gl_buffer_is_mapped(state.buffer) == GL_TRUE) {
      _gl_set_error(GL_INVALID_OPERATION);
      return false;
    }
  }

  if (element_buffer != 0 &&
      gl_buffer_is_mapped(element_buffer) == GL_TRUE) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  for (uint32_t i = 0; i < GL33_MAX_UNIFORM_BUFFER_BINDINGS; ++i) {
    GLuint buffer = g_gl_context->uniform_buffer_bindings[i].buffer;
    if (buffer != 0 && gl_buffer_is_mapped(buffer) == GL_TRUE) {
      _gl_set_error(GL_INVALID_OPERATION);
      return false;
    }
  }

  return true;
}

static bool validate_array_args(GLint first, GLsizei count,
                                GLsizei instancecount) {
  if (first < 0 || count < 0 || instancecount < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }
  return true;
}

static bool validate_element_args(GLsizei count, GLenum type,
                                  GLsizei instancecount,
                                  IndexTypeInfo *index_info) {
  if (count < 0 || instancecount < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }
  if (!get_index_type(type, index_info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return false;
  }
  return true;
}

static bool validate_range_args(GLuint start, GLuint end, GLsizei count) {
  if (count < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }
  if (end < start) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }
  return true;
}

static bool prepare_indices(GLsizei count, const GLvoid *indices,
                            const IndexTypeInfo *index_info,
                            PreparedIndices *prepared) {
  GLuint element_buffer;
  void *buffer_data;
  GLsizeiptr buffer_size;
  uint32_t offset;
  uint32_t byte_count;

  if (!index_info || !prepared) return false;
  prepared->indices = NULL;
  prepared->owned_indices = NULL;
  prepared->gx2_type = index_info->gx2_type;

  element_buffer = gl_vao_get_element_array_buffer();
  if (element_buffer == 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  if (!multiply_u32(count, index_info->element_size, &byte_count)) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }

  buffer_size = gl_buffer_get_size(element_buffer);
  offset = (uint32_t)(uintptr_t)indices;
  if ((uint64_t)offset + (uint64_t)byte_count > (uint64_t)buffer_size) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  if (!validate_mapped_buffers(element_buffer)) return false;

  buffer_data = gl_buffer_get_data(element_buffer);
  if (!buffer_data && byte_count > 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  if (index_info->widen_u8 && count > 0) {
    const uint8_t *src = (const uint8_t *)buffer_data + offset;
    uint16_t *dst = (uint16_t *)malloc((size_t)count * sizeof(uint16_t));
    if (!dst) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return false;
    }
    for (GLsizei i = 0; i < count; ++i) {
      dst[i] = src[i];
    }
    prepared->indices = dst;
    prepared->owned_indices = dst;
  } else {
    prepared->indices = (const uint8_t *)buffer_data + offset;
  }

  return true;
}

static bool validate_index_request(GLsizei count, const GLvoid *indices,
                                   const IndexTypeInfo *index_info) {
  GLuint element_buffer;
  void *buffer_data;
  GLsizeiptr buffer_size;
  uint32_t offset;
  uint32_t byte_count;

  if (!index_info) return false;

  element_buffer = gl_vao_get_element_array_buffer();
  if (element_buffer == 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  if (!multiply_u32(count, index_info->element_size, &byte_count)) {
    _gl_set_error(GL_INVALID_VALUE);
    return false;
  }

  buffer_size = gl_buffer_get_size(element_buffer);
  offset = (uint32_t)(uintptr_t)indices;
  if ((uint64_t)offset + (uint64_t)byte_count > (uint64_t)buffer_size) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  if (!validate_mapped_buffers(element_buffer)) return false;

  buffer_data = gl_buffer_get_data(element_buffer);
  if (!buffer_data && byte_count > 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  return true;
}

static void release_indices(PreparedIndices *prepared) {
  if (!prepared) return;
  free(prepared->owned_indices);
  prepared->owned_indices = NULL;
  prepared->indices = NULL;
}

static void set_restart_index_for_draw(void) {
  GX2SetPrimitiveRestartIndex(g_gl_context && g_gl_context->primitive_restart_enabled
                                  ? g_gl_context->primitive_restart_index
                                  : 0xFFFFFFFFu);
}

static void draw_arrays_checked(GX2PrimitiveMode prim, GLint first,
                                GLsizei count, GLsizei instancecount) {
  if (count == 0 || instancecount == 0) return;
  set_restart_index_for_draw();
  GX2DrawEx(prim, (uint32_t)count, (uint32_t)first, (uint32_t)instancecount);
  gl_framebuffer_mark_bound_color_dirty();
}

static bool draw_elements_checked(GX2PrimitiveMode prim, GLsizei count,
                                  const IndexTypeInfo *index_info,
                                  const GLvoid *indices,
                                  GLsizei instancecount,
                                  GLint basevertex) {
  PreparedIndices prepared;

  if (count == 0 || instancecount == 0) return true;
  if (!prepare_indices(count, indices, index_info, &prepared)) return false;

  set_restart_index_for_draw();
  if (index_info->widen_u8) {
    GX2DrawIndexedImmediateEx(prim, (uint32_t)count, prepared.gx2_type,
                              prepared.indices, (uint32_t)basevertex,
                              (uint32_t)instancecount);
  } else {
    GX2DrawIndexedEx(prim, (uint32_t)count, prepared.gx2_type,
                     prepared.indices, (uint32_t)basevertex,
                     (uint32_t)instancecount);
  }
  release_indices(&prepared);
  gl_framebuffer_mark_bound_color_dirty();
  return true;
}

void _gl_DrawArrays(GLenum mode, GLint first, GLsizei count) {
  _gl_DrawArraysInstanced(mode, first, count, 1);
}

void _gl_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                             GLsizei instancecount) {
  GX2PrimitiveMode prim;

  if (!validate_array_args(first, count, instancecount)) return;
  if (!validate_common_draw_state(mode, &prim)) return;
  if (count == 0 || instancecount == 0) return;
  if (!validate_mapped_buffers(0)) return;

  gl_flush_state();
  draw_arrays_checked(prim, first, count, instancecount);
}

void _gl_DrawElements(GLenum mode, GLsizei count, GLenum type,
                      const GLvoid *indices) {
  _gl_DrawElementsInstanced(mode, count, type, indices, 1);
}

void _gl_DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                               const GLvoid *indices,
                               GLsizei instancecount) {
  GX2PrimitiveMode prim;
  IndexTypeInfo index_info;

  if (!validate_element_args(count, type, instancecount, &index_info)) return;
  if (!validate_common_draw_state(mode, &prim)) return;
  if (count == 0 || instancecount == 0) return;
  if (!validate_index_request(count, indices, &index_info)) return;

  gl_flush_state();
  draw_elements_checked(prim, count, &index_info, indices, instancecount, 0);
}

void _gl_DrawRangeElements(GLenum mode, GLuint start, GLuint end,
                           GLsizei count, GLenum type,
                           const GLvoid *indices) {
  if (!validate_range_args(start, end, count)) return;
  _gl_DrawElements(mode, count, type, indices);
}

void _gl_DrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type,
                                const GLvoid *indices, GLint basevertex) {
  GX2PrimitiveMode prim;
  IndexTypeInfo index_info;

  if (!validate_element_args(count, type, 1, &index_info)) return;
  if (!validate_common_draw_state(mode, &prim)) return;
  if (count == 0) return;
  if (!validate_index_request(count, indices, &index_info)) return;

  gl_flush_state();
  draw_elements_checked(prim, count, &index_info, indices, 1, basevertex);
}

void _gl_DrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end,
                                     GLsizei count, GLenum type,
                                     const GLvoid *indices,
                                     GLint basevertex) {
  if (!validate_range_args(start, end, count)) return;
  _gl_DrawElementsBaseVertex(mode, count, type, indices, basevertex);
}

void _gl_DrawElementsInstancedBaseVertex(GLenum mode, GLsizei count,
                                         GLenum type, const GLvoid *indices,
                                         GLsizei instancecount,
                                         GLint basevertex) {
  GX2PrimitiveMode prim;
  IndexTypeInfo index_info;

  if (!validate_element_args(count, type, instancecount, &index_info)) return;
  if (!validate_common_draw_state(mode, &prim)) return;
  if (count == 0 || instancecount == 0) return;
  if (!validate_index_request(count, indices, &index_info)) return;

  gl_flush_state();
  draw_elements_checked(prim, count, &index_info, indices, instancecount,
                        basevertex);
}

void _gl_MultiDrawArrays(GLenum mode, const GLint *first,
                         const GLsizei *count, GLsizei drawcount) {
  GX2PrimitiveMode prim;

  if (drawcount < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (drawcount == 0) return;
  if (!first || !count) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  for (GLsizei i = 0; i < drawcount; ++i) {
    if (!validate_array_args(first[i], count[i], 1)) return;
  }
  if (!validate_common_draw_state(mode, &prim)) return;
  if (!validate_mapped_buffers(0)) return;

  gl_flush_state();
  for (GLsizei i = 0; i < drawcount; ++i) {
    draw_arrays_checked(prim, first[i], count[i], 1);
  }
}

void _gl_MultiDrawElements(GLenum mode, const GLsizei *count, GLenum type,
                           const GLvoid *const *indices,
                           GLsizei drawcount) {
  _gl_MultiDrawElementsBaseVertex(mode, count, type, indices, drawcount, NULL);
}

void _gl_MultiDrawElementsBaseVertex(GLenum mode, const GLsizei *count,
                                     GLenum type,
                                     const GLvoid *const *indices,
                                     GLsizei drawcount,
                                     const GLint *basevertex) {
  GX2PrimitiveMode prim;
  IndexTypeInfo index_info;

  if (drawcount < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (drawcount == 0) return;
  if (!count || !indices) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_index_type(type, &index_info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  for (GLsizei i = 0; i < drawcount; ++i) {
    if (count[i] < 0) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
  }
  if (!validate_common_draw_state(mode, &prim)) return;
  for (GLsizei i = 0; i < drawcount; ++i) {
    if (count[i] == 0) continue;
    if (!validate_index_request(count[i], indices[i], &index_info)) return;
  }

  gl_flush_state();
  for (GLsizei i = 0; i < drawcount; ++i) {
    if (!draw_elements_checked(prim, count[i], &index_info, indices[i], 1,
                               basevertex ? basevertex[i] : 0)) {
      return;
    }
  }
}

void _gl_PrimitiveRestartIndex(GLuint index) {
  if (!g_gl_context) return;
  g_gl_context->primitive_restart_index = index;
  g_gl_context->dirty_flags |= GL_DIRTY_PRIMITIVE_RESTART;
}

#ifdef __cplusplus
}
#endif
