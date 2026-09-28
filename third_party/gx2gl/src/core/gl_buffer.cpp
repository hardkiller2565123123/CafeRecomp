#include "gl_buffer.h"
#include "gl_vao.h"
#include "endian/endian.h"

#include <gx2r/buffer.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_BUFFERS 1024

typedef struct {
  bool reserved;
  bool in_use;
  GLsizeiptr size;
  GLenum usage;

  GX2RBuffer raw_buffer;
  bool raw_owned;
  GX2RBuffer uniform_buffer;
  bool uniform_owned;

  uint8_t *shadow;
  void *mapped_ptr;
  GLenum mapped_access;
  GLbitfield mapped_access_flags;
  GLintptr mapped_offset;
  GLsizeiptr mapped_length;
} GLBuffer;

static GLBuffer g_buffers[MAX_BUFFERS];
static uint8_t g_empty_mapping_byte;
static void release_buffer_storage(GLBuffer *buf);

static const GX2RResourceFlags kRawBufferFlags =
    (GX2RResourceFlags)(GX2R_RESOURCE_BIND_VERTEX_BUFFER |
                        GX2R_RESOURCE_BIND_INDEX_BUFFER |
                        GX2R_RESOURCE_BIND_STREAM_OUTPUT |
                        GX2R_RESOURCE_USAGE_CPU_WRITE |
                        GX2R_RESOURCE_USAGE_GPU_READ |
                        GX2R_RESOURCE_USAGE_FORCE_MEM2);

static const GX2RResourceFlags kUniformBufferFlags =
    (GX2RResourceFlags)(GX2R_RESOURCE_BIND_UNIFORM_BLOCK |
                        GX2R_RESOURCE_USAGE_CPU_WRITE |
                        GX2R_RESOURCE_USAGE_GPU_READ |
                        GX2R_RESOURCE_USAGE_FORCE_MEM2);

static void reset_mapping_state(GLBuffer *buf) {
  if (!buf) return;
  buf->mapped_ptr = NULL;
  buf->mapped_access = GL_READ_WRITE;
  buf->mapped_access_flags = 0;
  buf->mapped_offset = 0;
  buf->mapped_length = 0;
}

void gl_buffer_init(void) {
  memset(g_buffers, 0, sizeof(g_buffers));
  for (uint32_t i = 0; i < MAX_BUFFERS; ++i) {
    reset_mapping_state(&g_buffers[i]);
    g_buffers[i].usage = GL_STATIC_DRAW;
  }
}

void gl_buffer_shutdown(void) {
  for (uint32_t i = 1; i < MAX_BUFFERS; ++i) {
    release_buffer_storage(&g_buffers[i]);
  }
  memset(g_buffers, 0, sizeof(g_buffers));
}

static bool is_buffer_target(GLenum target) {
  switch (target) {
  case GL_ARRAY_BUFFER:
  case GL_COPY_READ_BUFFER:
  case GL_COPY_WRITE_BUFFER:
  case GL_ELEMENT_ARRAY_BUFFER:
  case GL_PIXEL_PACK_BUFFER:
  case GL_PIXEL_UNPACK_BUFFER:
  case GL_TEXTURE_BUFFER:
  case GL_TRANSFORM_FEEDBACK_BUFFER:
  case GL_UNIFORM_BUFFER:
    return true;
  default:
    return false;
  }
}

static bool is_buffer_usage(GLenum usage) {
  switch (usage) {
  case GL_STREAM_DRAW:
  case GL_STREAM_READ:
  case GL_STREAM_COPY:
  case GL_STATIC_DRAW:
  case GL_STATIC_READ:
  case GL_STATIC_COPY:
  case GL_DYNAMIC_DRAW:
  case GL_DYNAMIC_READ:
  case GL_DYNAMIC_COPY:
    return true;
  default:
    return false;
  }
}

static bool range_in_buffer(GLintptr offset, GLsizeiptr size,
                            GLsizeiptr buffer_size) {
  if (offset < 0 || size < 0) return false;
  return (uint64_t)offset <= (uint64_t)buffer_size &&
         (uint64_t)size <= (uint64_t)buffer_size - (uint64_t)offset;
}

