#include "gl_query.h"

#include "gl_context.h"

#include <coreinit/time.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/state.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_OBJECT_TYPE
#define GL_OBJECT_TYPE 0x9112
#endif
#ifndef GL_SYNC_CONDITION
#define GL_SYNC_CONDITION 0x9113
#endif
#ifndef GL_SYNC_STATUS
#define GL_SYNC_STATUS 0x9114
#endif
#ifndef GL_SYNC_FLAGS
#define GL_SYNC_FLAGS 0x9115
#endif
#ifndef GL_SYNC_FENCE
#define GL_SYNC_FENCE 0x9116
#endif
#ifndef GL_WAIT_FAILED
#ifdef GL_WAIT_FAILED_GL
#define GL_WAIT_FAILED GL_WAIT_FAILED_GL
#else
#define GL_WAIT_FAILED 0x911D
#endif
#endif
#ifndef GL_CURRENT_QUERY
#define GL_CURRENT_QUERY 0x8865
#endif
#ifndef GL_PRIMITIVES_GENERATED
#define GL_PRIMITIVES_GENERATED 0x8C87
#endif
#ifndef GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN
#define GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN 0x8C88
#endif
#ifndef GL_QUERY_BY_REGION_WAIT
#define GL_QUERY_BY_REGION_WAIT 0x8E15
#endif
#ifndef GL_QUERY_BY_REGION_NO_WAIT
#define GL_QUERY_BY_REGION_NO_WAIT 0x8E16
#endif

#define GL33_MAX_QUERY_OBJECTS 512
#define GL33_SYNC_MAGIC 0x33584E53u

typedef struct {
  bool reserved;
  bool in_use;
  bool active;
  GLenum target;
  GLuint64 result;
  GLboolean result_available;
  OSTime begin_ticks;
} GLQueryObject;

struct __GLsync {
  uint32_t magic;
  GLenum condition;
  GLbitfield flags;
  GLboolean signaled;
  struct __GLsync *next;
};

static GLQueryObject g_query_objects[GL33_MAX_QUERY_OBJECTS];
static struct __GLsync *g_sync_objects;

static bool is_query_begin_target(GLenum target) {
  switch (target) {
  case GL_SAMPLES_PASSED:
  case GL_ANY_SAMPLES_PASSED:
  case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
  case GL_TIME_ELAPSED:
  case GL_PRIMITIVES_GENERATED:
  case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
    return true;
  default:
    return false;
  }
}

static bool is_query_info_target(GLenum target) {
  return is_query_begin_target(target) || target == GL_TIMESTAMP;
}

static bool is_query_object_pname(GLenum pname) {
  return pname == GL_QUERY_RESULT || pname == GL_QUERY_RESULT_AVAILABLE;
}

static bool is_conditional_render_mode(GLenum mode) {
  return mode == GL_QUERY_WAIT ||
         mode == GL_QUERY_NO_WAIT ||
         mode == GL_QUERY_BY_REGION_WAIT ||
         mode == GL_QUERY_BY_REGION_NO_WAIT;
}

static bool query_targets_compatible(GLenum object_target,
                                     GLenum requested_target) {
  if (object_target == requested_target) return true;
  if ((object_target == GL_TIME_ELAPSED || object_target == GL_TIMESTAMP) &&
      (requested_target == GL_TIME_ELAPSED || requested_target == GL_TIMESTAMP)) {
    return true;
  }
  return false;
}

static GLuint *active_query_binding(GLenum target) {
  if (!g_gl_context) return NULL;

  switch (target) {
  case GL_SAMPLES_PASSED:
    return &g_gl_context->active_query_samples_passed;
  case GL_ANY_SAMPLES_PASSED:
    return &g_gl_context->active_query_any_samples_passed;
  case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
    return &g_gl_context->active_query_any_samples_passed_conservative;
  case GL_TIME_ELAPSED:
    return &g_gl_context->active_query_time_elapsed;
  case GL_PRIMITIVES_GENERATED:
    return &g_gl_context->active_query_primitives_generated;
  case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
    return &g_gl_context->active_query_transform_feedback_primitives_written;
  default:
    return NULL;
  }
}

