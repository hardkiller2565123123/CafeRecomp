#include "gl_state.h"

#include "core/gl_framebuffer.h"
#include "core/gl_shader.h"
#include "core/gl_texture.h"
#include "core/gl_vao.h"
#include "endian/endian.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <coreinit/cache.h>
#include <gx2/clear.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>

typedef struct {
  int8_t x[8];
  int8_t y[8];
} GX2GLAASampleLoc;

void GX2SetAAMode(GX2AAMode aa);
void GX2SetAAModeEx(GX2GLAASampleLoc *sampleLoc, GX2AAMode aa);
#ifdef __cplusplus
}
#endif

#include <stdint.h>
#include <string.h>

#ifndef GL_POLYGON_OFFSET_FACTOR
#define GL_POLYGON_OFFSET_FACTOR 0x8038
#endif
#ifndef GL_POLYGON_OFFSET_UNITS
#define GL_POLYGON_OFFSET_UNITS 0x2A00
#endif

#define GX2GL_PA_SU_SC_MODE_CNTL_PROVOKING_VTX_LAST (1u << 19)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  GLint x;
  GLint y;
  GLint width;
  GLint height;
} GLRect;

static const int8_t kSampleOffsets1X[8][2] = {
    {0, 0}, {0, 0}, {0, 0}, {0, 0},
    {0, 0}, {0, 0}, {0, 0}, {0, 0},
};
static const int8_t kSampleOffsets2X[8][2] = {
    {-4, -4}, {4, 4}, {0, 0}, {0, 0},
    {0, 0}, {0, 0}, {0, 0}, {0, 0},
};
static const int8_t kSampleOffsets4X[8][2] = {
    {-2, -6}, {6, -2}, {-6, 2}, {2, 6},
    {0, 0}, {0, 0}, {0, 0}, {0, 0},
};
static const int8_t kSampleOffsets8X[8][2] = {
    {1, -3}, {-1, 3}, {5, 1}, {-3, -5},
    {-5, 5}, {-7, -1}, {3, 7}, {7, -7},
};

static const int8_t (*sample_offsets(GLsizei samples))[2] {
  switch (samples) {
  case 1: return kSampleOffsets1X;
  case 2: return kSampleOffsets2X;
  case 4: return kSampleOffsets4X;
  case 8: return kSampleOffsets8X;
  default: return NULL;
  }
}

GLboolean gl_get_multisample_position(GLsizei samples, GLuint index,
                                      GLfloat *position) {
  const int8_t (*offsets)[2] = sample_offsets(samples);
  if (!position || !offsets || index >= (GLuint)samples) return GL_FALSE;
  position[0] = 0.5f + (GLfloat)offsets[index][0] / 16.0f;
  position[1] = 0.5f + (GLfloat)offsets[index][1] / 16.0f;
  return GL_TRUE;
}

static GLboolean gl_bool(GLboolean value) {
  return value ? GL_TRUE : GL_FALSE;
}