static bool ranges_overlap(GLintptr a_offset, GLsizeiptr a_size,
                           GLintptr b_offset, GLsizeiptr b_size) {
  uint64_t a0 = (uint64_t)a_offset;
  uint64_t a1 = a0 + (uint64_t)a_size;
  uint64_t b0 = (uint64_t)b_offset;
  uint64_t b1 = b0 + (uint64_t)b_size;
  return a0 < b1 && b0 < a1;
}

static bool create_gx2r_buffer(GX2RBuffer *gx2, GLsizeiptr size,
                               GX2RResourceFlags flags) {
  if (!gx2 || size < 0 || (uint64_t)size > UINT32_MAX) return false;
  memset(gx2, 0, sizeof(*gx2));
  if (size == 0) return true;

  gx2->flags = flags;
  gx2->elemSize = 1;
  gx2->elemCount = (uint32_t)size;
  if (!GX2RCreateBuffer(gx2)) {
    memset(gx2, 0, sizeof(*gx2));
    return false;
  }
  return true;
}

static void destroy_gx2r_buffer(GX2RBuffer *gx2, bool *owned) {
  if (!gx2 || !owned || !*owned) return;
  GX2RDestroyBufferEx(gx2, (GX2RResourceFlags)0);
  memset(gx2, 0, sizeof(*gx2));
  *owned = false;
}

static void release_buffer_storage(GLBuffer *buf) {
  if (!buf) return;
  destroy_gx2r_buffer(&buf->raw_buffer, &buf->raw_owned);
  destroy_gx2r_buffer(&buf->uniform_buffer, &buf->uniform_owned);
  free(buf->shadow);
  buf->shadow = NULL;
  buf->size = 0;
  buf->usage = GL_STATIC_DRAW;
  reset_mapping_state(buf);
}

static GLBuffer *get_buffer(GLuint id) {
  if (id == 0 || id >= MAX_BUFFERS || !g_buffers[id].in_use) return NULL;
  return &g_buffers[id];
}

static bool ensure_buffer_object(GLuint id) {
  GLBuffer *buf;

  if (id == 0 || id >= MAX_BUFFERS) return false;
  buf = &g_buffers[id];
  if (buf->in_use) return true;
  if (!buf->reserved) return false;

  memset(buf, 0, sizeof(*buf));
  buf->in_use = true;
  buf->reserved = false;
  buf->usage = GL_STATIC_DRAW;
  reset_mapping_state(buf);
  return true;
}

static GLuint get_bound_buffer(GLenum target) {
  if (!g_gl_context) return 0;
  switch (target) {
  case GL_ARRAY_BUFFER:
    return g_gl_context->bound_array_buffer;
  case GL_COPY_READ_BUFFER:
    return g_gl_context->bound_copy_read_buffer;
  case GL_COPY_WRITE_BUFFER:
    return g_gl_context->bound_copy_write_buffer;
  case GL_ELEMENT_ARRAY_BUFFER:
    return gl_vao_get_element_array_buffer();
  case GL_PIXEL_PACK_BUFFER:
    return g_gl_context->bound_pixel_pack_buffer;
  case GL_PIXEL_UNPACK_BUFFER:
    return g_gl_context->bound_pixel_unpack_buffer;
  case GL_TEXTURE_BUFFER:
    return g_gl_context->bound_texture_buffer;
  case GL_TRANSFORM_FEEDBACK_BUFFER:
    return g_gl_context->bound_transform_feedback_buffer;
  case GL_UNIFORM_BUFFER:
    return g_gl_context->bound_uniform_buffer;
  default:
    return 0;
  }
}