static GLQueryObject *query_object(GLuint id) {
  if (id == 0 || id >= GL33_MAX_QUERY_OBJECTS) return NULL;
  if (!g_query_objects[id].in_use) return NULL;
  return &g_query_objects[id];
}

static GLQueryObject *create_or_get_query_object(GLuint id, GLenum target) {
  GLQueryObject *query;

  if (id == 0 || id >= GL33_MAX_QUERY_OBJECTS) {
    _gl_set_error(GL_INVALID_OPERATION);
    return NULL;
  }

  query = &g_query_objects[id];
  if (query->in_use) {
    if (!query_targets_compatible(query->target, target)) {
      _gl_set_error(GL_INVALID_OPERATION);
      return NULL;
    }
    return query;
  }

  memset(query, 0, sizeof(*query));
  query->in_use = true;
  query->target = target;
  query->result_available = GL_FALSE;
  return query;
}

static GLuint64 ticks_to_query_nanoseconds(OSTime ticks) {
  if (ticks <= 0) return 0;
  return (GLuint64)OSTicksToNanoseconds((uint64_t)ticks);
}

static void finish_query_object(GLQueryObject *query) {
  OSTime end_ticks;

  if (!query) return;

  if (query->target == GL_TIME_ELAPSED) {
    end_ticks = OSGetTime();
    query->result = ticks_to_query_nanoseconds(end_ticks - query->begin_ticks);
  } else {
    query->result = 0;
  }

  query->active = false;
  query->result_available = GL_TRUE;
}

static bool allocate_query_names(GLsizei n, GLuint *ids) {
  GLsizei found = 0;

  for (GLuint id = 1; id < GL33_MAX_QUERY_OBJECTS && found < n; ++id) {
    if (!g_query_objects[id].reserved && !g_query_objects[id].in_use) {
      ids[found++] = id;
    }
  }

  if (found != n) {
    for (GLsizei i = 0; i < n; ++i) ids[i] = 0;
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  for (GLsizei i = 0; i < n; ++i) {
    g_query_objects[ids[i]].reserved = true;
  }
  return true;
}

static bool get_query_result(GLuint id, GLenum pname, GLuint64 *result) {
  GLQueryObject *query;

  if (!result) return false;
  if (!is_query_object_pname(pname)) {
    _gl_set_error(GL_INVALID_ENUM);
    return false;
  }

  query = query_object(id);
  if (!query) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }
  if (query->active) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  if (pname == GL_QUERY_RESULT_AVAILABLE) {
    *result = query->result_available ? GL_TRUE : GL_FALSE;
  } else {
    *result = query->result;
  }
  return true;
}

static struct __GLsync *find_sync_object(GLsync sync) {
  struct __GLsync *candidate = (struct __GLsync *)sync;

  if (!candidate) return NULL;
  for (struct __GLsync *it = g_sync_objects; it; it = it->next) {
    if (it == candidate && it->magic == GL33_SYNC_MAGIC) {
      return it;
    }
  }
  return NULL;
}

void gl_query_init(void) {
  memset(g_query_objects, 0, sizeof(g_query_objects));
}

void gl_query_shutdown(void) {
  gl_query_init();
}

void gl_sync_shutdown(void) {
  struct __GLsync *sync = g_sync_objects;

  while (sync) {
    struct __GLsync *next = sync->next;
    sync->magic = 0;
    free(sync);
    sync = next;
  }

  g_sync_objects = NULL;
}

void gl_sync_signal_all(void) {
  for (struct __GLsync *sync = g_sync_objects; sync; sync = sync->next) {
    sync->signaled = GL_TRUE;
  }
}