static GLfloat clamp01f(GLfloat value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

static GLint clamp_stencil_ref(GLint value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return value;
}

static uint8_t clamp_clear_unorm8(GLfloat value) {
  value = clamp01f(value);
  return (uint8_t)(value * 255.0f + 0.5f);
}

static void mark_dirty(uint32_t flags) {
  if (g_gl_context) g_gl_context->dirty_flags |= flags;
}

static bool valid_compare_func(GLenum func) {
  switch (func) {
  case GL_NEVER:
  case GL_LESS:
  case GL_EQUAL:
  case GL_LEQUAL:
  case GL_GREATER:
  case GL_NOTEQUAL:
  case GL_GEQUAL:
  case GL_ALWAYS:
    return true;
  default:
    return false;
  }
}

static GX2CompareFunction map_compare_func(GLenum func) {
  switch (func) {
  case GL_NEVER: return GX2_COMPARE_FUNC_NEVER;
  case GL_LESS: return GX2_COMPARE_FUNC_LESS;
  case GL_EQUAL: return GX2_COMPARE_FUNC_EQUAL;
  case GL_LEQUAL: return GX2_COMPARE_FUNC_LEQUAL;
  case GL_GREATER: return GX2_COMPARE_FUNC_GREATER;
  case GL_NOTEQUAL: return GX2_COMPARE_FUNC_NOT_EQUAL;
  case GL_GEQUAL: return GX2_COMPARE_FUNC_GEQUAL;
  default: return GX2_COMPARE_FUNC_ALWAYS;
  }
}

static bool valid_blend_factor(GLenum factor) {
  switch (factor) {
  case GL_ZERO:
  case GL_ONE:
  case GL_SRC_COLOR:
  case GL_ONE_MINUS_SRC_COLOR:
  case GL_SRC_ALPHA:
  case GL_ONE_MINUS_SRC_ALPHA:
  case GL_DST_ALPHA:
  case GL_ONE_MINUS_DST_ALPHA:
  case GL_DST_COLOR:
  case GL_ONE_MINUS_DST_COLOR:
  case GL_SRC_ALPHA_SATURATE:
  case GL_CONSTANT_COLOR:
  case GL_ONE_MINUS_CONSTANT_COLOR:
  case GL_CONSTANT_ALPHA:
  case GL_ONE_MINUS_CONSTANT_ALPHA:
    return true;
  default:
    return false;
  }
}

static GX2BlendMode map_blend_factor(GLenum factor) {
  switch (factor) {
  case GL_ZERO: return GX2_BLEND_MODE_ZERO;
  case GL_ONE: return GX2_BLEND_MODE_ONE;
  case GL_SRC_COLOR: return GX2_BLEND_MODE_SRC_COLOR;
  case GL_ONE_MINUS_SRC_COLOR: return GX2_BLEND_MODE_INV_SRC_COLOR;
  case GL_SRC_ALPHA: return GX2_BLEND_MODE_SRC_ALPHA;
  case GL_ONE_MINUS_SRC_ALPHA: return GX2_BLEND_MODE_INV_SRC_ALPHA;
  case GL_DST_ALPHA: return GX2_BLEND_MODE_DST_ALPHA;
  case GL_ONE_MINUS_DST_ALPHA: return GX2_BLEND_MODE_INV_DST_ALPHA;
  case GL_DST_COLOR: return GX2_BLEND_MODE_DST_COLOR;
  case GL_ONE_MINUS_DST_COLOR: return GX2_BLEND_MODE_INV_DST_COLOR;
  case GL_SRC_ALPHA_SATURATE: return GX2_BLEND_MODE_SRC_ALPHA_SAT;
  case GL_CONSTANT_COLOR: return GX2_BLEND_MODE_BLEND_FACTOR;
  case GL_ONE_MINUS_CONSTANT_COLOR: return GX2_BLEND_MODE_INV_BLEND_FACTOR;
  case GL_CONSTANT_ALPHA: return GX2_BLEND_MODE_CONSTANT_ALPHA;
  case GL_ONE_MINUS_CONSTANT_ALPHA: return GX2_BLEND_MODE_INV_CONSTANT_ALPHA;
  default: return GX2_BLEND_MODE_ONE;
  }
}

static bool valid_blend_equation(GLenum mode) {
  switch (mode) {
  case GL_FUNC_ADD:
  case GL_FUNC_SUBTRACT:
  case GL_FUNC_REVERSE_SUBTRACT:
  case GL_MIN:
  case GL_MAX:
    return true;
  default:
    return false;
  }
}

static GX2BlendCombineMode map_blend_equation(GLenum mode) {
  switch (mode) {
  case GL_FUNC_SUBTRACT: return GX2_BLEND_COMBINE_MODE_SUB;
  case GL_FUNC_REVERSE_SUBTRACT: return GX2_BLEND_COMBINE_MODE_REV_SUB;
  case GL_MIN: return GX2_BLEND_COMBINE_MODE_MIN;
  case GL_MAX: return GX2_BLEND_COMBINE_MODE_MAX;
  default: return GX2_BLEND_COMBINE_MODE_ADD;
  }
}

static bool valid_stencil_op(GLenum op) {
  switch (op) {
  case GL_KEEP:
  case GL_ZERO:
  case GL_REPLACE:
  case GL_INCR:
  case GL_DECR:
  case GL_INVERT:
  case GL_INCR_WRAP:
  case GL_DECR_WRAP:
    return true;
  default:
    return false;
  }
}

static GX2StencilFunction map_stencil_op(GLenum op) {
  switch (op) {
  case GL_ZERO: return GX2_STENCIL_FUNCTION_ZERO;
  case GL_REPLACE: return GX2_STENCIL_FUNCTION_REPLACE;
  case GL_INCR: return GX2_STENCIL_FUNCTION_INCR_CLAMP;
  case GL_DECR: return GX2_STENCIL_FUNCTION_DECR_CLAMP;
  case GL_INVERT: return GX2_STENCIL_FUNCTION_INV;
  case GL_INCR_WRAP: return GX2_STENCIL_FUNCTION_INCR_WRAP;
  case GL_DECR_WRAP: return GX2_STENCIL_FUNCTION_DECR_WRAP;
  default: return GX2_STENCIL_FUNCTION_KEEP;
  }
}

static bool valid_logic_op(GLenum op) {
  switch (op) {
  case GL_CLEAR:
  case GL_SET:
  case GL_COPY:
  case GL_COPY_INVERTED:
  case GL_NOOP:
  case GL_INVERT:
  case GL_AND:
  case GL_NAND:
  case GL_OR:
  case GL_NOR:
  case GL_XOR:
  case GL_EQUIV:
  case GL_AND_REVERSE:
  case GL_AND_INVERTED:
  case GL_OR_REVERSE:
  case GL_OR_INVERTED:
    return true;
  default:
    return false;
  }
}

static GX2LogicOp map_logic_op(GLenum op) {
  switch (op) {
  case GL_CLEAR: return GX2_LOGIC_OP_CLEAR;
  case GL_SET: return GX2_LOGIC_OP_SET;
  case GL_COPY: return GX2_LOGIC_OP_COPY;
  case GL_COPY_INVERTED: return GX2_LOGIC_OP_INV_COPY;
  case GL_NOOP: return GX2_LOGIC_OP_NOP;
  case GL_INVERT: return GX2_LOGIC_OP_INV;
  case GL_AND: return GX2_LOGIC_OP_AND;
  case GL_NAND: return GX2_LOGIC_OP_NOT_AND;
  case GL_OR: return GX2_LOGIC_OP_OR;
  case GL_NOR: return GX2_LOGIC_OP_NOR;
  case GL_XOR: return GX2_LOGIC_OP_XOR;
  case GL_EQUIV: return GX2_LOGIC_OP_EQUIV;
  case GL_AND_REVERSE: return GX2_LOGIC_OP_REV_AND;
  case GL_AND_INVERTED: return GX2_LOGIC_OP_INV_AND;
  case GL_OR_REVERSE: return GX2_LOGIC_OP_REV_OR;
  case GL_OR_INVERTED: return GX2_LOGIC_OP_INV_OR;
  default: return GX2_LOGIC_OP_COPY;
  }
}

static bool valid_face(GLenum face) {
  return face == GL_FRONT || face == GL_BACK || face == GL_FRONT_AND_BACK;
}

static bool valid_front_face(GLenum mode) {
  return mode == GL_CW || mode == GL_CCW;
}

static GX2FrontFace map_front_face(GLenum mode) {
  return mode == GL_CW ? GX2_FRONT_FACE_CW : GX2_FRONT_FACE_CCW;
}

static bool valid_polygon_mode(GLenum mode) {
  return mode == GL_POINT || mode == GL_LINE || mode == GL_FILL;
}

static GX2PolygonMode map_polygon_mode(GLenum mode) {
  switch (mode) {
  case GL_POINT: return GX2_POLYGON_MODE_POINT;
  case GL_LINE: return GX2_POLYGON_MODE_LINE;
  default: return GX2_POLYGON_MODE_TRIANGLE;
  }
}

static int stencil_face_index(GLenum face) {
  return face == GL_BACK ? 1 : 0;
}

static uint32_t dirty_for_cap(GLenum cap) {
  switch (cap) {
  case GL_BLEND:
    return GL_DIRTY_BLEND;
  case GL_COLOR_LOGIC_OP:
    return GL_DIRTY_LOGIC_OP | GL_DIRTY_BLEND;
  case GL_DEPTH_TEST:
  case GL_STENCIL_TEST:
    return GL_DIRTY_DEPTH_STENCIL;
  case GL_CULL_FACE:
    return GL_DIRTY_CULL | GL_DIRTY_POLYGON_MODE;
  case GL_SCISSOR_TEST:
    return GL_DIRTY_SCISSOR;
  case GL_SAMPLE_ALPHA_TO_COVERAGE:
  case GL_SAMPLE_ALPHA_TO_ONE:
  case GL_SAMPLE_COVERAGE:
  case GL_SAMPLE_MASK:
  case GL_MULTISAMPLE:
    return GL_DIRTY_MULTISAMPLE;
  case GL_PRIMITIVE_RESTART:
    return GL_DIRTY_PRIMITIVE_RESTART;
  case GL_RASTERIZER_DISCARD:
    return GL_DIRTY_RASTERIZER_DISCARD;
  case GL_POLYGON_OFFSET_FILL:
  case GL_POLYGON_OFFSET_LINE:
  case GL_POLYGON_OFFSET_POINT:
    return GL_DIRTY_POLYGON_MODE;
  default:
    return 0;
  }
}

static GLboolean *cap_storage(GLenum cap) {
  if (!g_gl_context) return NULL;

  switch (cap) {
  case GL_BLEND:
    return &g_gl_context->blend_enabled;
  case GL_COLOR_LOGIC_OP:
    return &g_gl_context->color_logic_op_enabled;
  case GL_DEPTH_TEST:
    return &g_gl_context->depth_test_enabled;
  case GL_STENCIL_TEST:
    return &g_gl_context->stencil_test_enabled;
  case GL_CULL_FACE:
    return &g_gl_context->cull_face_enabled;
  case GL_SCISSOR_TEST:
    return &g_gl_context->scissor_test_enabled;
  case GL_SAMPLE_ALPHA_TO_COVERAGE:
    return &g_gl_context->sample_alpha_to_coverage_enabled;
  case GL_SAMPLE_ALPHA_TO_ONE:
    return &g_gl_context->sample_alpha_to_one_enabled;
  case GL_SAMPLE_COVERAGE:
    return &g_gl_context->sample_coverage_enabled;
  case GL_SAMPLE_MASK:
    return &g_gl_context->sample_mask_enabled;
  case GL_MULTISAMPLE:
    return &g_gl_context->multisample_enabled;
  case GL_PRIMITIVE_RESTART:
    return &g_gl_context->primitive_restart_enabled;
  case GL_RASTERIZER_DISCARD:
    return &g_gl_context->rasterizer_discard_enabled;
  case GL_POLYGON_OFFSET_FILL:
    return &g_gl_context->polygon_offset_fill_enabled;
  case GL_POLYGON_OFFSET_LINE:
    return &g_gl_context->polygon_offset_line_enabled;
  case GL_POLYGON_OFFSET_POINT:
    return &g_gl_context->polygon_offset_point_enabled;
  default:
    return NULL;
  }
}

static GLRect draw_target_rect(void) {
  GX2ColorBuffer *color = gl_get_draw_color_buffer(0);
  GLRect rect;

  rect.x = 0;
  rect.y = 0;
  rect.width = color ? (GLint)color->surface.width : g_gl_context->viewport.width;
  rect.height = color ? (GLint)color->surface.height : g_gl_context->viewport.height;
  if (rect.width < 0) rect.width = 0;
  if (rect.height < 0) rect.height = 0;
  return rect;
}

static GLRect intersect_rect(GLRect a, GLRect b) {
  GLint ax1 = a.x + a.width;
  GLint ay1 = a.y + a.height;
  GLint bx1 = b.x + b.width;
  GLint by1 = b.y + b.height;
  GLRect out;

  out.x = a.x > b.x ? a.x : b.x;
  out.y = a.y > b.y ? a.y : b.y;
  ax1 = ax1 < bx1 ? ax1 : bx1;
  ay1 = ay1 < by1 ? ay1 : by1;
  out.width = ax1 - out.x;
  out.height = ay1 - out.y;
  if (out.width < 0) out.width = 0;
  if (out.height < 0) out.height = 0;
  return out;
}

static bool current_clear_rect(GX2Surface *surface, GLRect *rect) {
  GLRect surface_rect;

  if (!g_gl_context || !surface || !rect) return false;

  surface_rect.x = 0;
  surface_rect.y = 0;
  surface_rect.width = (GLint)surface->width;
  surface_rect.height = (GLint)surface->height;

  if (g_gl_context->scissor_test_enabled) {
    GLRect scissor;
    scissor.x = g_gl_context->scissor.x;
    scissor.y = g_gl_context->scissor.y;
    scissor.width = g_gl_context->scissor.width;
    scissor.height = g_gl_context->scissor.height;
    *rect = intersect_rect(surface_rect, scissor);
  } else {
    *rect = surface_rect;
  }

  return rect->width > 0 && rect->height > 0;
}

static void write_rgba8_clear_pixel(GX2Surface *surface, GLint x, GLint y,
                                    const uint8_t clear[4]) {
  uint8_t *dst = (uint8_t *)surface->image +
                 (((size_t)y * (size_t)surface->pitch) + (size_t)x) * 4u;
  uint32_t packed;
  uint32_t native;
  uint8_t old[4];

  memcpy(&packed, dst, sizeof(packed));
  native = GPU_TO_CPU_32(packed);
  old[0] = (uint8_t)(native >> 24);
  old[1] = (uint8_t)(native >> 16);
  old[2] = (uint8_t)(native >> 8);
  old[3] = (uint8_t)native;

  if (g_gl_context->color_mask[0]) old[0] = clear[0];
  if (g_gl_context->color_mask[1]) old[1] = clear[1];
  if (g_gl_context->color_mask[2]) old[2] = clear[2];
  if (g_gl_context->color_mask[3]) old[3] = clear[3];

  native = ((uint32_t)old[0] << 24) |
           ((uint32_t)old[1] << 16) |
           ((uint32_t)old[2] << 8) |
           (uint32_t)old[3];
  packed = CPU_TO_GPU_32(native);
  memcpy(dst, &packed, sizeof(packed));
}

static bool get_color_buffer_cpu_view(const GX2ColorBuffer *color_buffer,
                                      GX2Surface *view) {
  const GX2Surface *source;
  GX2Surface layout;
  uint8_t *level_base;
  uint32_t level;
  uint32_t depth;
  uint32_t slice_size;

  if (!color_buffer || !view) return false;

  source = &color_buffer->surface;
  level = color_buffer->viewMip;
  if (!source->image || level >= source->mipLevels) return false;

  depth = source->depth ? source->depth : 1u;
  if (source->dim == GX2_SURFACE_DIM_TEXTURE_3D) {
    depth >>= level;
    if (depth == 0) depth = 1;
  }
  if (color_buffer->viewFirstSlice >= depth) return false;

  level_base = level == 0
                   ? (uint8_t *)source->image
                   : (source->mipmaps
                          ? (uint8_t *)source->mipmaps +
                                source->mipLevelOffset[level - 1]
                          : NULL);
  if (!level_base) return false;

  memset(&layout, 0, sizeof(layout));
  layout.dim = source->dim;
  layout.width = source->width >> level;
  layout.height = source->height >> level;
  layout.depth = depth;
  if (layout.width == 0) layout.width = 1;
  if (layout.height == 0) layout.height = 1;
  layout.mipLevels = 1;
  layout.format = source->format;
  layout.aa = source->aa;
  layout.use = source->use;
  layout.tileMode = source->tileMode;
  GX2CalcSurfaceSizeAndAlignment(&layout);

  slice_size = layout.imageSize / depth;
  *view = layout;
  view->depth = 1;
  view->image = level_base +
                (size_t)color_buffer->viewFirstSlice * (size_t)slice_size;
  view->imageSize = slice_size;
  view->mipmaps = NULL;
  view->mipmapSize = 0;
  return true;
}

static bool cpu_clear_draw_color_buffer(GLuint index, const GLfloat *clear_color) {
  GX2ColorBuffer *cb;
  GX2Surface view;
  GX2Surface *surface;
  GLRect rect;
  uint8_t clear[4];

  if (!g_gl_context || !clear_color || g_gl_context->bound_framebuffer == 0) {
    return false;
  }
  if (!gl_is_draw_color_buffer_enabled(index)) {
    return true;
  }

  cb = gl_get_draw_color_buffer(index);
  if (!cb || !cb->surface.image) {
    return false;
  }

  if (!get_color_buffer_cpu_view(cb, &view)) {
    return false;
  }
  surface = &view;
  if (surface->tileMode != GX2_TILE_MODE_LINEAR_ALIGNED) {
    return false;
  }
  if (!current_clear_rect(surface, &rect)) {
    return true;
  }

  clear[0] = clamp_clear_unorm8(clear_color[0]);
  clear[1] = clamp_clear_unorm8(clear_color[1]);
  clear[2] = clamp_clear_unorm8(clear_color[2]);
  clear[3] = clamp_clear_unorm8(clear_color[3]);

  for (GLint y = rect.y; y < rect.y + rect.height; ++y) {
    for (GLint x = rect.x; x < rect.x + rect.width; ++x) {
      switch (surface->format) {
      case GX2_SURFACE_FORMAT_UNORM_R8: {
        uint8_t *dst = (uint8_t *)surface->image +
                       (size_t)y * (size_t)surface->pitch +
                       (size_t)x;
        if (g_gl_context->color_mask[0]) dst[0] = clear[0];
        break;
      }
      case GX2_SURFACE_FORMAT_UNORM_R8_G8: {
        uint8_t *dst = (uint8_t *)surface->image +
                       (((size_t)y * (size_t)surface->pitch) + (size_t)x) * 2u;
        if (g_gl_context->color_mask[0]) dst[0] = clear[0];
        if (g_gl_context->color_mask[1]) dst[1] = clear[1];
        break;
      }
      case GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8:
        write_rgba8_clear_pixel(surface, x, y, clear);
        break;
      default:
        return false;
      }
    }
  }

  DCFlushRange(surface->image, surface->imageSize);
  GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER |
                                    GX2_INVALIDATE_MODE_TEXTURE),
                surface->image, surface->imageSize);
  return true;
}