static void set_bound_buffer(GLenum target, GLuint buffer) {
  if (!g_gl_context) return;
  switch (target) {
  case GL_ARRAY_BUFFER:
    g_gl_context->bound_array_buffer = buffer;
    break;
  case GL_COPY_READ_BUFFER:
    g_gl_context->bound_copy_read_buffer = buffer;
    break;
  case GL_COPY_WRITE_BUFFER:
    g_gl_context->bound_copy_write_buffer = buffer;
    break;
  case GL_ELEMENT_ARRAY_BUFFER:
    gl_vao_set_element_array_buffer(buffer);
    break;
  case GL_PIXEL_PACK_BUFFER:
    g_gl_context->bound_pixel_pack_buffer = buffer;
    break;
  case GL_PIXEL_UNPACK_BUFFER:
    g_gl_context->bound_pixel_unpack_buffer = buffer;
    break;
  case GL_TEXTURE_BUFFER:
    g_gl_context->bound_texture_buffer = buffer;
    break;
  case GL_TRANSFORM_FEEDBACK_BUFFER:
    g_gl_context->bound_transform_feedback_buffer = buffer;
    break;
  case GL_UNIFORM_BUFFER:
    g_gl_context->bound_uniform_buffer = buffer;
    break;
  default:
    break;
  }
}

static bool valid_buffer_name_for_bind(GLuint buffer) {
  if (buffer == 0) return true;
  if (buffer >= MAX_BUFFERS) return false;
  return g_buffers[buffer].in_use || g_buffers[buffer].reserved;
}

static bool sync_raw_range(GLBuffer *buf, GLintptr offset, GLsizeiptr size) {
  uint8_t *dst;

  if (!buf || size == 0) return true;
  if (!buf->raw_owned || !buf->raw_buffer.buffer || !buf->shadow) return false;
  if (!range_in_buffer(offset, size, buf->size)) return false;

  dst = (uint8_t *)GX2RLockBufferEx(&buf->raw_buffer, GX2R_RESOURCE_USAGE_CPU_WRITE);
  if (!dst) return false;

  memcpy(dst + offset, buf->shadow + offset, (size_t)size);
  GX2RUnlockBufferEx(&buf->raw_buffer, GX2R_RESOURCE_USAGE_CPU_WRITE);
  GX2RInvalidateBuffer(&buf->raw_buffer,
                       (GX2RResourceFlags)(GX2R_RESOURCE_USAGE_CPU_WRITE |
                                           GX2R_RESOURCE_USAGE_GPU_READ));
  return true;
}

static bool ensure_uniform_storage(GLBuffer *buf) {
  if (!buf) return false;
  if (buf->size == 0) return true;
  if (buf->uniform_owned && buf->uniform_buffer.buffer) return true;

  if (!create_gx2r_buffer(&buf->uniform_buffer, buf->size, kUniformBufferFlags)) {
    return false;
  }
  buf->uniform_owned = true;
  return true;
}

static bool sync_uniform_range(GLBuffer *buf, GLintptr offset, GLsizeiptr size) {
  GLintptr start;
  GLintptr end;
  GLintptr full_word_bytes;
  uint8_t *dst;

  if (!buf || size == 0) return true;
  if (!buf->shadow || !range_in_buffer(offset, size, buf->size)) return false;
  if (!ensure_uniform_storage(buf)) return false;

  start = offset & ~((GLintptr)3);
  end = offset + size;
  if (end < buf->size) end = (end + 3) & ~((GLintptr)3);
  else end = buf->size;

  dst = (uint8_t *)GX2RLockBufferEx(&buf->uniform_buffer,
                                    GX2R_RESOURCE_USAGE_CPU_WRITE);
  if (!dst) return false;

  full_word_bytes = (end - start) & ~((GLintptr)3);
  for (GLintptr i = 0; i < full_word_bytes; i += 4) {
    uint32_t value;
    memcpy(&value, buf->shadow + start + i, sizeof(value));
    value = CPU_TO_GPU_32(value);
    memcpy(dst + start + i, &value, sizeof(value));
  }
  if (full_word_bytes < end - start) {
    memcpy(dst + start + full_word_bytes,
           buf->shadow + start + full_word_bytes,
           (size_t)((end - start) - full_word_bytes));
  }

  GX2RUnlockBufferEx(&buf->uniform_buffer, GX2R_RESOURCE_USAGE_CPU_WRITE);
  GX2RInvalidateBuffer(&buf->uniform_buffer,
                       (GX2RResourceFlags)(GX2R_RESOURCE_USAGE_CPU_WRITE |
                                           GX2R_RESOURCE_USAGE_GPU_READ));
  return true;
}