void _gl_GenQueries(GLsizei n, GLuint *ids) {
  if (n < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (n > 0 && !ids) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (n == 0) return;

  allocate_query_names(n, ids);
}

void _gl_DeleteQueries(GLsizei n, const GLuint *ids) {
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
    GLQueryObject *query;

    if (id == 0 || id >= GL33_MAX_QUERY_OBJECTS) continue;

    query = &g_query_objects[id];
    if (query->in_use && query->active) {
      GLuint *binding = active_query_binding(query->target);
      finish_query_object(query);
      if (binding && *binding == id) *binding = 0;
    }
    memset(query, 0, sizeof(*query));
  }
}

GLboolean _gl_IsQuery(GLuint id) {
  return query_object(id) ? GL_TRUE : GL_FALSE;
}

void _gl_BeginQuery(GLenum target, GLuint id) {
  GLuint *binding;
  GLQueryObject *query;

  if (!is_query_begin_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  binding = active_query_binding(target);
  if (!binding || *binding != 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  query = create_or_get_query_object(id, target);
  if (!query) return;
  if (query->active) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  query->target = target;
  query->active = true;
  query->result = 0;
  query->result_available = GL_FALSE;
  query->begin_ticks = OSGetTime();
  *binding = id;
}

void _gl_EndQuery(GLenum target) {
  GLuint id;
  GLuint *binding;
  GLQueryObject *query;

  if (!is_query_begin_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  binding = active_query_binding(target);
  if (!binding || *binding == 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  id = *binding;
  query = query_object(id);
  if (!query || !query->active) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  finish_query_object(query);
  *binding = 0;
}

void _gl_GetQueryiv(GLenum target, GLenum pname, GLint *params) {
  if (!params) return;
  if (!is_query_info_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  if (pname == GL_CURRENT_QUERY) {
    GLuint *binding;

    if (!is_query_begin_target(target)) {
      _gl_set_error(GL_INVALID_ENUM);
      return;
    }
    binding = active_query_binding(target);
    *params = binding ? (GLint)*binding : 0;
    return;
  }

  if (pname == GL_QUERY_COUNTER_BITS) {
    *params = (target == GL_TIME_ELAPSED || target == GL_TIMESTAMP) ? 64 : 32;
    return;
  }

  _gl_set_error(GL_INVALID_ENUM);
}

void _gl_GetQueryObjectiv(GLuint id, GLenum pname, GLint *params) {
  GLuint64 result = 0;

  if (!params) return;
  if (!get_query_result(id, pname, &result)) return;
  *params = (GLint)result;
}

void _gl_GetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params) {
  GLuint64 result = 0;

  if (!params) return;
  if (!get_query_result(id, pname, &result)) return;
  *params = (GLuint)result;
}

void _gl_BeginConditionalRender(GLuint id, GLenum mode) {
  GLQueryObject *query;

  if (!is_conditional_render_mode(mode)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!g_gl_context || g_gl_context->conditional_render_active) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  query = query_object(id);
  if (!query || query->active) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  g_gl_context->conditional_render_query = id;
  g_gl_context->conditional_render_mode = mode;
  g_gl_context->conditional_render_active = GL_TRUE;
}

void _gl_EndConditionalRender(void) {
  if (!g_gl_context || !g_gl_context->conditional_render_active) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  g_gl_context->conditional_render_query = 0;
  g_gl_context->conditional_render_mode = 0;
  g_gl_context->conditional_render_active = GL_FALSE;
}

void _gl_BeginQueryIndexed(GLenum target, GLuint index, GLuint id) {
  if (index != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  _gl_BeginQuery(target, id);
}

void _gl_EndQueryIndexed(GLenum target, GLuint index) {
  if (index != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  _gl_EndQuery(target);
}

void _gl_GetQueryIndexediv(GLenum target, GLuint index, GLenum pname, GLint *params) {
  if (index != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  _gl_GetQueryiv(target, pname, params);
}

GLsync glFenceSync(GLenum condition, GLbitfield flags) {
  struct __GLsync *sync;

  if (condition != GL_SYNC_GPU_COMMANDS_COMPLETE) {
    _gl_set_error(GL_INVALID_ENUM);
    return NULL;
  }
  if (flags != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return NULL;
  }

  sync = (struct __GLsync *)malloc(sizeof(*sync));
  if (!sync) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return NULL;
  }

  sync->magic = GL33_SYNC_MAGIC;
  sync->condition = condition;
  sync->flags = flags;
  sync->signaled = GL_FALSE;
  sync->next = g_sync_objects;
  g_sync_objects = sync;

  GX2Flush();
  return (GLsync)sync;
}

GLboolean glIsSync(GLsync sync) {
  return find_sync_object(sync) ? GL_TRUE : GL_FALSE;
}

void glDeleteSync(GLsync sync) {
  struct __GLsync *target = (struct __GLsync *)sync;
  struct __GLsync **link = &g_sync_objects;

  while (*link && *link != target) {
    link = &(*link)->next;
  }

  if (!*link || (*link)->magic != GL33_SYNC_MAGIC) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  *link = target->next;
  target->magic = 0;
  free(target);
}

GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
  struct __GLsync *object = find_sync_object(sync);
  GLboolean was_signaled;

  if (!object) {
    _gl_set_error(GL_INVALID_VALUE);
    return GL_WAIT_FAILED;
  }
  if (flags & ~GL_SYNC_FLUSH_COMMANDS_BIT) {
    _gl_set_error(GL_INVALID_VALUE);
    return GL_WAIT_FAILED;
  }

  was_signaled = object->signaled;
  if (!object->signaled) {
    if ((flags & GL_SYNC_FLUSH_COMMANDS_BIT) != 0) {
      GX2Flush();
    }
    if (timeout == 0) {
      return GL_TIMEOUT_EXPIRED;
    }
    GX2DrawDone();
    gl_sync_signal_all();
  }

  return was_signaled ? GL_ALREADY_SIGNALED : GL_CONDITION_SATISFIED;
}

void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
  struct __GLsync *object = find_sync_object(sync);

  if (!object) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (flags != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (timeout != GL_TIMEOUT_IGNORED) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!object->signaled) {
    GX2DrawDone();
    gl_sync_signal_all();
  }
}

void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize,
                 GLsizei *length, GLint *values) {
  struct __GLsync *object = find_sync_object(sync);
  GLint value;

  if (!object) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (bufSize < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (bufSize > 0 && !values) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  switch (pname) {
  case GL_OBJECT_TYPE:
    value = GL_SYNC_FENCE;
    break;
  case GL_SYNC_CONDITION:
    value = GL_SYNC_GPU_COMMANDS_COMPLETE;
    break;
  case GL_SYNC_STATUS:
    value = object->signaled ? GL_SIGNALED : GL_UNSIGNALED;
    break;
  case GL_SYNC_FLAGS:
    value = (GLint)object->flags;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  if (length) *length = bufSize > 0 ? 1 : 0;
  if (values && bufSize > 0) values[0] = value;
}

void glQueryCounter(GLuint id, GLenum target) {
  GLQueryObject *query;
  OSTime now;

  if (target != GL_TIMESTAMP) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  query = create_or_get_query_object(id, target);
  if (!query) return;
  if (query->active) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  GX2Flush();
  now = OSGetTime();
  query->target = GL_TIMESTAMP;
  query->active = false;
  query->result = ticks_to_query_nanoseconds(now);
  query->result_available = GL_TRUE;
}

void glGetQueryObjecti64v(GLuint id, GLenum pname, GLint64 *params) {
  GLuint64 result = 0;

  if (!params) return;
  if (!get_query_result(id, pname, &result)) return;
  *params = (GLint64)result;
}

void glGetQueryObjectui64v(GLuint id, GLenum pname, GLuint64 *params) {
  GLuint64 result = 0;

  if (!params) return;
  if (!get_query_result(id, pname, &result)) return;
  *params = result;
}
