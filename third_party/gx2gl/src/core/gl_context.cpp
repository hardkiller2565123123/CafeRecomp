#include "gl_context.h"
#include "mem/gl_mem.h"
#include "state/gl_state.h"
#include "gl_buffer.h"
#include "gl_texture.h"
#include "gl_shader.h"
#include "gl_vao.h"
#include "gl_framebuffer.h"
#include "gl_draw.h"
#include "gl_query.h"
#include "gl_transform_feedback.h"
#include "gl_dispatch.h"
#include <coreinit/mutex.h>
#include <gx2/event.h>
#include <gx2/draw.h>
#include <whb/gfx.h>
#include <gx2/clear.h>
#include <stdint.h>
#include <string.h>

#ifndef GL_TEXTURE_2D_MULTISAMPLE
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#endif
#ifndef GL_PROXY_TEXTURE_2D_MULTISAMPLE
#define GL_PROXY_TEXTURE_2D_MULTISAMPLE 0x9101
#endif
#ifndef GL_TEXTURE_2D_MULTISAMPLE_ARRAY
#define GL_TEXTURE_2D_MULTISAMPLE_ARRAY 0x9102
#endif
#ifndef GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY
#define GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY 0x9103
#endif
#ifndef GL_LAST_VERTEX_CONVENTION
#define GL_LAST_VERTEX_CONVENTION 0x8E4E
#endif
#ifdef __cplusplus
extern "C" {
#endif

gl_context_t *g_gl_context = NULL;

static void gl_context_default_framebuffer_size(GLsizei *width,
                                                GLsizei *height) {
  GX2ColorBuffer *tv_color;

  if (width) *width = 0;
  if (height) *height = 0;

  tv_color = WHBGfxGetTVColourBuffer();
  if (!tv_color || !WHBGfxGetTVContextState() ||
      !tv_color->surface.image) return;

  if (width) *width = (GLsizei)tv_color->surface.width;
  if (height) *height = (GLsizei)tv_color->surface.height;
}

static void gl_context_init_pixel_store(gl_context_t *ctx) {
  ctx->pack_alignment = 4;
  ctx->unpack_alignment = 4;
}

static void gl_context_init_blend_depth_stencil(gl_context_t *ctx) {
  ctx->blend_src_rgb = GL_ONE;
  ctx->blend_dst_rgb = GL_ZERO;
  ctx->blend_src_alpha = GL_ONE;
  ctx->blend_dst_alpha = GL_ZERO;
  ctx->blend_eq_rgb = GL_FUNC_ADD;
  ctx->blend_eq_alpha = GL_FUNC_ADD;
  ctx->depth_func = GL_LESS;
  ctx->depth_mask = GL_TRUE;
  ctx->stencil_compare_mask[0] = 0xFFFFFFFFu;
  ctx->stencil_compare_mask[1] = 0xFFFFFFFFu;
  ctx->stencil_write_mask[0] = 0xFFFFFFFFu;
  ctx->stencil_write_mask[1] = 0xFFFFFFFFu;
  ctx->stencil_func[0] = GL_ALWAYS;
  ctx->stencil_func[1] = GL_ALWAYS;
  ctx->stencil_fail[0] = GL_KEEP;
  ctx->stencil_fail[1] = GL_KEEP;
  ctx->stencil_zfail[0] = GL_KEEP;
  ctx->stencil_zfail[1] = GL_KEEP;
  ctx->stencil_zpass[0] = GL_KEEP;
  ctx->stencil_zpass[1] = GL_KEEP;
}

static void gl_context_init_raster_state(gl_context_t *ctx) {
  GLsizei default_width = 0;
  GLsizei default_height = 0;

  gl_context_default_framebuffer_size(&default_width, &default_height);

  ctx->active_texture = 0;
  ctx->viewport.x = 0;
  ctx->viewport.y = 0;
  ctx->viewport.width = default_width;
  ctx->viewport.height = default_height;
  ctx->viewport.near_z = 0.0f;
  ctx->viewport.far_z = 1.0f;
  ctx->scissor.x = 0;
  ctx->scissor.y = 0;
  ctx->scissor.width = default_width;
  ctx->scissor.height = default_height;
  ctx->cull_face_mode = GL_BACK;
  ctx->front_face = GL_CCW;
  ctx->polygon_mode = GL_FILL;
  ctx->provoking_vertex = GL_LAST_VERTEX_CONVENTION;
  ctx->clamp_read_color = GL_FIXED_ONLY;
  ctx->line_width = 1.0f;
  ctx->logic_op = GL_COPY;
  ctx->point_size = 1.0f;
  ctx->sample_coverage_value = 1.0f;
  ctx->sample_mask_value = 0xFFFFFFFFu;
  ctx->multisample_enabled = GL_TRUE;
  ctx->generate_mipmap_hint = GL_DONT_CARE;
  ctx->primitive_restart_index = 0;
  ctx->color_mask[0] = GL_TRUE;
  ctx->color_mask[1] = GL_TRUE;
  ctx->color_mask[2] = GL_TRUE;
  ctx->color_mask[3] = GL_TRUE;
}

static void gl_context_init_vertex_defaults(gl_context_t *ctx) {
  for (uint32_t i = 0; i < GL33_MAX_VERTEX_ATTRIBS; ++i) {
    ctx->current_vertex_attrib[i][0] = 0.0f;
    ctx->current_vertex_attrib[i][1] = 0.0f;
    ctx->current_vertex_attrib[i][2] = 0.0f;
    ctx->current_vertex_attrib[i][3] = 1.0f;
  }
}

static void gl_context_init_defaults(gl_context_t *ctx) {
  if (!ctx) return;

  memset(ctx, 0, sizeof(gl_context_t));

  gl_context_init_pixel_store(ctx);
  gl_context_init_blend_depth_stencil(ctx);
  gl_context_init_raster_state(ctx);
  gl_context_init_vertex_defaults(ctx);

  ctx->clear_depth = 1.0f;
  ctx->clear_stencil = 0;
  ctx->error = GL_NO_ERROR;
  ctx->dirty_flags = 0xFFFFFFFF;
}

void _gl_set_error(GLenum error) {
  uint32_t next;

  if (!g_gl_context || error == GL_NO_ERROR) return;

  OSLockMutex(&g_gl_context->error_mutex);
  next = (g_gl_context->error_head + 1) % GL_ERROR_QUEUE_SIZE;
  if (next != g_gl_context->error_tail) {
    g_gl_context->error_queue[g_gl_context->error_head] = error;
    g_gl_context->error_head = next;
  }
  OSUnlockMutex(&g_gl_context->error_mutex);
}

GLenum glGetError(void) {
  GLenum error;

  if (!g_gl_context) return GL_NO_ERROR;

  OSLockMutex(&g_gl_context->error_mutex);
  if (g_gl_context->error_head == g_gl_context->error_tail) {
    OSUnlockMutex(&g_gl_context->error_mutex);
    return GL_NO_ERROR;
  }

  error = g_gl_context->error_queue[g_gl_context->error_tail];
  g_gl_context->error_tail = (g_gl_context->error_tail + 1) % GL_ERROR_QUEUE_SIZE;
  OSUnlockMutex(&g_gl_context->error_mutex);
  return error;
}

void _gl_Flush(void) {
  gl_flush_state();
  GX2Flush();
}

void _gl_Finish(void) {
  GX2DrawDone();
  gl_sync_signal_all();
}

static void gl_context_init_subsystems(void) {
  gl_buffer_init();
  gl_texture_init();
  gl_shader_init();
  gl_vao_init();
  gl_framebuffer_init();
  gl_query_init();
  gl_transform_feedback_init();
}

gl_context_t *gl_context_create(void) {
  gl_context_t *ctx = (gl_context_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2,
                                                   sizeof(gl_context_t), 4);
  if (!ctx) return NULL;

  gl_context_init_defaults(ctx);
  OSInitMutex(&ctx->error_mutex);
  gl_context_init_subsystems();
  gl_dispatch_init(ctx);

  return ctx;
}

void gl_context_destroy(gl_context_t *ctx) {
  if (!ctx) return;

  if (g_gl_context == ctx) {
    _gl_Finish();
  }

  gl_sync_shutdown();
  gl_query_shutdown();
  gl_transform_feedback_shutdown();
  gl_framebuffer_shutdown();
  gl_shader_shutdown();
  gl_texture_shutdown();
  gl_buffer_shutdown();
  gl_vao_shutdown();
  if (g_gl_context == ctx) {
    g_gl_context = NULL;
  }
  gl_mem_free(GL_MEM_TYPE_MEM2, ctx);
}

#ifdef __cplusplus
}
#endif