GLboolean gl_state_cpu_clear_draw_color_buffer(GLuint index,
                                               const GLfloat *clear_color) {
  return cpu_clear_draw_color_buffer(index, clear_color) ? GL_TRUE : GL_FALSE;
}

void _gl_ClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
  if (!g_gl_context) return;
  g_gl_context->clear_color[0] = clamp01f(red);
  g_gl_context->clear_color[1] = clamp01f(green);
  g_gl_context->clear_color[2] = clamp01f(blue);
  g_gl_context->clear_color[3] = clamp01f(alpha);
}

void _gl_ClearDepth(GLclampd depth) {
  if (!g_gl_context) return;
  g_gl_context->clear_depth = clamp01f((GLfloat)depth);
}

void _gl_ClearStencil(GLint s) {
  if (!g_gl_context) return;
  g_gl_context->clear_stencil = s;
}

void _gl_Clear(GLbitfield mask) {
  GX2DepthBuffer *depth;
  GX2ClearFlags depth_stencil_flags = (GX2ClearFlags)0;

  if (!g_gl_context) return;
  if ((mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (mask == 0) return;

  gl_flush_state();

  if (mask & GL_COLOR_BUFFER_BIT) {
    for (GLuint i = 0; i < 8; ++i) {
      GX2ColorBuffer *color;
      if (!gl_is_draw_color_buffer_enabled(i)) continue;
      if (!cpu_clear_draw_color_buffer(i, g_gl_context->clear_color)) {
        color = gl_get_draw_color_buffer(i);
        if (color) {
          GX2ClearColor(color,
                        g_gl_context->clear_color[0],
                        g_gl_context->clear_color[1],
                        g_gl_context->clear_color[2],
                        g_gl_context->clear_color[3]);
        }
      }
    }
    gl_framebuffer_mark_bound_color_dirty();
  }

  depth = gl_get_draw_depth_buffer();
  if (!depth) return;

  if ((mask & GL_DEPTH_BUFFER_BIT) && g_gl_context->depth_mask) {
    depth_stencil_flags = (GX2ClearFlags)(depth_stencil_flags | GX2_CLEAR_FLAGS_DEPTH);
  }
  if ((mask & GL_STENCIL_BUFFER_BIT) &&
      (g_gl_context->stencil_write_mask[0] != 0 ||
       g_gl_context->stencil_write_mask[1] != 0)) {
    depth_stencil_flags = (GX2ClearFlags)(depth_stencil_flags | GX2_CLEAR_FLAGS_STENCIL);
  }

  if (depth_stencil_flags != 0) {
    GX2ClearDepthStencilEx(depth,
                           (mask & GL_DEPTH_BUFFER_BIT)
                               ? g_gl_context->clear_depth
                               : depth->depthClear,
                           (mask & GL_STENCIL_BUFFER_BIT)
                               ? (uint8_t)g_gl_context->clear_stencil
                               : (uint8_t)depth->stencilClear,
                           depth_stencil_flags);
  }
}

void _gl_Enable(GLenum cap) {
  GLboolean *enabled = cap_storage(cap);
  if (!enabled) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  *enabled = GL_TRUE;
  mark_dirty(dirty_for_cap(cap));
}

void _gl_Disable(GLenum cap) {
  GLboolean *enabled = cap_storage(cap);
  if (!enabled) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  *enabled = GL_FALSE;
  mark_dirty(dirty_for_cap(cap));
}

GLboolean _gl_IsEnabled(GLenum cap) {
  GLboolean *enabled = cap_storage(cap);
  if (!enabled) {
    _gl_set_error(GL_INVALID_ENUM);
    return GL_FALSE;
  }
  return *enabled ? GL_TRUE : GL_FALSE;
}

void _gl_LogicOp(GLenum opcode) {
  if (!g_gl_context) return;
  if (!valid_logic_op(opcode)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->logic_op = opcode;
  mark_dirty(GL_DIRTY_LOGIC_OP);
}

void _gl_PointSize(GLfloat size) {
  if (!g_gl_context) return;
  if (size <= 0.0f) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  g_gl_context->point_size = size;
  mark_dirty(GL_DIRTY_POINT_SIZE);
}

void _gl_LineWidth(GLfloat width) {
  if (!g_gl_context) return;
  if (width <= 0.0f) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  g_gl_context->line_width = width;
  mark_dirty(GL_DIRTY_LINE_WIDTH);
}

void _gl_Viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
  if (!g_gl_context) return;
  if (width < 0 || height < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  g_gl_context->viewport.x = x;
  g_gl_context->viewport.y = y;
  g_gl_context->viewport.width = width;
  g_gl_context->viewport.height = height;
  mark_dirty(GL_DIRTY_VIEWPORT);
}

void _gl_Scissor(GLint x, GLint y, GLsizei width, GLsizei height) {
  if (!g_gl_context) return;
  if (width < 0 || height < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  g_gl_context->scissor.x = x;
  g_gl_context->scissor.y = y;
  g_gl_context->scissor.width = width;
  g_gl_context->scissor.height = height;
  mark_dirty(GL_DIRTY_SCISSOR);
}

void _gl_ColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
  if (!g_gl_context) return;
  g_gl_context->color_mask[0] = gl_bool(red);
  g_gl_context->color_mask[1] = gl_bool(green);
  g_gl_context->color_mask[2] = gl_bool(blue);
  g_gl_context->color_mask[3] = gl_bool(alpha);
  mark_dirty(GL_DIRTY_COLOR_MASK);
}

void _gl_Hint(GLenum target, GLenum mode) {
  if (!g_gl_context) return;
  if (mode != GL_DONT_CARE && mode != GL_FASTEST && mode != GL_NICEST) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (target != GL_GENERATE_MIPMAP_HINT) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->generate_mipmap_hint = mode;
}

void _gl_PixelStorei(GLenum pname, GLint param) {
  GLint *slot = NULL;

  if (!g_gl_context) return;

  switch (pname) {
  case GL_PACK_ALIGNMENT:
    slot = &g_gl_context->pack_alignment;
    break;
  case GL_UNPACK_ALIGNMENT:
    slot = &g_gl_context->unpack_alignment;
    break;
  case GL_PACK_ROW_LENGTH:
    slot = &g_gl_context->pack_row_length;
    break;
  case GL_PACK_SKIP_ROWS:
    slot = &g_gl_context->pack_skip_rows;
    break;
  case GL_PACK_SKIP_PIXELS:
    slot = &g_gl_context->pack_skip_pixels;
    break;
  case GL_PACK_IMAGE_HEIGHT:
    slot = &g_gl_context->pack_image_height;
    break;
  case GL_PACK_SKIP_IMAGES:
    slot = &g_gl_context->pack_skip_images;
    break;
  case GL_UNPACK_ROW_LENGTH:
    slot = &g_gl_context->unpack_row_length;
    break;
  case GL_UNPACK_SKIP_ROWS:
    slot = &g_gl_context->unpack_skip_rows;
    break;
  case GL_UNPACK_SKIP_PIXELS:
    slot = &g_gl_context->unpack_skip_pixels;
    break;
  case GL_UNPACK_IMAGE_HEIGHT:
    slot = &g_gl_context->unpack_image_height;
    break;
  case GL_UNPACK_SKIP_IMAGES:
    slot = &g_gl_context->unpack_skip_images;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  if (pname == GL_PACK_ALIGNMENT || pname == GL_UNPACK_ALIGNMENT) {
    if (param != 1 && param != 2 && param != 4 && param != 8) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
  } else if (param < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  *slot = param;
}

void _gl_SampleCoverage(GLclampf value, GLboolean invert) {
  if (!g_gl_context) return;
  g_gl_context->sample_coverage_value = clamp01f(value);
  g_gl_context->sample_coverage_invert = gl_bool(invert);
  mark_dirty(GL_DIRTY_MULTISAMPLE);
}

void _gl_BlendFunc(GLenum sfactor, GLenum dfactor) {
  _gl_BlendFuncSeparate(sfactor, dfactor, sfactor, dfactor);
}

void _gl_BlendEquation(GLenum mode) {
  _gl_BlendEquationSeparate(mode, mode);
}

void _gl_BlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
  if (!g_gl_context) return;
  if (!valid_blend_equation(modeRGB) || !valid_blend_equation(modeAlpha)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->blend_eq_rgb = modeRGB;
  g_gl_context->blend_eq_alpha = modeAlpha;
  mark_dirty(GL_DIRTY_BLEND);
}

void _gl_BlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB,
                           GLenum sfactorAlpha, GLenum dfactorAlpha) {
  if (!g_gl_context) return;
  if (!valid_blend_factor(sfactorRGB) || !valid_blend_factor(dfactorRGB) ||
      !valid_blend_factor(sfactorAlpha) || !valid_blend_factor(dfactorAlpha)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->blend_src_rgb = sfactorRGB;
  g_gl_context->blend_dst_rgb = dfactorRGB;
  g_gl_context->blend_src_alpha = sfactorAlpha;
  g_gl_context->blend_dst_alpha = dfactorAlpha;
  mark_dirty(GL_DIRTY_BLEND);
}

void _gl_BlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
  if (!g_gl_context) return;
  g_gl_context->blend_color[0] = clamp01f(red);
  g_gl_context->blend_color[1] = clamp01f(green);
  g_gl_context->blend_color[2] = clamp01f(blue);
  g_gl_context->blend_color[3] = clamp01f(alpha);
  mark_dirty(GL_DIRTY_BLEND);
}

void _gl_DepthFunc(GLenum func) {
  if (!g_gl_context) return;
  if (!valid_compare_func(func)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->depth_func = func;
  mark_dirty(GL_DIRTY_DEPTH_STENCIL);
}

void _gl_DepthMask(GLboolean flag) {
  if (!g_gl_context) return;
  g_gl_context->depth_mask = gl_bool(flag);
  mark_dirty(GL_DIRTY_DEPTH_STENCIL);
}

void _gl_DepthRange(GLclampd nearVal, GLclampd farVal) {
  if (!g_gl_context) return;
  g_gl_context->viewport.near_z = clamp01f((GLfloat)nearVal);
  g_gl_context->viewport.far_z = clamp01f((GLfloat)farVal);
  mark_dirty(GL_DIRTY_VIEWPORT);
}

void _gl_StencilFunc(GLenum func, GLint ref, GLuint mask) {
  _gl_StencilFuncSeparate(GL_FRONT_AND_BACK, func, ref, mask);
}

void _gl_StencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
  if (!g_gl_context) return;
  if (!valid_face(face) || !valid_compare_func(func)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  for (int i = 0; i < 2; ++i) {
    if (face == GL_FRONT_AND_BACK || i == stencil_face_index(face)) {
      g_gl_context->stencil_func[i] = func;
      g_gl_context->stencil_ref[i] = clamp_stencil_ref(ref);
      g_gl_context->stencil_compare_mask[i] = mask;
    }
  }
  mark_dirty(GL_DIRTY_DEPTH_STENCIL);
}

void _gl_StencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
  _gl_StencilOpSeparate(GL_FRONT_AND_BACK, fail, zfail, zpass);
}

void _gl_StencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass) {
  if (!g_gl_context) return;
  if (!valid_face(face) ||
      !valid_stencil_op(fail) ||
      !valid_stencil_op(zfail) ||
      !valid_stencil_op(zpass)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  for (int i = 0; i < 2; ++i) {
    if (face == GL_FRONT_AND_BACK || i == stencil_face_index(face)) {
      g_gl_context->stencil_fail[i] = fail;
      g_gl_context->stencil_zfail[i] = zfail;
      g_gl_context->stencil_zpass[i] = zpass;
    }
  }
  mark_dirty(GL_DIRTY_DEPTH_STENCIL);
}

void _gl_StencilMask(GLuint mask) {
  _gl_StencilMaskSeparate(GL_FRONT_AND_BACK, mask);
}

void _gl_StencilMaskSeparate(GLenum face, GLuint mask) {
  if (!g_gl_context) return;
  if (!valid_face(face)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  for (int i = 0; i < 2; ++i) {
    if (face == GL_FRONT_AND_BACK || i == stencil_face_index(face)) {
      g_gl_context->stencil_write_mask[i] = mask;
    }
  }
  mark_dirty(GL_DIRTY_DEPTH_STENCIL);
}

void _gl_CullFace(GLenum mode) {
  if (!g_gl_context) return;
  if (!valid_face(mode)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->cull_face_mode = mode;
  mark_dirty(GL_DIRTY_CULL | GL_DIRTY_POLYGON_MODE);
}

void _gl_FrontFace(GLenum mode) {
  if (!g_gl_context) return;
  if (!valid_front_face(mode)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->front_face = mode;
  mark_dirty(GL_DIRTY_CULL | GL_DIRTY_POLYGON_MODE);
}

void _gl_PolygonMode(GLenum face, GLenum mode) {
  if (!g_gl_context) return;
  if (face != GL_FRONT_AND_BACK) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!valid_polygon_mode(mode)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->polygon_mode = mode;
  mark_dirty(GL_DIRTY_POLYGON_MODE);
}

void _gl_PolygonOffset(GLfloat factor, GLfloat units) {
  if (!g_gl_context) return;
  g_gl_context->polygon_offset_factor = factor;
  g_gl_context->polygon_offset_units = units;
  mark_dirty(GL_DIRTY_POLYGON_MODE);
}

static bool cull_front_enabled(void) {
  return g_gl_context->cull_face_enabled &&
         (g_gl_context->cull_face_mode == GL_FRONT ||
          g_gl_context->cull_face_mode == GL_FRONT_AND_BACK);
}

static bool cull_back_enabled(void) {
  return g_gl_context->cull_face_enabled &&
         (g_gl_context->cull_face_mode == GL_BACK ||
          g_gl_context->cull_face_mode == GL_FRONT_AND_BACK);
}

static bool polygon_offset_enabled_for_mode(void) {
  switch (g_gl_context->polygon_mode) {
  case GL_POINT:
    return g_gl_context->polygon_offset_point_enabled;
  case GL_LINE:
    return g_gl_context->polygon_offset_line_enabled;
  default:
    return g_gl_context->polygon_offset_fill_enabled;
  }
}

static void emit_alpha_state(void) {
  GX2SetAlphaTest(GX2_DISABLE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);
  GX2SetAlphaToMask(g_gl_context->sample_alpha_to_coverage_enabled
                        ? GX2_ENABLE
                        : GX2_DISABLE,
                    GX2_ALPHA_TO_MASK_MODE_NON_DITHERED);
}

static void emit_multisample_state(void) {
  GLsizei samples = gl_get_draw_sample_count();
  GLsizei programmed_samples = samples > 0 ? samples : 1;
  const int8_t (*offsets)[2] = sample_offsets(programmed_samples);
  GX2GLAASampleLoc locations;
  GX2AAMode aa_mode = GX2_AA_MODE1X;
  uint32_t active_mask;
  uint32_t mask;
  GX2AAMaskReg mask_reg;

  memset(&locations, 0, sizeof(locations));
  if (programmed_samples == 2) aa_mode = GX2_AA_MODE2X;
  else if (programmed_samples == 4) aa_mode = GX2_AA_MODE4X;
  else if (programmed_samples == 8) aa_mode = GX2_AA_MODE8X;
  if (offsets) {
    for (GLsizei i = 0; i < programmed_samples; ++i) {
      locations.x[i] = offsets[i][0];
      locations.y[i] = offsets[i][1];
    }
  }
  GX2SetAAMode(aa_mode);
  GX2SetAAModeEx(&locations, aa_mode);

  if (samples <= 0) {
    GX2InitAAMaskReg(&mask_reg, 0xFF, 0xFF, 0xFF, 0xFF);
    GX2SetAAMaskReg(&mask_reg);
    return;
  }

  active_mask = samples >= 8 ? 0xFFu : ((1u << (uint32_t)samples) - 1u);
  mask = active_mask;
  if (g_gl_context->sample_coverage_enabled) {
    uint32_t covered = (uint32_t)(g_gl_context->sample_coverage_value *
                                  (GLfloat)samples + 0.5f);
    uint32_t coverage_mask;
    if (covered > (uint32_t)samples) covered = (uint32_t)samples;
    coverage_mask = covered == 0 ? 0u : ((1u << covered) - 1u);
    if (g_gl_context->sample_coverage_invert) {
      coverage_mask = (~coverage_mask) & active_mask;
    }
    mask &= coverage_mask;
  }
  if (g_gl_context->sample_mask_enabled) {
    mask &= g_gl_context->sample_mask_value;
  }

  GX2InitAAMaskReg(&mask_reg, (uint8_t)mask, (uint8_t)mask,
                   (uint8_t)mask, (uint8_t)mask);
  GX2SetAAMaskReg(&mask_reg);
}

static void emit_viewport_state(void) {
  GLRect target = draw_target_rect();
  GLfloat flipped_y = (GLfloat)target.height -
                      (GLfloat)g_gl_context->viewport.y -
                      (GLfloat)g_gl_context->viewport.height;

  GX2SetViewport((GLfloat)g_gl_context->viewport.x,
                 flipped_y,
                 (GLfloat)g_gl_context->viewport.width,
                 (GLfloat)g_gl_context->viewport.height,
                 g_gl_context->viewport.near_z,
                 g_gl_context->viewport.far_z);
}

static void emit_scissor_state(void) {
  GLRect target = draw_target_rect();
  GLRect scissor = target;

  if (g_gl_context->scissor_test_enabled) {
    GLRect requested;
    requested.x = g_gl_context->scissor.x;
    requested.y = g_gl_context->scissor.y;
    requested.width = g_gl_context->scissor.width;
    requested.height = g_gl_context->scissor.height;
    scissor = intersect_rect(target, requested);
  }

  GX2SetScissor((uint32_t)scissor.x,
                (uint32_t)(target.height - (scissor.y + scissor.height)),
                (uint32_t)scissor.width,
                (uint32_t)scissor.height);
}

static void emit_blend_state(void) {
  uint8_t blend_mask = (g_gl_context->blend_enabled &&
                        !g_gl_context->color_logic_op_enabled)
                           ? 0xFF
                           : 0x00;
  GX2LogicOp logic_op = g_gl_context->color_logic_op_enabled
                            ? map_logic_op(g_gl_context->logic_op)
                            : GX2_LOGIC_OP_COPY;

  GX2SetColorControl(logic_op, blend_mask, GX2_FALSE, GX2_TRUE);

  for (GLuint i = 0; i < 8; ++i) {
    GX2SetBlendControl((GX2RenderTarget)i,
                       map_blend_factor(g_gl_context->blend_src_rgb),
                       map_blend_factor(g_gl_context->blend_dst_rgb),
                       map_blend_equation(g_gl_context->blend_eq_rgb),
                       g_gl_context->blend_enabled ? GX2_ENABLE : GX2_DISABLE,
                       map_blend_factor(g_gl_context->blend_src_alpha),
                       map_blend_factor(g_gl_context->blend_dst_alpha),
                       map_blend_equation(g_gl_context->blend_eq_alpha));
  }

  GX2SetBlendConstantColor(g_gl_context->blend_color[0],
                           g_gl_context->blend_color[1],
                           g_gl_context->blend_color[2],
                           g_gl_context->blend_color[3]);
}

static void emit_color_mask_state(void) {
  uint8_t mask = 0;

  if (g_gl_context->color_mask[0]) mask |= GX2_CHANNEL_MASK_R;
  if (g_gl_context->color_mask[1]) mask |= GX2_CHANNEL_MASK_G;
  if (g_gl_context->color_mask[2]) mask |= GX2_CHANNEL_MASK_B;
  if (g_gl_context->color_mask[3]) mask |= GX2_CHANNEL_MASK_A;

  GX2SetTargetChannelMasks((GX2ChannelMask)mask, (GX2ChannelMask)mask,
                           (GX2ChannelMask)mask, (GX2ChannelMask)mask,
                           (GX2ChannelMask)mask, (GX2ChannelMask)mask,
                           (GX2ChannelMask)mask, (GX2ChannelMask)mask);
}

static void emit_depth_stencil_state(void) {
  GX2SetDepthStencilControl(
      g_gl_context->depth_test_enabled ? GX2_ENABLE : GX2_DISABLE,
      g_gl_context->depth_mask ? GX2_ENABLE : GX2_DISABLE,
      map_compare_func(g_gl_context->depth_func),
      g_gl_context->stencil_test_enabled ? GX2_ENABLE : GX2_DISABLE,
      g_gl_context->stencil_test_enabled ? GX2_ENABLE : GX2_DISABLE,
      map_compare_func(g_gl_context->stencil_func[0]),
      map_stencil_op(g_gl_context->stencil_zpass[0]),
      map_stencil_op(g_gl_context->stencil_zfail[0]),
      map_stencil_op(g_gl_context->stencil_fail[0]),
      map_compare_func(g_gl_context->stencil_func[1]),
      map_stencil_op(g_gl_context->stencil_zpass[1]),
      map_stencil_op(g_gl_context->stencil_zfail[1]),
      map_stencil_op(g_gl_context->stencil_fail[1]));

  GX2SetStencilMask((uint8_t)g_gl_context->stencil_compare_mask[0],
                    (uint8_t)g_gl_context->stencil_write_mask[0],
                    (uint8_t)g_gl_context->stencil_ref[0],
                    (uint8_t)g_gl_context->stencil_compare_mask[1],
                    (uint8_t)g_gl_context->stencil_write_mask[1],
                    (uint8_t)g_gl_context->stencil_ref[1]);
}

static void emit_raster_state(void) {
  GX2PolygonMode mode = map_polygon_mode(g_gl_context->polygon_mode);
  GX2PolygonControlReg polygon;
  bool polygon_mode_enabled = mode != GX2_POLYGON_MODE_TRIANGLE;
  bool offset_enabled = polygon_offset_enabled_for_mode();

  GX2SetCullOnlyControl(map_front_face(g_gl_context->front_face),
                        cull_front_enabled(),
                        cull_back_enabled());
  GX2InitPolygonControlReg(&polygon,
                           map_front_face(g_gl_context->front_face),
                           cull_front_enabled(),
                           cull_back_enabled(),
                           polygon_mode_enabled ? GX2_ENABLE : GX2_DISABLE,
                           mode,
                           mode,
                           offset_enabled ? GX2_ENABLE : GX2_DISABLE,
                           offset_enabled ? GX2_ENABLE : GX2_DISABLE,
                           GX2_DISABLE);
  if (g_gl_context->provoking_vertex == GL_LAST_VERTEX_CONVENTION) {
    polygon.pa_su_sc_mode_cntl |= GX2GL_PA_SU_SC_MODE_CNTL_PROVOKING_VTX_LAST;
  } else {
    polygon.pa_su_sc_mode_cntl &= ~GX2GL_PA_SU_SC_MODE_CNTL_PROVOKING_VTX_LAST;
  }
  GX2SetPolygonControlReg(&polygon);

  if (offset_enabled) {
    GX2SetPolygonOffset(g_gl_context->polygon_offset_units,
                        g_gl_context->polygon_offset_factor,
                        g_gl_context->polygon_offset_units,
                        g_gl_context->polygon_offset_factor,
                        0.0f);
  }
}

static void emit_rasterizer_discard_state(void) {
  GX2SetRasterizerClipControl(
      g_gl_context->rasterizer_discard_enabled ? GX2_DISABLE : GX2_ENABLE,
      GX2_ENABLE);
}

void gl_flush_state(void) {
  uint32_t dirty;

  if (!g_gl_context) return;

  dirty = g_gl_context->dirty_flags;
  gl_bind_framebuffers();
  emit_alpha_state();

  if (dirty & (GL_DIRTY_VIEWPORT | GL_DIRTY_FRAMEBUFFER)) {
    emit_viewport_state();
  }
  if (dirty & (GL_DIRTY_SCISSOR | GL_DIRTY_FRAMEBUFFER)) {
    emit_scissor_state();
  }
  if (dirty & (GL_DIRTY_BLEND | GL_DIRTY_LOGIC_OP)) {
    emit_blend_state();
  }
  if (dirty & GL_DIRTY_COLOR_MASK) {
    emit_color_mask_state();
  }
  if (dirty & GL_DIRTY_DEPTH_STENCIL) {
    emit_depth_stencil_state();
  }
  if (dirty & GL_DIRTY_POINT_SIZE) {
    GX2SetPointSize(g_gl_context->point_size, g_gl_context->point_size);
  }
  if (dirty & GL_DIRTY_LINE_WIDTH) {
    GX2SetLineWidth(g_gl_context->line_width);
  }
  if (dirty & (GL_DIRTY_CULL | GL_DIRTY_POLYGON_MODE |
               GL_DIRTY_PROVOKING_VERTEX)) {
    emit_raster_state();
  }
  if (dirty & GL_DIRTY_RASTERIZER_DISCARD) {
    emit_rasterizer_discard_state();
  }
  if (dirty & (GL_DIRTY_MULTISAMPLE | GL_DIRTY_FRAMEBUFFER)) {
    emit_multisample_state();
  }

  gl_bind_shaders();
  gl_bind_program_fetch_shader();
  gl_bind_textures();
  gl_bind_vao();
  g_gl_context->dirty_flags = 0;
}

#ifdef __cplusplus
}
#endif