static gl_uniform_buffer_binding_t *indexed_binding_for_target(GLenum target,
                                                              GLuint index) {
  if (!g_gl_context) return NULL;
  if (target == GL_UNIFORM_BUFFER) {
    if (index >= GL33_MAX_UNIFORM_BUFFER_BINDINGS) return NULL;
    return &g_gl_context->uniform_buffer_bindings[index];
  }
  if (target == GL_TRANSFORM_FEEDBACK_BUFFER) {
    return gl_transform_feedback_current_buffer_binding(index);
  }
  return NULL;
}

static GLuint indexed_binding_limit(GLenum target) {
  if (target == GL_UNIFORM_BUFFER) return GL33_MAX_UNIFORM_BUFFER_BINDINGS;
  if (target == GL_TRANSFORM_FEEDBACK_BUFFER)
    return GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS;
  return 0;
}

void _gl_GenBuffers(GLsizei n, GLuint *buffers) {
  GLsizei count = 0;

  if (n < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (n > 0 && !buffers) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  for (GLuint id = 1; id < MAX_BUFFERS && count < n; ++id) {
    if (!g_buffers[id].reserved && !g_buffers[id].in_use) {
      memset(&g_buffers[id], 0, sizeof(g_buffers[id]));
      g_buffers[id].reserved = true;
      g_buffers[id].usage = GL_STATIC_DRAW;
      reset_mapping_state(&g_buffers[id]);
      buffers[count++] = id;
    }
  }

  if (count < n) {
    for (GLsizei i = count; i < n; ++i) buffers[i] = 0;
    _gl_set_error(GL_OUT_OF_MEMORY);
  }
}

void _gl_DeleteBuffers(GLsizei n, const GLuint *buffers) {
  if (n < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (n > 0 && !buffers) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  for (GLsizei i = 0; i < n; ++i) {
    GLuint id = buffers[i];
    if (id == 0 || id >= MAX_BUFFERS) continue;
    if (!g_buffers[id].reserved && !g_buffers[id].in_use) continue;

    if (g_gl_context) {
      if (g_gl_context->bound_array_buffer == id) g_gl_context->bound_array_buffer = 0;
      if (g_gl_context->bound_copy_read_buffer == id) g_gl_context->bound_copy_read_buffer = 0;
      if (g_gl_context->bound_copy_write_buffer == id) g_gl_context->bound_copy_write_buffer = 0;
      if (g_gl_context->bound_pixel_pack_buffer == id) g_gl_context->bound_pixel_pack_buffer = 0;
      if (g_gl_context->bound_pixel_unpack_buffer == id) g_gl_context->bound_pixel_unpack_buffer = 0;
      if (g_gl_context->bound_texture_buffer == id) g_gl_context->bound_texture_buffer = 0;
      if (g_gl_context->bound_transform_feedback_buffer == id) g_gl_context->bound_transform_feedback_buffer = 0;
      if (g_gl_context->bound_uniform_buffer == id) g_gl_context->bound_uniform_buffer = 0;

      for (uint32_t binding = 0; binding < GL33_MAX_UNIFORM_BUFFER_BINDINGS; ++binding) {
        if (g_gl_context->uniform_buffer_bindings[binding].buffer == id) {
          memset(&g_gl_context->uniform_buffer_bindings[binding], 0,
                 sizeof(g_gl_context->uniform_buffer_bindings[binding]));
        }
      }
      for (uint32_t binding = 0; binding < GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS; ++binding) {
        if (g_gl_context->transform_feedback_buffer_bindings[binding].buffer == id) {
          memset(&g_gl_context->transform_feedback_buffer_bindings[binding], 0,
                 sizeof(g_gl_context->transform_feedback_buffer_bindings[binding]));
        }
      }
      gl_transform_feedback_unbind_buffer(id);
      g_gl_context->dirty_flags |= GL_DIRTY_UNIFORM_BINDINGS | GL_DIRTY_VAO;
    }

    gl_vao_unbind_buffer(id);
    release_buffer_storage(&g_buffers[id]);
    memset(&g_buffers[id], 0, sizeof(g_buffers[id]));
  }
}

GLboolean _gl_IsBuffer(GLuint buffer) {
  return get_buffer(buffer) ? GL_TRUE : GL_FALSE;
}

void _gl_BindBuffer(GLenum target, GLuint buffer) {
  if (!g_gl_context) return;
  if (!is_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!valid_buffer_name_for_bind(buffer)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (target == GL_ELEMENT_ARRAY_BUFFER && !gl_vao_has_bound_array()) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (buffer != 0 && !ensure_buffer_object(buffer)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  set_bound_buffer(target, buffer);
  if (target == GL_ELEMENT_ARRAY_BUFFER) {
    g_gl_context->dirty_flags |= GL_DIRTY_VAO;
  }
}

void _gl_BindBufferBase(GLenum target, GLuint index, GLuint buffer) {
  gl_uniform_buffer_binding_t *binding;

  if (!g_gl_context) return;
  if (target != GL_UNIFORM_BUFFER && target != GL_TRANSFORM_FEEDBACK_BUFFER) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (index >= indexed_binding_limit(target)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (target == GL_TRANSFORM_FEEDBACK_BUFFER &&
      gl_transform_feedback_current_active() == GL_TRUE) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!valid_buffer_name_for_bind(buffer)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (buffer != 0 && !ensure_buffer_object(buffer)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  set_bound_buffer(target, buffer);
  binding = indexed_binding_for_target(target, index);
  if (!binding) return;
  binding->buffer = buffer;
  binding->offset = 0;
  binding->size = 0;
  binding->whole_buffer = buffer ? GL_TRUE : GL_FALSE;
  if (target == GL_TRANSFORM_FEEDBACK_BUFFER) {
    g_gl_context->transform_feedback_buffer_bindings[index] = *binding;
  }

  if (target == GL_UNIFORM_BUFFER) {
    g_gl_context->dirty_flags |= GL_DIRTY_UNIFORM_BINDINGS;
  }
}

void _gl_BindBufferRange(GLenum target, GLuint index, GLuint buffer,
                         GLintptr offset, GLsizeiptr size) {
  gl_uniform_buffer_binding_t *binding;
  GLBuffer *buf;

  if (!g_gl_context) return;
  if (target != GL_UNIFORM_BUFFER && target != GL_TRANSFORM_FEEDBACK_BUFFER) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (index >= indexed_binding_limit(target)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (target == GL_TRANSFORM_FEEDBACK_BUFFER &&
      gl_transform_feedback_current_active() == GL_TRUE) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!valid_buffer_name_for_bind(buffer)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (buffer == 0) {
    set_bound_buffer(target, 0);
    binding = indexed_binding_for_target(target, index);
    if (binding) memset(binding, 0, sizeof(*binding));
    if (target == GL_TRANSFORM_FEEDBACK_BUFFER) {
      memset(&g_gl_context->transform_feedback_buffer_bindings[index], 0,
             sizeof(g_gl_context->transform_feedback_buffer_bindings[index]));
    }
    if (target == GL_UNIFORM_BUFFER) {
      g_gl_context->dirty_flags |= GL_DIRTY_UNIFORM_BINDINGS;
    }
    return;
  }
  if (!ensure_buffer_object(buffer)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (offset < 0 || size <= 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  buf = get_buffer(buffer);
  if (!buf || !range_in_buffer(offset, size, buf->size)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (target == GL_UNIFORM_BUFFER && (offset & 0xFF) != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (target == GL_TRANSFORM_FEEDBACK_BUFFER &&
      (((offset | size) & 0x3) != 0)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  set_bound_buffer(target, buffer);
  binding = indexed_binding_for_target(target, index);
  if (!binding) return;
  binding->buffer = buffer;
  binding->offset = offset;
  binding->size = size;
  binding->whole_buffer = GL_FALSE;
  if (target == GL_TRANSFORM_FEEDBACK_BUFFER) {
    g_gl_context->transform_feedback_buffer_bindings[index] = *binding;
  }

  if (target == GL_UNIFORM_BUFFER) {
    g_gl_context->dirty_flags |= GL_DIRTY_UNIFORM_BINDINGS;
  }
}

void _gl_BufferData(GLenum target, GLsizeiptr size, const GLvoid *data,
                    GLenum usage) {
  GLuint id;
  GLBuffer *buf;
  uint8_t *new_shadow = NULL;
  GX2RBuffer new_raw;
  bool new_raw_owned = false;

  if (!is_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!is_buffer_usage(usage)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (size < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  id = get_bound_buffer(target);
  buf = get_buffer(id);
  if (!buf) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  memset(&new_raw, 0, sizeof(new_raw));
  if (size > 0) {
    new_shadow = (uint8_t *)malloc((size_t)size);
    if (!new_shadow) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
    if (data) memcpy(new_shadow, data, (size_t)size);
    else memset(new_shadow, 0, (size_t)size);

    if (!create_gx2r_buffer(&new_raw, size, kRawBufferFlags)) {
      free(new_shadow);
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
    new_raw_owned = true;
  }

  release_buffer_storage(buf);
  buf->size = size;
  buf->usage = usage;
  buf->shadow = new_shadow;
  buf->raw_buffer = new_raw;
  buf->raw_owned = new_raw_owned;
  reset_mapping_state(buf);

  if (size > 0 && !sync_raw_range(buf, 0, size)) {
    _gl_set_error(GL_OUT_OF_MEMORY);
  }
}

void _gl_BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                       const GLvoid *data) {
  GLuint id;
  GLBuffer *buf;

  if (!is_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (offset < 0 || size < 0 || (size > 0 && !data)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  id = get_bound_buffer(target);
  buf = get_buffer(id);
  if (!buf) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!range_in_buffer(offset, size, buf->size)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (buf->mapped_ptr) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (size == 0) return;

  memcpy(buf->shadow + offset, data, (size_t)size);
  if (!sync_raw_range(buf, offset, size)) {
    _gl_set_error(GL_OUT_OF_MEMORY);
  }
}

void _gl_GetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
  GLuint id;
  GLBuffer *buf;

  if (!params) return;
  if (!is_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  id = get_bound_buffer(target);
  buf = get_buffer(id);
  if (!buf) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  switch (pname) {
  case GL_BUFFER_SIZE:
    *params = (GLint)buf->size;
    break;
  case GL_BUFFER_USAGE:
    *params = (GLint)buf->usage;
    break;
  case GL_BUFFER_ACCESS:
    *params = (GLint)buf->mapped_access;
    break;
  case GL_BUFFER_ACCESS_FLAGS:
    *params = (GLint)buf->mapped_access_flags;
    break;
  case GL_BUFFER_MAPPED:
    *params = buf->mapped_ptr ? GL_TRUE : GL_FALSE;
    break;
  case GL_BUFFER_MAP_OFFSET:
    *params = (GLint)buf->mapped_offset;
    break;
  case GL_BUFFER_MAP_LENGTH:
    *params = (GLint)buf->mapped_length;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_GetBufferPointerv(GLenum target, GLenum pname, GLvoid **params) {
  GLuint id;
  GLBuffer *buf;

  if (!params) return;
  if (!is_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (pname != GL_BUFFER_MAP_POINTER) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  id = get_bound_buffer(target);
  buf = get_buffer(id);
  if (!buf) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  *params = buf->mapped_ptr;
}

void *_gl_MapBuffer(GLenum target, GLenum access) {
  GLbitfield flags;
  GLBuffer *buf;

  switch (access) {
  case GL_READ_ONLY:
    flags = GL_MAP_READ_BIT;
    break;
  case GL_WRITE_ONLY:
    flags = GL_MAP_WRITE_BIT;
    break;
  case GL_READ_WRITE:
    flags = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    return NULL;
  }

  if (!is_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return NULL;
  }
  buf = get_buffer(get_bound_buffer(target));
  if (!buf) {
    _gl_set_error(GL_INVALID_OPERATION);
    return NULL;
  }

  return _gl_MapBufferRange(target, 0, buf->size, flags);
}

void *_gl_MapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                         GLbitfield access) {
  static const GLbitfield valid_access_bits =
      GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT |
      GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT |
      GL_MAP_UNSYNCHRONIZED_BIT;
  GLuint id;
  GLBuffer *buf;

  if (!is_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return NULL;
  }

  id = get_bound_buffer(target);
  buf = get_buffer(id);
  if (!buf) {
    _gl_set_error(GL_INVALID_OPERATION);
    return NULL;
  }
  if (!range_in_buffer(offset, length, buf->size) ||
      (access & ~valid_access_bits) != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return NULL;
  }
  if (buf->mapped_ptr) {
    _gl_set_error(GL_INVALID_OPERATION);
    return NULL;
  }
  if ((access & (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT)) == 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return NULL;
  }
  if ((access & GL_MAP_READ_BIT) &&
      (access & (GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
                 GL_MAP_UNSYNCHRONIZED_BIT))) {
    _gl_set_error(GL_INVALID_OPERATION);
    return NULL;
  }
  if ((access & GL_MAP_FLUSH_EXPLICIT_BIT) &&
      !(access & GL_MAP_WRITE_BIT)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return NULL;
  }

  if (length == 0 && !buf->shadow) {
    buf->mapped_ptr = &g_empty_mapping_byte;
  } else {
    buf->mapped_ptr = buf->shadow + offset;
  }
  buf->mapped_access_flags = access;
  if ((access & GL_MAP_READ_BIT) && (access & GL_MAP_WRITE_BIT)) {
    buf->mapped_access = GL_READ_WRITE;
  } else if (access & GL_MAP_WRITE_BIT) {
    buf->mapped_access = GL_WRITE_ONLY;
  } else {
    buf->mapped_access = GL_READ_ONLY;
  }
  buf->mapped_offset = offset;
  buf->mapped_length = length;
  return buf->mapped_ptr;
}

GLboolean _gl_UnmapBuffer(GLenum target) {
  GLuint id;
  GLBuffer *buf;

  if (!is_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return GL_FALSE;
  }

  id = get_bound_buffer(target);
  buf = get_buffer(id);
  if (!buf) {
    _gl_set_error(GL_INVALID_OPERATION);
    return GL_FALSE;
  }
  if (!buf->mapped_ptr) {
    _gl_set_error(GL_INVALID_OPERATION);
    return GL_FALSE;
  }

  if ((buf->mapped_access_flags & GL_MAP_WRITE_BIT) &&
      !(buf->mapped_access_flags & GL_MAP_FLUSH_EXPLICIT_BIT)) {
    if (!sync_raw_range(buf, buf->mapped_offset, buf->mapped_length)) {
      reset_mapping_state(buf);
      _gl_set_error(GL_OUT_OF_MEMORY);
      return GL_FALSE;
    }
  }

  reset_mapping_state(buf);
  return GL_TRUE;
}

void _gl_FlushMappedBufferRange(GLenum target, GLintptr offset,
                                GLsizeiptr length) {
  GLuint id;
  GLBuffer *buf;

  if (!is_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  id = get_bound_buffer(target);
  buf = get_buffer(id);
  if (!buf) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (offset < 0 || length < 0 ||
      (uint64_t)offset + (uint64_t)length > (uint64_t)buf->mapped_length) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!buf->mapped_ptr ||
      !(buf->mapped_access_flags & GL_MAP_FLUSH_EXPLICIT_BIT)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (length == 0) return;

  if (!sync_raw_range(buf, buf->mapped_offset + offset, length)) {
    _gl_set_error(GL_OUT_OF_MEMORY);
  }
}

void gl_buffer_copy_sub_data(GLenum readTarget, GLenum writeTarget,
                             GLintptr readOffset, GLintptr writeOffset,
                             GLsizeiptr size) {
  GLuint read_id;
  GLuint write_id;
  GLBuffer *read_buf;
  GLBuffer *write_buf;

  if (!is_buffer_target(readTarget) || !is_buffer_target(writeTarget)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (readOffset < 0 || writeOffset < 0 || size < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  read_id = get_bound_buffer(readTarget);
  write_id = get_bound_buffer(writeTarget);
  read_buf = get_buffer(read_id);
  write_buf = get_buffer(write_id);
  if (!read_buf || !write_buf) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!range_in_buffer(readOffset, size, read_buf->size) ||
      !range_in_buffer(writeOffset, size, write_buf->size)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (read_buf->mapped_ptr || write_buf->mapped_ptr) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (read_id == write_id &&
      ranges_overlap(readOffset, size, writeOffset, size)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (size == 0) return;

  memmove(write_buf->shadow + writeOffset, read_buf->shadow + readOffset,
          (size_t)size);
  if (!sync_raw_range(write_buf, writeOffset, size)) {
    _gl_set_error(GL_OUT_OF_MEMORY);
  }
}

void gl_buffer_get_sub_data(GLenum target, GLintptr offset, GLsizeiptr size,
                            GLvoid *data) {
  GLuint id;
  GLBuffer *buf;

  if (!is_buffer_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (offset < 0 || size < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  id = get_bound_buffer(target);
  buf = get_buffer(id);
  if (!buf) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!range_in_buffer(offset, size, buf->size)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (buf->mapped_ptr) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (size == 0) return;
  if (!data) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  memcpy(data, buf->shadow + offset, (size_t)size);
}

void gl_buffer_end_frame(void) {
}

GLboolean gl_buffer_is_mapped(GLuint buffer) {
  GLBuffer *buf = get_buffer(buffer);
  return (buf && buf->mapped_ptr) ? GL_TRUE : GL_FALSE;
}

void *gl_buffer_get_data(GLuint id) {
  GLBuffer *buf = get_buffer(id);
  if (!buf || !buf->raw_owned) return NULL;
  return buf->raw_buffer.buffer;
}

static GLboolean gl_buffer_get_shadow_range(GLuint id, GLintptr offset,
                                            GLsizeiptr size,
                                            GLvoid **data) {
  GLBuffer *buf = get_buffer(id);

  if (data) *data = NULL;
  if (!data || !buf || offset < 0 || size < 0 ||
      !range_in_buffer(offset, size, buf->size)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return GL_FALSE;
  }
  if (buf->mapped_ptr) {
    _gl_set_error(GL_INVALID_OPERATION);
    return GL_FALSE;
  }
  if (size > 0 && !buf->shadow) {
    _gl_set_error(GL_INVALID_OPERATION);
    return GL_FALSE;
  }

  *data = size == 0 && !buf->shadow ? NULL : buf->shadow + offset;
  return GL_TRUE;
}

GLboolean gl_buffer_get_read_range(GLuint buffer, GLintptr offset,
                                   GLsizeiptr size, const GLvoid **data) {
  GLvoid *mutable_data = NULL;
  GLboolean ok = gl_buffer_get_shadow_range(buffer, offset, size,
                                            &mutable_data);
  if (data) *data = mutable_data;
  return ok;
}

GLboolean gl_buffer_get_write_range(GLuint buffer, GLintptr offset,
                                    GLsizeiptr size, GLvoid **data) {
  return gl_buffer_get_shadow_range(buffer, offset, size, data);
}

GLboolean gl_buffer_flush_range(GLuint buffer, GLintptr offset,
                                GLsizeiptr size) {
  GLBuffer *buf = get_buffer(buffer);

  if (!buf || offset < 0 || size < 0 ||
      !range_in_buffer(offset, size, buf->size)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return GL_FALSE;
  }
  if (buf->mapped_ptr) {
    _gl_set_error(GL_INVALID_OPERATION);
    return GL_FALSE;
  }
  if (size == 0) return GL_TRUE;
  if (!sync_raw_range(buf, offset, size)) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return GL_FALSE;
  }
  return GL_TRUE;
}

void *gl_buffer_get_uniform_block_data(GLuint id, GLintptr offset,
                                       GLsizeiptr size) {
  GLBuffer *buf = get_buffer(id);
  if (!buf || !range_in_buffer(offset, size, buf->size)) return NULL;
  if (size > 0 && !sync_uniform_range(buf, offset, size)) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return NULL;
  }
  if (buf->uniform_owned && buf->uniform_buffer.buffer) {
    return (uint8_t *)buf->uniform_buffer.buffer + offset;
  }
  return NULL;
}

GLsizeiptr gl_buffer_get_size(GLuint id) {
  GLBuffer *buf = get_buffer(id);
  return buf ? buf->size : 0;
}

GX2RBuffer *gl_buffer_get_gx2r_buffer(GLuint id) {
  GLBuffer *buf = get_buffer(id);
  if (!buf || !buf->raw_owned || !buf->raw_buffer.buffer) return NULL;
  return &buf->raw_buffer;
}

#ifdef __cplusplus
}
#endif
