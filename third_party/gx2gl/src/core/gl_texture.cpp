#include "gl_texture.h"
#include "gl_buffer.h"
#include "gl_context.h"
#include "gl_framebuffer.h"
#include "gl_shader.h"
#include "state/gl_state.h"
#include "endian/endian.h"
#include "mem/gl_mem.h"
#include "Platform/WiiU_Log.hpp"
#include <coreinit/cache.h>
#include <gx2/clear.h>
#include <gx2/enum.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#endif

#ifndef GL_UNSIGNED_SHORT_5_5_5_1
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#endif

#ifndef GL_UNSIGNED_SHORT_4_4_4_4
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#endif

#ifndef GL_RGBA4
#define GL_RGBA4 0x8056
#endif

#ifndef GL_RGB5_A1
#define GL_RGB5_A1 0x8057
#endif

#ifndef GL_RGB565
#define GL_RGB565 0x8D62
#endif

#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#endif

#ifndef GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A
#endif

#define MAX_TEXTURES 2048
#define MAX_SAMPLER_OBJECTS 512
#define GX2GL_MAX_TEXTURE_LEVELS 14
#define GX2GL_CUBE_FACE_MASK 0x3Fu

typedef struct {
  GX2Texture gx2_texture;
  GX2Sampler gx2_sampler;
  GLenum target;
  GLint internal_format;
  GLsizei width, height, depth;
  GLsizei samples;
  GLboolean fixed_sample_locations;
  GLint base_level, max_level;
  GLfloat min_lod, max_lod, lod_bias;
  GLfloat border_color[4];
  GLenum compare_mode;
  GLenum compare_func;
  GLenum swizzle[4];
  uint16_t defined_levels;
  uint8_t cube_defined_faces[GX2GL_MAX_TEXTURE_LEVELS];
  bool reserved;
  bool in_use;
  bool complete;
  bool pending_delete;
  bool storage_allocated;

  GLenum min_filter;
  GLenum mag_filter;
  GLenum wrap_s;
  GLenum wrap_t;
  GLenum wrap_r;
} GLTexture;

typedef struct {
  GX2SurfaceFormat gx2_format;
  GX2SurfaceUse surface_use;
  uint32_t comp_map;
  uint8_t src_components;
  uint8_t dst_components;
  uint8_t bytes_per_component;
  uint8_t src_bytes_per_texel;
  uint8_t dst_bytes_per_texel;
  bool packed_u32;
  bool mipmap_supported;
  bool compressed;
  uint8_t block_bytes;
} TextureFormatInfo;

typedef struct {
  GX2SurfaceDim dim;
  GLsizei width;
  GLsizei height;
  GLsizei depth;
  uint32_t pitch;
  uint32_t image_size;
  uint32_t slice_size;
} TextureLevelLayout;

static GLTexture g_textures[MAX_TEXTURES];
static GLTexture g_default_texture_1d;
static GLTexture g_default_texture_2d;
static GLTexture g_default_texture_3d;
static GLTexture g_default_texture_cube;
static GLTexture g_default_texture_1d_array;
static GLTexture g_default_texture_2d_array;
static GLTexture g_default_texture_2d_multisample;
static GLTexture g_default_texture_2d_multisample_array;

typedef struct {
  GLsizei width;
  GLsizei height;
  GLsizei depth;
  GLsizei samples;
  GLint internal_format;
  GLboolean fixed_sample_locations;
  bool defined;
} GLProxyMultisampleImage;

static GLProxyMultisampleImage g_proxy_multisample_2d;
static GLProxyMultisampleImage g_proxy_multisample_array;

typedef struct {
  GX2Sampler gx2_sampler;
  bool in_use;
  GLenum min_filter;
  GLenum mag_filter;
  GLenum wrap_s;
  GLenum wrap_t;
  GLenum wrap_r;
  GLfloat min_lod;
  GLfloat max_lod;
  GLfloat lod_bias;
  GLfloat border_color[4];
  GLenum compare_mode;
  GLenum compare_func;
} GLSampler;

static GLSampler g_samplers[MAX_SAMPLER_OBJECTS];

#ifndef GX2GL_ENABLE_VERBOSE_FILE_LOGS
#define GX2GL_ENABLE_VERBOSE_FILE_LOGS 0
#endif

static void log_texture_step(const char *format, ...) {
#if GX2GL_ENABLE_VERBOSE_FILE_LOGS
  char buffer[1024];
  va_list args;
  FILE *log_file;

  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  WiiU_Log("%s", buffer);
  log_file = fopen(WiiU_GetGX2GLInitLogPath(), "a");
  if (log_file) {
    fprintf(log_file, "[gx2gl-texture] %s\n", buffer);
    fclose(log_file);
  }
#else
  (void)format;
#endif
}

static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

static bool texture_ptr_is_named(const GLTexture *tex) {
  return tex >= g_textures && tex < g_textures + MAX_TEXTURES;
}

static GLuint texture_name_from_ptr(const GLTexture *tex) {
  return texture_ptr_is_named(tex) ? (GLuint)(tex - g_textures) : 0;
}

static GLTexture *default_texture_for_target(GLenum target) {
  switch (target) {
  case GL_TEXTURE_1D:
    return &g_default_texture_1d;
  case GL_TEXTURE_2D:
    return &g_default_texture_2d;
  case GL_TEXTURE_3D:
    return &g_default_texture_3d;
  case GL_TEXTURE_CUBE_MAP:
    return &g_default_texture_cube;
  case GL_TEXTURE_1D_ARRAY:
    return &g_default_texture_1d_array;
  case GL_TEXTURE_2D_ARRAY:
    return &g_default_texture_2d_array;
  case GL_TEXTURE_2D_MULTISAMPLE:
    return &g_default_texture_2d_multisample;
  case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
    return &g_default_texture_2d_multisample_array;
  default:
    return NULL;
  }
}

static void init_texture_object(GLTexture *tex, GLenum target, bool reserved) {
  if (!tex) {
    return;
  }

  memset(tex, 0, sizeof(*tex));
  tex->target = target;
  tex->internal_format = GL_RGBA;
  tex->samples = 0;
  tex->fixed_sample_locations = GL_TRUE;
  tex->base_level = 0;
  tex->max_level = 1000;
  tex->min_lod = -1000.0f;
  tex->max_lod = 1000.0f;
  tex->lod_bias = 0.0f;
  tex->border_color[0] = 0.0f;
  tex->border_color[1] = 0.0f;
  tex->border_color[2] = 0.0f;
  tex->border_color[3] = 0.0f;
  tex->compare_mode = GL_NONE;
  tex->compare_func = GL_LEQUAL;
  tex->swizzle[0] = GL_RED;
  tex->swizzle[1] = GL_GREEN;
  tex->swizzle[2] = GL_BLUE;
  tex->swizzle[3] = GL_ALPHA;
  tex->min_filter = GL_NEAREST_MIPMAP_LINEAR;
  tex->mag_filter = GL_LINEAR;
  tex->wrap_s = GL_REPEAT;
  tex->wrap_t = GL_REPEAT;
  tex->wrap_r = GL_REPEAT;
  tex->reserved = reserved;
  tex->in_use = !reserved;
}

static void init_sampler_defaults(GLSampler *sampler) {
  if (!sampler) {
    return;
  }

  memset(sampler, 0, sizeof(*sampler));
  sampler->in_use = true;
  sampler->min_filter = GL_NEAREST_MIPMAP_LINEAR;
  sampler->mag_filter = GL_LINEAR;
  sampler->wrap_s = GL_REPEAT;
  sampler->wrap_t = GL_REPEAT;
  sampler->wrap_r = GL_REPEAT;
  sampler->min_lod = -1000.0f;
  sampler->max_lod = 1000.0f;
  sampler->lod_bias = 0.0f;
  sampler->border_color[0] = 0.0f;
  sampler->border_color[1] = 0.0f;
  sampler->border_color[2] = 0.0f;
  sampler->border_color[3] = 0.0f;
  sampler->compare_mode = GL_NONE;
  sampler->compare_func = GL_LEQUAL;
}

static bool level_index_valid(GLint level) {
  return level >= 0 && level < GX2GL_MAX_TEXTURE_LEVELS;
}

static bool texture_level_defined(const GLTexture *tex, uint32_t level) {
  if (!tex || level >= GX2GL_MAX_TEXTURE_LEVELS) {
    return false;
  }
  if (tex->target == GL_TEXTURE_CUBE_MAP) {
    return tex->cube_defined_faces[level] == GX2GL_CUBE_FACE_MASK;
  }
  return (tex->defined_levels & (uint16_t)(1u << level)) != 0;
}

static bool texture_face_level_defined(const GLTexture *tex, uint32_t level,
                                       uint32_t face) {
  if (!tex || level >= GX2GL_MAX_TEXTURE_LEVELS) {
    return false;
  }
  if (tex->target != GL_TEXTURE_CUBE_MAP) {
    return texture_level_defined(tex, level);
  }
  return face < 6u &&
         (tex->cube_defined_faces[level] & (uint8_t)(1u << face)) != 0;
}

static void mark_texture_level_defined(GLTexture *tex, uint32_t level,
                                       uint32_t face) {
  if (!tex || level >= GX2GL_MAX_TEXTURE_LEVELS) {
    return;
  }
  if (tex->target == GL_TEXTURE_CUBE_MAP) {
    if (face < 6u) {
      tex->cube_defined_faces[level] |= (uint8_t)(1u << face);
    }
  } else {
    tex->defined_levels |= (uint16_t)(1u << level);
  }
}

static void mark_cube_level_complete(GLTexture *tex, uint32_t level) {
  if (tex && tex->target == GL_TEXTURE_CUBE_MAP &&
      level < GX2GL_MAX_TEXTURE_LEVELS) {
    tex->cube_defined_faces[level] = GX2GL_CUBE_FACE_MASK;
  }
}

static void clear_texture_level_state(GLTexture *tex) {
  if (!tex) {
    return;
  }
  tex->defined_levels = 0;
  memset(tex->cube_defined_faces, 0, sizeof(tex->cube_defined_faces));
}

static void update_texture_view(GLTexture *tex) {
  uint32_t first;
  uint32_t last;

  if (!tex || !tex->storage_allocated || tex->gx2_texture.surface.mipLevels == 0) {
    return;
  }

  first = tex->base_level < 0 ? 0u : (uint32_t)tex->base_level;
  if (first >= tex->gx2_texture.surface.mipLevels) {
    first = tex->gx2_texture.surface.mipLevels - 1u;
  }
  last = tex->gx2_texture.surface.mipLevels - 1u;
  if (tex->max_level >= 0 && (uint32_t)tex->max_level < last) {
    last = (uint32_t)tex->max_level;
  }
  if (last < first) {
    last = first;
  }

  tex->gx2_texture.viewFirstMip = first;
  tex->gx2_texture.viewNumMips = last - first + 1u;
  GX2InitTextureRegs(&tex->gx2_texture);
}

static void free_gx2_texture_storage(GX2Texture *texture) {
  if (!texture) {
    return;
  }
  log_texture_step("free_gx2_texture_storage: begin image=%p mipmaps=%p imageSize=%u mipmapSize=%u",
                   texture->surface.image, texture->surface.mipmaps,
                   (unsigned int)texture->surface.imageSize,
                   (unsigned int)texture->surface.mipmapSize);
  if (texture->surface.image) {
    log_texture_step("free_gx2_texture_storage: freeing image=%p",
                     texture->surface.image);
    gl_mem_free(GL_MEM_TYPE_MEM2, texture->surface.image);
    texture->surface.image = NULL;
  }
  if (texture->surface.mipmaps) {
    log_texture_step("free_gx2_texture_storage: freeing mipmaps=%p",
                     texture->surface.mipmaps);
    gl_mem_free(GL_MEM_TYPE_MEM2, texture->surface.mipmaps);
    texture->surface.mipmaps = NULL;
  }
  log_texture_step("free_gx2_texture_storage: done");
}

static void free_texture_storage(GLTexture *tex) {
  if (!tex || !tex->storage_allocated) {
    log_texture_step("free_texture_storage: skip tex=%p storage_allocated=%d",
                     tex, tex && tex->storage_allocated ? 1 : 0);
    return;
  }
  log_texture_step("free_texture_storage: begin tex=%p image=%p mipmaps=%p internal=0x%X size=%dx%dx%d",
                   tex, tex->gx2_texture.surface.image,
                   tex->gx2_texture.surface.mipmaps,
                   (unsigned int)tex->internal_format, (int)tex->width,
                   (int)tex->height, (int)tex->depth);
  free_gx2_texture_storage(&tex->gx2_texture);
  memset(&tex->gx2_texture, 0, sizeof(tex->gx2_texture));
  tex->storage_allocated = false;
  tex->complete = false;
  log_texture_step("free_texture_storage: done tex=%p", tex);
}

static bool map_dim(GLenum target, GX2SurfaceDim *dim) {
  switch (target) {
  case GL_TEXTURE_1D:
    *dim = GX2_SURFACE_DIM_TEXTURE_2D;
    return true;
  case GL_TEXTURE_2D:
    *dim = GX2_SURFACE_DIM_TEXTURE_2D;
    return true;
  case GL_TEXTURE_3D:
    *dim = GX2_SURFACE_DIM_TEXTURE_3D;
    return true;
  case GL_TEXTURE_CUBE_MAP:
    *dim = GX2_SURFACE_DIM_TEXTURE_CUBE;
    return true;
  case GL_TEXTURE_1D_ARRAY:
    *dim = GX2_SURFACE_DIM_TEXTURE_1D_ARRAY;
    return true;
  case GL_TEXTURE_2D_ARRAY:
    *dim = GX2_SURFACE_DIM_TEXTURE_2D_ARRAY;
    return true;
  case GL_TEXTURE_2D_MULTISAMPLE:
    *dim = GX2_SURFACE_DIM_TEXTURE_2D_MSAA;
    return true;
  case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
    *dim = GX2_SURFACE_DIM_TEXTURE_2D_MSAA_ARRAY;
    return true;
  default:
    *dim = GX2_SURFACE_DIM_TEXTURE_2D;
    return false;
  }
}

static bool is_valid_texture_target(GLenum target) {
  return target == GL_TEXTURE_1D || target == GL_TEXTURE_2D ||
         target == GL_TEXTURE_3D || target == GL_TEXTURE_CUBE_MAP ||
         target == GL_TEXTURE_1D_ARRAY || target == GL_TEXTURE_2D_ARRAY;
}

static bool is_multisample_texture_target(GLenum target) {
  return target == GL_TEXTURE_2D_MULTISAMPLE ||
         target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
}

static bool texture_depth_is_spatial(GLenum target) {
  return target == GL_TEXTURE_3D;
}

static bool map_sample_count(GLsizei requested, GX2AAMode *mode,
                             GLsizei *actual) {
  if (!mode || !actual || requested <= 0 || requested > GL33_MAX_SAMPLES) {
    return false;
  }
  if (requested == 1) {
    *mode = GX2_AA_MODE1X;
    *actual = 1;
  } else if (requested == 2) {
    *mode = GX2_AA_MODE2X;
    *actual = 2;
  } else if (requested <= 4) {
    *mode = GX2_AA_MODE4X;
    *actual = 4;
  } else {
    *mode = GX2_AA_MODE8X;
    *actual = 8;
  }
  return true;
}

static bool is_cube_map_face_target(GLenum target) {
  return target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
         target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
}

static uint32_t cube_map_face_index(GLenum target) {
  return (uint32_t)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
}

static GX2TexXYFilterMode map_xy_filter(GLenum filter) {
  switch (filter) {
  case GL_NEAREST:
  case GL_NEAREST_MIPMAP_NEAREST:
  case GL_NEAREST_MIPMAP_LINEAR:
    return GX2_TEX_XY_FILTER_MODE_POINT;
  case GL_LINEAR:
  case GL_LINEAR_MIPMAP_NEAREST:
  case GL_LINEAR_MIPMAP_LINEAR:
  default:
    return GX2_TEX_XY_FILTER_MODE_LINEAR;
  }
}

static GX2TexMipFilterMode map_mip_filter(GLenum filter) {
  switch (filter) {
  case GL_NEAREST_MIPMAP_NEAREST:
  case GL_LINEAR_MIPMAP_NEAREST:
    return GX2_TEX_MIP_FILTER_MODE_POINT;
  case GL_NEAREST_MIPMAP_LINEAR:
  case GL_LINEAR_MIPMAP_LINEAR:
    return GX2_TEX_MIP_FILTER_MODE_LINEAR;
  default:
    return GX2_TEX_MIP_FILTER_MODE_NONE;
  }
}

static GX2TexClampMode map_wrap(GLenum wrap) {
  switch (wrap) {
  case GL_CLAMP_TO_BORDER:
    return GX2_TEX_CLAMP_MODE_CLAMP_BORDER;
  case GL_CLAMP_TO_EDGE:
    return GX2_TEX_CLAMP_MODE_CLAMP;
  case GL_MIRRORED_REPEAT:
    return GX2_TEX_CLAMP_MODE_MIRROR;
  case GL_REPEAT:
  default:
    return GX2_TEX_CLAMP_MODE_WRAP;
  }
}

static bool is_valid_min_filter(GLint filter) {
  switch (filter) {
  case GL_NEAREST:
  case GL_LINEAR:
  case GL_NEAREST_MIPMAP_NEAREST:
  case GL_LINEAR_MIPMAP_NEAREST:
  case GL_NEAREST_MIPMAP_LINEAR:
  case GL_LINEAR_MIPMAP_LINEAR:
    return true;
  default:
    return false;
  }
}

static bool is_valid_mag_filter(GLint filter) {
  return filter == GL_NEAREST || filter == GL_LINEAR;
}

static bool is_valid_wrap_mode(GLint wrap) {
  return wrap == GL_REPEAT || wrap == GL_CLAMP_TO_EDGE ||
         wrap == GL_CLAMP_TO_BORDER || wrap == GL_MIRRORED_REPEAT;
}

static bool is_valid_compare_mode(GLint mode) {
  return mode == GL_NONE || mode == GL_COMPARE_REF_TO_TEXTURE;
}

static bool is_valid_compare_func(GLint func) {
  switch (func) {
  case GL_LEQUAL:
  case GL_GEQUAL:
  case GL_LESS:
  case GL_GREATER:
  case GL_EQUAL:
  case GL_NOTEQUAL:
  case GL_ALWAYS:
  case GL_NEVER:
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
  case GL_ALWAYS:
  default:
    return GX2_COMPARE_FUNC_ALWAYS;
  }
}

static bool is_valid_swizzle(GLint swizzle) {
  return swizzle == GL_RED || swizzle == GL_GREEN || swizzle == GL_BLUE ||
         swizzle == GL_ALPHA || swizzle == GL_ZERO || swizzle == GL_ONE;
}

static GX2TexBorderType map_border_type(const GLfloat color[4]) {
  if (color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f &&
      color[3] == 0.0f) {
    return GX2_TEX_BORDER_TYPE_TRANSPARENT_BLACK;
  }
  if (color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f &&
      color[3] == 1.0f) {
    return GX2_TEX_BORDER_TYPE_BLACK;
  }
  if (color[0] == 1.0f && color[1] == 1.0f && color[2] == 1.0f &&
      color[3] == 1.0f) {
    return GX2_TEX_BORDER_TYPE_WHITE;
  }
  return GX2_TEX_BORDER_TYPE_VARIABLE;
}

static bool get_texture_format_info(GLint internalformat, GLenum format,
                                    GLenum type, bool validate_upload,
                                    TextureFormatInfo *info) {
  memset(info, 0, sizeof(*info));

  switch (internalformat) {
  case GL_ALPHA:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_R);
    info->src_components = 1;
    info->dst_components = 1;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 1;
    info->dst_bytes_per_texel = 1;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_ALPHA || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case GL_LUMINANCE:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_1);
    info->src_components = 1;
    info->dst_components = 1;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 1;
    info->dst_bytes_per_texel = 1;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_LUMINANCE || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case GL_LUMINANCE_ALPHA:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_G);
    info->src_components = 2;
    info->dst_components = 2;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 2;
    info->dst_bytes_per_texel = 2;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_LUMINANCE_ALPHA || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case 1:
  case GL_RED:
  case GL_R8:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
    info->src_components = 1;
    info->dst_components = 1;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 1;
    info->dst_bytes_per_texel = 1;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RED || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case 2:
  case GL_RG:
  case GL_RG8:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
    info->src_components = 2;
    info->dst_components = 2;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 2;
    info->dst_bytes_per_texel = 2;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RG || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case 3:
  case GL_RGB:
  case GL_RGB8:
    if (type == GL_UNSIGNED_SHORT_5_6_5) {
      info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R5_G6_B5;
      info->surface_use =
          (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
      info->comp_map =
          GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
      info->src_components = 1;
      info->dst_components = 1;
      info->bytes_per_component = 2;
      info->src_bytes_per_texel = 2;
      info->dst_bytes_per_texel = 2;
      info->mipmap_supported = true;
      if (validate_upload &&
          (format != GL_RGB || type != GL_UNSIGNED_SHORT_5_6_5)) {
        return false;
      }
      return true;
    }

    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    info->src_components = 3;
    info->dst_components = 4;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 3;
    info->dst_bytes_per_texel = 4;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RGB || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case 4:
  case GL_RGBA:
  case GL_RGBA8:
    if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
      info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4;
      info->surface_use =
          (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
      info->comp_map =
          GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
      info->src_components = 1;
      info->dst_components = 1;
      info->bytes_per_component = 2;
      info->src_bytes_per_texel = 2;
      info->dst_bytes_per_texel = 2;
      info->mipmap_supported = true;
      if (validate_upload &&
          (format != GL_RGBA || type != GL_UNSIGNED_SHORT_4_4_4_4)) {
        return false;
      }
      return true;
    }

    if (type == GL_UNSIGNED_SHORT_5_5_5_1) {
      info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1;
      info->surface_use =
          (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
      info->comp_map =
          GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
      info->src_components = 1;
      info->dst_components = 1;
      info->bytes_per_component = 2;
      info->src_bytes_per_texel = 2;
      info->dst_bytes_per_texel = 2;
      info->mipmap_supported = true;
      if (validate_upload &&
          (format != GL_RGBA || type != GL_UNSIGNED_SHORT_5_5_5_1)) {
        return false;
      }
      return true;
    }

    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    info->src_components = 4;
    info->dst_components = 4;
    info->bytes_per_component = 1;
    info->src_bytes_per_texel = 4;
    info->dst_bytes_per_texel = 4;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RGBA || type != GL_UNSIGNED_BYTE)) {
      return false;
    }
    return true;
  case GL_RGBA4:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    info->src_components = 1;
    info->dst_components = 1;
    info->bytes_per_component = 2;
    info->src_bytes_per_texel = 2;
    info->dst_bytes_per_texel = 2;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RGBA || type != GL_UNSIGNED_SHORT_4_4_4_4)) {
      return false;
    }
    return true;
  case GL_RGB5_A1:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    info->src_components = 1;
    info->dst_components = 1;
    info->bytes_per_component = 2;
    info->src_bytes_per_texel = 2;
    info->dst_bytes_per_texel = 2;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RGBA || type != GL_UNSIGNED_SHORT_5_5_5_1)) {
      return false;
    }
    return true;
  case GL_RGB565:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R5_G6_B5;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    info->src_components = 1;
    info->dst_components = 1;
    info->bytes_per_component = 2;
    info->src_bytes_per_texel = 2;
    info->dst_bytes_per_texel = 2;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RGB || type != GL_UNSIGNED_SHORT_5_6_5)) {
      return false;
    }
    return true;
  case GL_RGBA16F:
    info->gx2_format = GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    info->src_components = 4;
    info->dst_components = 4;
    info->bytes_per_component = 2;
    info->src_bytes_per_texel = 8;
    info->dst_bytes_per_texel = 8;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RGBA || type != GL_HALF_FLOAT)) {
      return false;
    }
    return true;
  case GL_RGBA32F:
    info->gx2_format = GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    info->src_components = 4;
    info->dst_components = 4;
    info->bytes_per_component = 4;
    info->src_bytes_per_texel = 16;
    info->dst_bytes_per_texel = 16;
    info->mipmap_supported = true;
    if (validate_upload &&
        (format != GL_RGBA || type != GL_FLOAT)) {
      return false;
    }
    return true;
  case GL_DEPTH_COMPONENT:
  case GL_DEPTH_COMPONENT32F:
    info->gx2_format = GX2_SURFACE_FORMAT_FLOAT_R32;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_DEPTH_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
    info->src_components = 1;
    info->dst_components = 1;
    info->bytes_per_component = 4;
    info->src_bytes_per_texel = 4;
    info->dst_bytes_per_texel = 4;
    info->mipmap_supported = false;
    if (validate_upload &&
        (format != GL_DEPTH_COMPONENT || type != GL_FLOAT)) {
      return false;
    }
    return true;
  case GL_DEPTH_STENCIL:
  case GL_DEPTH24_STENCIL8:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_R24_X8;
    info->surface_use =
        (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_DEPTH_BUFFER);
    info->comp_map =
        GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
    info->src_components = 1;
    info->dst_components = 1;
    info->bytes_per_component = 4;
    info->src_bytes_per_texel = 4;
    info->dst_bytes_per_texel = 4;
    info->packed_u32 = true;
    info->mipmap_supported = false;
    if (validate_upload &&
        (format != GL_DEPTH_STENCIL || type != GL_UNSIGNED_INT_24_8)) {
      return false;
    }
    return true;
  case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
  case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_BC1;
    info->surface_use = GX2_SURFACE_USE_TEXTURE;
    info->comp_map = GX2_COMP_MAP(
        GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B,
        internalformat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ? GX2_SQ_SEL_1
                                                          : GX2_SQ_SEL_A);
    info->src_components =
        internalformat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ? 3 : 4;
    info->dst_components = info->src_components;
    info->block_bytes = 8;
    info->compressed = true;
    return !validate_upload;
  case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_BC2;
    info->surface_use = GX2_SURFACE_USE_TEXTURE;
    info->comp_map = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G,
                                  GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    info->src_components = 4;
    info->dst_components = 4;
    info->block_bytes = 16;
    info->compressed = true;
    return !validate_upload;
  case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
    info->gx2_format = GX2_SURFACE_FORMAT_UNORM_BC3;
    info->surface_use = GX2_SURFACE_USE_TEXTURE;
    info->comp_map = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G,
                                  GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    info->src_components = 4;
    info->dst_components = 4;
    info->block_bytes = 16;
    info->compressed = true;
    return !validate_upload;
  default:
    return false;
  }
}

static bool get_compressed_format_info(GLenum format,
                                       TextureFormatInfo *info) {
  return get_texture_format_info((GLint)format, GL_RGBA, GL_UNSIGNED_BYTE,
                                 false, info) &&
         info->compressed;
}

static bool compressed_image_size(GLsizei width, GLsizei height,
                                  uint32_t block_bytes,
                                  GLsizei *image_size) {
  uint64_t blocks_w;
  uint64_t blocks_h;
  uint64_t size;

  if (!image_size || width < 0 || height < 0 || block_bytes == 0) {
    return false;
  }
  if (width == 0 || height == 0) {
    *image_size = 0;
    return true;
  }

  blocks_w = ((uint64_t)(uint32_t)width + 3u) / 4u;
  blocks_h = ((uint64_t)(uint32_t)height + 3u) / 4u;
  size = blocks_w * blocks_h * block_bytes;
  if (size > (uint64_t)INT32_MAX) {
    return false;
  }
  *image_size = (GLsizei)size;
  return true;
}

static bool calc_level_layout(GLenum target, GX2SurfaceFormat format,
                              GLsizei base_width, GLsizei base_height,
                              GLsizei base_depth, uint32_t level,
                              TextureLevelLayout *layout) {
  GX2Surface surface;
  GX2SurfaceDim dim;

  memset(&surface, 0, sizeof(surface));
  if (!map_dim(target, &dim)) {
    return false;
  }

  surface.dim = dim;
  surface.width = (uint32_t)((base_width >> level) > 0 ? (base_width >> level) : 1);
  if (target == GL_TEXTURE_1D_ARRAY) {
    surface.height = 1;
  } else {
    surface.height =
        (uint32_t)((base_height >> level) > 0 ? (base_height >> level) : 1);
  }
  if (texture_depth_is_spatial(target)) {
    surface.depth =
        (uint32_t)((base_depth >> level) > 0 ? (base_depth >> level) : 1);
  } else {
    surface.depth = (uint32_t)(base_depth > 0 ? base_depth : 1);
  }
  surface.mipLevels = 1;
  surface.format = format;
  surface.use = GX2_SURFACE_USE_TEXTURE;
  surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

  GX2CalcSurfaceSizeAndAlignment(&surface);

  layout->dim = dim;
  layout->width = (GLsizei)surface.width;
  layout->height = (GLsizei)surface.height;
  layout->depth = (GLsizei)surface.depth;
  layout->pitch = surface.pitch;
  layout->image_size = surface.imageSize;
  layout->slice_size =
      surface.depth > 0 ? surface.imageSize / surface.depth : surface.imageSize;
  return true;
}

static uint8_t *get_texture_level_ptr_from_gx2(const GX2Texture *texture,
                                               uint32_t level) {
  if (!texture || level >= texture->surface.mipLevels) {
    return NULL;
  }
  if (level == 0) {
    return (uint8_t *)texture->surface.image;
  }
  if (!texture->surface.mipmaps) {
    return NULL;
  }
  return (uint8_t *)texture->surface.mipmaps +
         texture->surface.mipLevelOffset[level - 1];
}

static uint8_t *get_texture_level_ptr(const GLTexture *tex, uint32_t level) {
  return get_texture_level_ptr_from_gx2(&tex->gx2_texture, level);
}

static void init_sampler_state(GX2Sampler *sampler, GLenum min_filter,
                               GLenum mag_filter, GLenum wrap_s,
                               GLenum wrap_t, GLenum wrap_r, GLfloat min_lod,
                               GLfloat max_lod, GLfloat lod_bias,
                               const GLfloat border_color[4],
                               GLenum compare_mode, GLenum compare_func) {
  GX2InitSamplerXYFilter(sampler, map_xy_filter(mag_filter),
                         map_xy_filter(min_filter),
                         GX2_TEX_ANISO_RATIO_NONE);
  GX2InitSamplerZMFilter(sampler,
                         compare_mode == GL_COMPARE_REF_TO_TEXTURE
                             ? GX2_TEX_Z_FILTER_MODE_POINT
                             : GX2_TEX_Z_FILTER_MODE_NONE,
                         map_mip_filter(min_filter));
  GX2InitSamplerClamping(sampler, map_wrap(wrap_s), map_wrap(wrap_t),
                         map_wrap(wrap_r));
  GX2InitSamplerLOD(sampler, min_lod, max_lod, lod_bias);
  GX2InitSamplerBorderType(sampler, map_border_type(border_color));
  if (compare_mode == GL_COMPARE_REF_TO_TEXTURE) {
    GX2InitSamplerDepthCompare(sampler, map_compare_func(compare_func));
  }
}

static void init_texture_sampler(GLTexture *tex) {
  init_sampler_state(&tex->gx2_sampler, tex->min_filter, tex->mag_filter,
                     tex->wrap_s, tex->wrap_t, tex->wrap_r, tex->min_lod,
                     tex->max_lod, tex->lod_bias, tex->border_color,
                     tex->compare_mode, tex->compare_func);
}

static void init_sampler_object(GLSampler *sampler) {
  init_sampler_state(&sampler->gx2_sampler, sampler->min_filter,
                     sampler->mag_filter, sampler->wrap_s, sampler->wrap_t,
                     sampler->wrap_r, sampler->min_lod, sampler->max_lod,
                     sampler->lod_bias, sampler->border_color,
                     sampler->compare_mode, sampler->compare_func);
}

static bool rebuild_texture_storage(GLTexture *tex, GLsizei width,
                                    GLsizei height, GLsizei depth,
                                    GLint internalformat,
                                    uint32_t mip_levels,
                                    bool preserve_existing) {
  TextureFormatInfo info;
  GX2Texture new_texture;
  GX2Texture old_texture;
  bool same_layout;
  uint32_t preserve_levels = 0;
  GX2SurfaceDim dim;
  uint16_t old_defined_levels = tex->defined_levels;
  uint8_t old_cube_defined_faces[GX2GL_MAX_TEXTURE_LEVELS];
  memcpy(old_cube_defined_faces, tex->cube_defined_faces,
         sizeof(old_cube_defined_faces));

  if (!get_texture_format_info(internalformat, GL_RGBA, GL_UNSIGNED_BYTE, false,
                               &info) ||
      !map_dim(tex->target, &dim)) {
    log_texture_step("rebuild_texture_storage: format lookup failed internal=0x%X target=0x%X",
                     (unsigned int)internalformat, (unsigned int)tex->target);
    return false;
  }

  log_texture_step("rebuild_texture_storage: begin target=0x%X internal=0x%X size=%dx%dx%d mips=%u gx2fmt=0x%X preserve=%d",
                   (unsigned int)tex->target, (unsigned int)internalformat,
                   (int)width, (int)height, (int)depth, (unsigned int)mip_levels,
                   (unsigned int)info.gx2_format, preserve_existing ? 1 : 0);

  memset(&new_texture, 0, sizeof(new_texture));
  new_texture.surface.dim = dim;
  new_texture.surface.width = (uint32_t)width;
  new_texture.surface.height = (uint32_t)height;
  new_texture.surface.depth = (uint32_t)depth;
  new_texture.surface.mipLevels = mip_levels;
  new_texture.surface.format = info.gx2_format;
  new_texture.surface.use = info.surface_use;
  new_texture.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
  new_texture.viewFirstMip = 0;
  new_texture.viewNumMips = mip_levels;
  new_texture.viewFirstSlice = 0;
  new_texture.viewNumSlices = (dim == GX2_SURFACE_DIM_TEXTURE_3D ||
                               dim == GX2_SURFACE_DIM_TEXTURE_1D_ARRAY ||
                               dim == GX2_SURFACE_DIM_TEXTURE_2D_ARRAY)
                                  ? (uint32_t)depth
                                  : (dim == GX2_SURFACE_DIM_TEXTURE_CUBE)
                                        ? 6u
                                        : 1u;
  new_texture.compMap = info.comp_map;

  log_texture_step("rebuild_texture_storage: before GX2CalcSurfaceSizeAndAlignment");
  GX2CalcSurfaceSizeAndAlignment(&new_texture.surface);
  log_texture_step("rebuild_texture_storage: after calc imageSize=%u mipmapSize=%u alignment=%u pitch=%u",
                   (unsigned int)new_texture.surface.imageSize,
                   (unsigned int)new_texture.surface.mipmapSize,
                   (unsigned int)new_texture.surface.alignment,
                   (unsigned int)new_texture.surface.pitch);

  if (new_texture.surface.imageSize > 0) {
    log_texture_step("rebuild_texture_storage: allocating image bytes=%u align=%u",
                     (unsigned int)new_texture.surface.imageSize,
                     (unsigned int)new_texture.surface.alignment);
    new_texture.surface.image =
        gl_mem_alloc(GL_MEM_TYPE_MEM2, new_texture.surface.imageSize,
                     new_texture.surface.alignment);
    if (!new_texture.surface.image) {
      log_texture_step("rebuild_texture_storage: image allocation failed");
      return false;
    }
    log_texture_step("rebuild_texture_storage: image allocation ok ptr=%p",
                     new_texture.surface.image);
    memset(new_texture.surface.image, 0, new_texture.surface.imageSize);
    log_texture_step("rebuild_texture_storage: image cleared");
  }

  if (new_texture.surface.mipmapSize > 0) {
    log_texture_step("rebuild_texture_storage: allocating mipmaps bytes=%u align=%u",
                     (unsigned int)new_texture.surface.mipmapSize,
                     (unsigned int)new_texture.surface.alignment);
    new_texture.surface.mipmaps =
        gl_mem_alloc(GL_MEM_TYPE_MEM2, new_texture.surface.mipmapSize,
                     new_texture.surface.alignment);
    if (!new_texture.surface.mipmaps) {
      log_texture_step("rebuild_texture_storage: mip allocation failed");
      free_gx2_texture_storage(&new_texture);
      return false;
    }
    log_texture_step("rebuild_texture_storage: mip allocation ok ptr=%p",
                     new_texture.surface.mipmaps);
    memset(new_texture.surface.mipmaps, 0, new_texture.surface.mipmapSize);
    log_texture_step("rebuild_texture_storage: mipmaps cleared");
  }

  old_texture = tex->gx2_texture;
  same_layout = tex->storage_allocated && tex->internal_format == internalformat &&
                tex->width == width && tex->height == height &&
                tex->depth == depth;

  if (preserve_existing && same_layout) {
    preserve_levels = min_u32(old_texture.surface.mipLevels,
                              new_texture.surface.mipLevels);
    for (uint32_t level = 0; level < preserve_levels; ++level) {
      TextureLevelLayout layout;
      uint8_t *dst = get_texture_level_ptr_from_gx2(&new_texture, level);
      uint8_t *src = get_texture_level_ptr_from_gx2(&old_texture, level);
      if (!dst || !src ||
          !calc_level_layout(tex->target, info.gx2_format, width, height, depth,
                             level, &layout)) {
        continue;
      }
      memcpy(dst, src, layout.image_size);
    }
  }

  free_texture_storage(tex);
  log_texture_step("rebuild_texture_storage: previous storage released");

  tex->gx2_texture = new_texture;
  tex->storage_allocated = true;
  tex->complete = true;
  tex->internal_format = internalformat;
  tex->width = width;
  tex->height = height;
  tex->depth = depth;
  if (preserve_existing && same_layout) {
    tex->defined_levels = old_defined_levels;
    memcpy(tex->cube_defined_faces, old_cube_defined_faces,
           sizeof(tex->cube_defined_faces));
  } else {
    clear_texture_level_state(tex);
  }

  log_texture_step("rebuild_texture_storage: before GX2InitTextureRegs");
  GX2InitTextureRegs(&tex->gx2_texture);
  update_texture_view(tex);
  log_texture_step("rebuild_texture_storage: after GX2InitTextureRegs");
  if (texture_ptr_is_named(tex)) {
    gl_framebuffer_mark_texture_dirty(texture_name_from_ptr(tex));
  }
  log_texture_step("rebuild_texture_storage: success");
  return true;
}

static bool multisample_format_is_integer(GX2SurfaceFormat format) {
  uint32_t numeric_type = ((uint32_t)format >> 8) & 0x7u;
  return numeric_type == 1u || numeric_type == 3u;
}

static bool rebuild_multisample_texture_storage(
    GLTexture *tex, GLsizei width, GLsizei height, GLsizei depth,
    GLenum internalformat, GLsizei requested_samples,
    GLboolean fixed_sample_locations) {
  TextureFormatInfo info;
  GX2Texture new_texture;
  GX2SurfaceDim dim;
  GX2AAMode aa_mode;
  GLsizei actual_samples;

  if (!tex || !is_multisample_texture_target(tex->target) ||
      !get_texture_format_info((GLint)internalformat, GL_RGBA,
                               GL_UNSIGNED_BYTE, false, &info) ||
      !map_dim(tex->target, &dim) ||
      !map_sample_count(requested_samples, &aa_mode, &actual_samples)) {
    return false;
  }
  if ((info.surface_use &
       (GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_DEPTH_BUFFER)) == 0) {
    return false;
  }
  if (actual_samples > 1 && multisample_format_is_integer(info.gx2_format)) {
    return false;
  }

  if (width == 0 || height == 0 || depth == 0) {
    free_texture_storage(tex);
    tex->internal_format = (GLint)internalformat;
    tex->width = width;
    tex->height = height;
    tex->depth = depth;
    tex->samples = actual_samples;
    tex->fixed_sample_locations =
        fixed_sample_locations ? GL_TRUE : GL_FALSE;
    tex->complete = true;
    clear_texture_level_state(tex);
    mark_texture_level_defined(tex, 0, 0);
    if (texture_ptr_is_named(tex)) {
      gl_framebuffer_mark_texture_dirty(texture_name_from_ptr(tex));
    }
    return true;
  }

  memset(&new_texture, 0, sizeof(new_texture));
  new_texture.surface.dim = dim;
  new_texture.surface.width = (uint32_t)width;
  new_texture.surface.height = (uint32_t)height;
  new_texture.surface.depth = (uint32_t)depth;
  new_texture.surface.mipLevels = 1;
  new_texture.surface.format = info.gx2_format;
  new_texture.surface.aa = aa_mode;
  new_texture.surface.use = (GX2SurfaceUse)(
      info.surface_use | GX2_SURFACE_USE_TEXTURE);
  new_texture.surface.tileMode = GX2_TILE_MODE_DEFAULT;
  new_texture.viewFirstMip = 0;
  new_texture.viewNumMips = 1;
  new_texture.viewFirstSlice = 0;
  new_texture.viewNumSlices =
      tex->target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY ? (uint32_t)depth : 1u;
  new_texture.compMap = info.comp_map;

  GX2CalcSurfaceSizeAndAlignment(&new_texture.surface);
  if (new_texture.surface.imageSize > 0) {
    new_texture.surface.image =
        gl_mem_alloc(GL_MEM_TYPE_MEM2, new_texture.surface.imageSize,
                     new_texture.surface.alignment);
    if (!new_texture.surface.image) {
      return false;
    }
    memset(new_texture.surface.image, 0, new_texture.surface.imageSize);
  }

  free_texture_storage(tex);
  tex->gx2_texture = new_texture;
  tex->storage_allocated = new_texture.surface.image != NULL;
  tex->complete = true;
  tex->internal_format = (GLint)internalformat;
  tex->width = width;
  tex->height = height;
  tex->depth = depth;
  tex->samples = actual_samples;
  tex->fixed_sample_locations = fixed_sample_locations ? GL_TRUE : GL_FALSE;
  clear_texture_level_state(tex);
  mark_texture_level_defined(tex, 0, 0);

  GX2InitTextureRegs(&tex->gx2_texture);
  if (texture_ptr_is_named(tex)) {
    gl_framebuffer_mark_texture_dirty(texture_name_from_ptr(tex));
  }
  return true;
}

static uint16_t load_unaligned_u16(const uint8_t *ptr) {
  uint16_t value;
  memcpy(&value, ptr, sizeof(value));
  return value;
}

static void store_unaligned_u16(uint8_t *ptr, uint16_t value) {
  memcpy(ptr, &value, sizeof(value));
}

static uint32_t load_unaligned_u32(const uint8_t *ptr) {
  uint32_t value;
  memcpy(&value, ptr, sizeof(value));
  return value;
}

static void store_unaligned_u32(uint8_t *ptr, uint32_t value) {
  memcpy(ptr, &value, sizeof(value));
}

static void copy_texture_row(uint8_t *dst, const uint8_t *src,
                             uint32_t texel_count,
                             const TextureFormatInfo *info) {
  if (info->packed_u32) {
    for (uint32_t i = 0; i < texel_count; ++i) {
      uint32_t word = load_unaligned_u32(src + i * sizeof(uint32_t));
      store_unaligned_u32(dst + i * sizeof(uint32_t), CPU_TO_GPU_32(word));
    }
    return;
  }

  if (info->bytes_per_component == 1) {
    if (info->dst_bytes_per_texel == 4 && info->src_bytes_per_texel == 4 &&
        info->src_components == 4 && info->dst_components == 4) {
      for (uint32_t i = 0; i < texel_count; ++i) {
        uint32_t word = load_unaligned_u32(src + i * sizeof(uint32_t));
        store_unaligned_u32(dst + i * sizeof(uint32_t), CPU_TO_GPU_32(word));
      }
      return;
    }

    if (info->src_components == info->dst_components) {
      memcpy(dst, src, texel_count * info->dst_bytes_per_texel);
      return;
    }

    for (uint32_t i = 0; i < texel_count; ++i) {
      const uint8_t *src_texel = src + i * info->src_bytes_per_texel;
      uint8_t *dst_texel = dst + i * info->dst_bytes_per_texel;
      if (info->dst_bytes_per_texel == 4) {
        uint8_t rgba[4] = {0, 0, 0, 0xFF};
        uint32_t packed;
        if (info->src_bytes_per_texel > 0) rgba[0] = src_texel[0];
        if (info->src_bytes_per_texel > 1) rgba[1] = src_texel[1];
        if (info->src_bytes_per_texel > 2) rgba[2] = src_texel[2];
        packed = ((uint32_t)rgba[0] << 24) |
                 ((uint32_t)rgba[1] << 16) |
                 ((uint32_t)rgba[2] << 8) |
                 (uint32_t)rgba[3];
        packed = CPU_TO_GPU_32(packed);
        memcpy(dst_texel, &packed, sizeof(packed));
      } else {
        memcpy(dst_texel, src_texel, info->src_bytes_per_texel);
      }
    }
    return;
  }

  if (info->bytes_per_component == 2) {
    uint32_t count = texel_count * info->dst_components;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t byte_offset = i * sizeof(uint16_t);
      uint16_t word = load_unaligned_u16(src + byte_offset);
      store_unaligned_u16(dst + byte_offset, CPU_TO_GPU_16(word));
    }
    return;
  }

  if (info->bytes_per_component == 4) {
    uint32_t count = texel_count * info->dst_components;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t byte_offset = i * sizeof(uint32_t);
      uint32_t word = load_unaligned_u32(src + byte_offset);
      store_unaligned_u32(dst + byte_offset, CPU_TO_GPU_32(word));
    }
    return;
  }
}

static void read_texture_row(uint8_t *dst, const uint8_t *src,
                             uint32_t texel_count,
                             const TextureFormatInfo *info) {
  if (info->packed_u32) {
    for (uint32_t i = 0; i < texel_count; ++i) {
      uint32_t word = load_unaligned_u32(src + i * sizeof(uint32_t));
      store_unaligned_u32(dst + i * sizeof(uint32_t), GPU_TO_CPU_32(word));
    }
    return;
  }

  if (info->bytes_per_component == 1) {
    if (info->dst_bytes_per_texel == 4 && info->src_bytes_per_texel == 4 &&
        info->src_components == 4 && info->dst_components == 4) {
      for (uint32_t i = 0; i < texel_count; ++i) {
        uint32_t word = load_unaligned_u32(src + i * sizeof(uint32_t));
        store_unaligned_u32(dst + i * sizeof(uint32_t), GPU_TO_CPU_32(word));
      }
      return;
    }

    if (info->src_components == info->dst_components) {
      memcpy(dst, src, texel_count * info->src_bytes_per_texel);
      return;
    }

    for (uint32_t i = 0; i < texel_count; ++i) {
      const uint8_t *src_texel = src + i * info->dst_bytes_per_texel;
      uint8_t *dst_texel = dst + i * info->src_bytes_per_texel;
      memcpy(dst_texel, src_texel, info->src_bytes_per_texel);
    }
    return;
  }

  if (info->bytes_per_component == 2) {
    uint32_t count = texel_count * info->src_components;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t byte_offset = i * sizeof(uint16_t);
      uint16_t word = load_unaligned_u16(src + byte_offset);
      store_unaligned_u16(dst + byte_offset, GPU_TO_CPU_16(word));
    }
    return;
  }

  if (info->bytes_per_component == 4) {
    uint32_t count = texel_count * info->src_components;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t byte_offset = i * sizeof(uint32_t);
      uint32_t word = load_unaligned_u32(src + byte_offset);
      store_unaligned_u32(dst + byte_offset, GPU_TO_CPU_32(word));
    }
  }
}

static uint32_t align_transfer_row_bytes(uint32_t row_bytes, GLint alignment) {
  uint32_t align = (uint32_t)alignment;
  if (align <= 1u) {
    return row_bytes;
  }
  return (row_bytes + align - 1u) & ~(align - 1u);
}

static bool get_unpack_layout(const TextureFormatInfo *info, GLsizei width,
                              GLsizei height, uint32_t *row_bytes,
                              uint32_t *image_bytes, size_t *base_offset) {
  GLint row_length;
  GLint image_height;

  if (!g_gl_context || !info || !row_bytes || !image_bytes || !base_offset) {
    return false;
  }

  row_length = g_gl_context->unpack_row_length > 0 ? g_gl_context->unpack_row_length
                                                   : width;
  image_height = g_gl_context->unpack_image_height > 0
                     ? g_gl_context->unpack_image_height
                     : height;

  *row_bytes = align_transfer_row_bytes(
      (uint32_t)row_length * info->src_bytes_per_texel,
      g_gl_context->unpack_alignment);
  *image_bytes = *row_bytes * (uint32_t)image_height;
  *base_offset =
      (size_t)g_gl_context->unpack_skip_images * (*image_bytes) +
      (size_t)g_gl_context->unpack_skip_rows * (*row_bytes) +
      (size_t)g_gl_context->unpack_skip_pixels * info->src_bytes_per_texel;
  return true;
}

static bool get_pack_layout(const TextureFormatInfo *info, GLsizei width,
                            GLsizei height, uint32_t *row_bytes,
                            uint32_t *image_bytes, size_t *base_offset) {
  GLint row_length;
  GLint image_height;

  if (!g_gl_context || !info || !row_bytes || !image_bytes || !base_offset) {
    return false;
  }

  row_length = g_gl_context->pack_row_length > 0 ? g_gl_context->pack_row_length
                                                 : width;
  image_height = g_gl_context->pack_image_height > 0
                     ? g_gl_context->pack_image_height
                     : height;

  *row_bytes = align_transfer_row_bytes(
      (uint32_t)row_length * info->src_bytes_per_texel,
      g_gl_context->pack_alignment);
  *image_bytes = *row_bytes * (uint32_t)image_height;
  *base_offset =
      (size_t)g_gl_context->pack_skip_images * (*image_bytes) +
      (size_t)g_gl_context->pack_skip_rows * (*row_bytes) +
      (size_t)g_gl_context->pack_skip_pixels * info->src_bytes_per_texel;
  return true;
}

static bool calc_pixel_transfer_byte_count(uint32_t row_bytes,
                                           uint32_t image_bytes,
                                           size_t base_offset,
                                           uint32_t texel_bytes,
                                           GLsizei width,
                                           GLsizei height,
                                           GLsizei depth,
                                           GLsizeiptr *byte_count) {
  uint64_t total;

  if (!byte_count || width < 0 || height < 0 || depth < 0) {
    return false;
  }
  if (width == 0 || height == 0 || depth == 0) {
    *byte_count = 0;
    return true;
  }

  total = (uint64_t)base_offset +
          (uint64_t)(depth - 1) * image_bytes +
          (uint64_t)(height - 1) * row_bytes +
          (uint64_t)width * texel_bytes;
  if (total > (uint64_t)PTRDIFF_MAX) {
    return false;
  }
  *byte_count = (GLsizeiptr)total;
  return true;
}

static GLintptr pointer_as_buffer_offset(const GLvoid *pointer) {
  return (GLintptr)(uintptr_t)pointer;
}

static bool resolve_unpack_pixels(const TextureFormatInfo *info,
                                  GLsizei width, GLsizei height,
                                  GLsizei depth, const GLvoid *pixels,
                                  const GLvoid **resolved_pixels) {
  uint32_t row_bytes;
  uint32_t image_bytes;
  size_t base_offset;
  GLsizeiptr byte_count;

  if (!resolved_pixels) return false;
  *resolved_pixels = pixels;
  if (!g_gl_context || g_gl_context->bound_pixel_unpack_buffer == 0) {
    return true;
  }

  if (!get_unpack_layout(info, width, height, &row_bytes, &image_bytes,
                         &base_offset) ||
      !calc_pixel_transfer_byte_count(row_bytes, image_bytes, base_offset,
                                      info->src_bytes_per_texel, width, height,
                                      depth, &byte_count)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  return gl_buffer_get_read_range(g_gl_context->bound_pixel_unpack_buffer,
                                  pointer_as_buffer_offset(pixels), byte_count,
                                  resolved_pixels) == GL_TRUE;
}

static bool resolve_pack_pixels(const TextureFormatInfo *info,
                                GLsizei width, GLsizei height,
                                GLsizei depth, GLvoid *pixels,
                                GLvoid **resolved_pixels,
                                GLintptr *buffer_offset,
                                GLsizeiptr *byte_count) {
  uint32_t row_bytes;
  uint32_t image_bytes;
  size_t base_offset;

  if (!resolved_pixels || !buffer_offset || !byte_count) return false;
  *resolved_pixels = pixels;
  *buffer_offset = 0;
  *byte_count = 0;
  if (!g_gl_context || g_gl_context->bound_pixel_pack_buffer == 0) {
    return true;
  }

  if (!get_pack_layout(info, width, height, &row_bytes, &image_bytes,
                       &base_offset) ||
      !calc_pixel_transfer_byte_count(row_bytes, image_bytes, base_offset,
                                      info->src_bytes_per_texel, width, height,
                                      depth, byte_count)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }

  *buffer_offset = pointer_as_buffer_offset(pixels);
  return gl_buffer_get_write_range(g_gl_context->bound_pixel_pack_buffer,
                                   *buffer_offset, *byte_count,
                                   resolved_pixels) == GL_TRUE;
}

static bool resolve_compressed_unpack_data(const GLvoid *data,
                                           GLsizei image_size,
                                           const GLvoid **resolved_data) {
  if (!resolved_data || image_size < 0) {
    return false;
  }
  *resolved_data = data;
  if (!g_gl_context || g_gl_context->bound_pixel_unpack_buffer == 0) {
    return true;
  }
  return gl_buffer_get_read_range(g_gl_context->bound_pixel_unpack_buffer,
                                  pointer_as_buffer_offset(data), image_size,
                                  resolved_data) == GL_TRUE;
}

static bool resolve_compressed_pack_data(GLvoid *data, GLsizei image_size,
                                         GLvoid **resolved_data,
                                         GLintptr *buffer_offset) {
  if (!resolved_data || !buffer_offset || image_size < 0) {
    return false;
  }
  *resolved_data = data;
  *buffer_offset = 0;
  if (!g_gl_context || g_gl_context->bound_pixel_pack_buffer == 0) {
    return true;
  }
  *buffer_offset = pointer_as_buffer_offset(data);
  return gl_buffer_get_write_range(g_gl_context->bound_pixel_pack_buffer,
                                   *buffer_offset, image_size,
                                   resolved_data) == GL_TRUE;
}

static bool upload_texture_level(GLTexture *tex, GLint level, GLsizei width,
                                 GLsizei height, GLsizei depth,
                                 const TextureFormatInfo *info,
                                 const GLvoid *pixels) {
  TextureLevelLayout layout;
  uint8_t *dst;
  const uint8_t *src;
  uint32_t src_row_bytes;
  uint32_t src_image_bytes;
  uint32_t dst_row_bytes;
  size_t src_base_offset;

  if (width == 0 || height == 0 || depth == 0) {
    return true;
  }
  if (!pixels) {
    return true;
  }
  if (!calc_level_layout(tex->target, tex->gx2_texture.surface.format,
                         tex->width, tex->height, tex->depth, (uint32_t)level,
                         &layout)) {
    return false;
  }

  dst = get_texture_level_ptr(tex, (uint32_t)level);
  if (!dst) {
    return false;
  }

  if (!get_unpack_layout(info, width, height, &src_row_bytes, &src_image_bytes,
                         &src_base_offset)) {
    return false;
  }
  src = (const uint8_t *)pixels + src_base_offset;
  dst_row_bytes = layout.pitch * info->dst_bytes_per_texel;

  for (GLsizei z = 0; z < depth; ++z) {
    uint8_t *dst_slice = dst + (uint32_t)z * layout.slice_size;
    const uint8_t *src_slice = src + (size_t)z * src_image_bytes;
    for (GLsizei y = 0; y < height; ++y) {
      uint32_t dst_y = (uint32_t)(height - 1 - y);
      copy_texture_row(dst_slice + dst_y * dst_row_bytes,
                       src_slice + (size_t)y * src_row_bytes, (uint32_t)width,
                       info);
    }
  }

  DCFlushRange(dst, layout.image_size);
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, dst, layout.image_size);
  return true;
}

static bool upload_texture_sub_region(GLTexture *tex, GLint level,
                                      GLint xoffset, GLint yoffset,
                                      GLint zoffset, GLsizei width,
                                      GLsizei height, GLsizei depth,
                                      const TextureFormatInfo *info,
                                      const GLvoid *pixels) {
  TextureLevelLayout layout;
  uint8_t *dst;
  const uint8_t *src;
  uint32_t src_row_bytes;
  uint32_t src_image_bytes;
  uint32_t dst_row_bytes;
  size_t src_base_offset;

  if (width == 0 || height == 0 || depth == 0) {
    return true;
  }
  if (!pixels) {
    return false;
  }
  if (!calc_level_layout(tex->target, tex->gx2_texture.surface.format,
                         tex->width, tex->height, tex->depth, (uint32_t)level,
                         &layout)) {
    return false;
  }
  if (xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 || height < 0 ||
      depth < 0 || xoffset + width > layout.width ||
      yoffset + height > layout.height || zoffset + depth > layout.depth) {
    return false;
  }

  dst = get_texture_level_ptr(tex, (uint32_t)level);
  if (!dst) {
    return false;
  }

  if (!get_unpack_layout(info, width, height, &src_row_bytes, &src_image_bytes,
                         &src_base_offset)) {
    return false;
  }
  src = (const uint8_t *)pixels + src_base_offset;
  dst_row_bytes = layout.pitch * info->dst_bytes_per_texel;

  for (GLsizei z = 0; z < depth; ++z) {
    uint8_t *dst_slice = dst + (uint32_t)(zoffset + z) * layout.slice_size;
    const uint8_t *src_slice = src + (size_t)z * src_image_bytes;
    for (GLsizei y = 0; y < height; ++y) {
      uint32_t dst_y = (uint32_t)(layout.height - 1 - (yoffset + y));
      copy_texture_row(
          dst_slice + dst_y * dst_row_bytes +
              (uint32_t)xoffset * info->dst_bytes_per_texel,
          src_slice + (size_t)y * src_row_bytes, (uint32_t)width, info);
    }
  }

  DCFlushRange(dst, layout.image_size);
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, dst, layout.image_size);
  return true;
}

static bool upload_compressed_texture_region(
    GLTexture *tex, GLint level, GLint xoffset, GLint yoffset, GLint slice,
    GLsizei width, GLsizei height, const TextureFormatInfo *info,
    const GLvoid *data) {
  TextureLevelLayout layout;
  uint8_t *dst_level;
  uint32_t src_blocks_w;
  uint32_t src_blocks_h;
  uint32_t src_row_bytes;
  uint32_t dst_row_bytes;

  if (!tex || !info || !info->compressed || width < 0 || height < 0 ||
      xoffset < 0 || yoffset < 0 || slice < 0) {
    return false;
  }
  if (width == 0 || height == 0) {
    return true;
  }
  if (!data || !calc_level_layout(tex->target,
                                  tex->gx2_texture.surface.format,
                                  tex->width, tex->height, tex->depth,
                                  (uint32_t)level, &layout) ||
      (uint64_t)(uint32_t)xoffset + (uint32_t)width >
          (uint32_t)layout.width ||
      (uint64_t)(uint32_t)yoffset + (uint32_t)height >
          (uint32_t)layout.height ||
      (uint32_t)slice >= (uint32_t)layout.depth) {
    return false;
  }

  dst_level = get_texture_level_ptr(tex, (uint32_t)level);
  if (!dst_level) {
    return false;
  }

  src_blocks_w = ((uint32_t)width + 3u) / 4u;
  src_blocks_h = ((uint32_t)height + 3u) / 4u;
  src_row_bytes = src_blocks_w * info->block_bytes;
  dst_row_bytes = layout.pitch * info->block_bytes;

  /* S3TC is a byte-defined little-endian block stream. Keep the block bytes
   * intact on the big-endian CPU; GX2 consumes the same stream directly. */
  for (uint32_t row = 0; row < src_blocks_h; ++row) {
    uint8_t *dst = dst_level + (uint32_t)slice * layout.slice_size +
                   ((uint32_t)yoffset / 4u + row) * dst_row_bytes +
                   ((uint32_t)xoffset / 4u) * info->block_bytes;
    const uint8_t *src = (const uint8_t *)data + row * src_row_bytes;
    memcpy(dst, src, src_row_bytes);
  }

  DCFlushRange(dst_level, layout.image_size);
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, dst_level,
                layout.image_size);
  return true;
}

static bool read_compressed_texture_image(const GLTexture *tex, GLint level,
                                          GLint slice,
                                          const TextureFormatInfo *info,
                                          GLvoid *data) {
  TextureLevelLayout layout;
  const uint8_t *src_level;
  uint32_t blocks_w;
  uint32_t blocks_h;
  uint32_t src_row_bytes;
  uint32_t dst_row_bytes;

  if (!tex || !info || !info->compressed || !data || slice < 0 ||
      !calc_level_layout(tex->target, tex->gx2_texture.surface.format,
                         tex->width, tex->height, tex->depth,
                         (uint32_t)level, &layout) ||
      (uint32_t)slice >= (uint32_t)layout.depth) {
    return false;
  }

  src_level = get_texture_level_ptr(tex, (uint32_t)level);
  if (!src_level) {
    return false;
  }

  blocks_w = ((uint32_t)layout.width + 3u) / 4u;
  blocks_h = ((uint32_t)layout.height + 3u) / 4u;
  src_row_bytes = layout.pitch * info->block_bytes;
  dst_row_bytes = blocks_w * info->block_bytes;
  src_level += (uint32_t)slice * layout.slice_size;
  for (uint32_t row = 0; row < blocks_h; ++row) {
    memcpy((uint8_t *)data + row * dst_row_bytes,
           src_level + row * src_row_bytes, dst_row_bytes);
  }
  return true;
}

static float decode_gpu_float(const uint8_t *ptr) {
  uint32_t word;
  float value;

  memcpy(&word, ptr, sizeof(word));
  word = GPU_TO_CPU_32(word);
  memcpy(&value, &word, sizeof(value));
  return value;
}

static void encode_gpu_float(uint8_t *ptr, float value) {
  uint32_t word;

  memcpy(&word, &value, sizeof(word));
  word = CPU_TO_GPU_32(word);
  memcpy(ptr, &word, sizeof(word));
}

static bool generate_mipmap_level(GLTexture *tex, uint32_t dst_level,
                                  const TextureFormatInfo *info) {
  TextureLevelLayout src_layout;
  TextureLevelLayout dst_layout;
  uint8_t *src_base;
  uint8_t *dst_base;
  uint32_t src_row_bytes;
  uint32_t dst_row_bytes;

  if (dst_level == 0) {
    return false;
  }
  if (!calc_level_layout(tex->target, tex->gx2_texture.surface.format,
                         tex->width, tex->height, tex->depth, dst_level - 1,
                         &src_layout) ||
      !calc_level_layout(tex->target, tex->gx2_texture.surface.format,
                         tex->width, tex->height, tex->depth, dst_level,
                         &dst_layout)) {
    return false;
  }

  src_base = get_texture_level_ptr(tex, dst_level - 1);
  dst_base = get_texture_level_ptr(tex, dst_level);
  if (!src_base || !dst_base) {
    return false;
  }

  memset(dst_base, 0, dst_layout.image_size);

  src_row_bytes = src_layout.pitch * info->dst_bytes_per_texel;
  dst_row_bytes = dst_layout.pitch * info->dst_bytes_per_texel;

  for (GLsizei z = 0; z < dst_layout.depth; ++z) {
    uint8_t *dst_slice = dst_base + (uint32_t)z * dst_layout.slice_size;
    uint32_t src_z0 = texture_depth_is_spatial(tex->target)
                          ? (uint32_t)z * 2u
                          : (uint32_t)z;
    uint32_t src_z_count = texture_depth_is_spatial(tex->target) &&
                                   src_layout.depth > 1
                               ? 2u
                               : 1u;

    if (src_z0 + src_z_count > (uint32_t)src_layout.depth) {
      src_z_count = (uint32_t)src_layout.depth - src_z0;
    }

    for (GLsizei y = 0; y < dst_layout.height; ++y) {
      uint8_t *dst_row = dst_slice + (uint32_t)y * dst_row_bytes;
      uint32_t src_y0 = (uint32_t)y * 2u;
      uint32_t src_y_count = src_y0 + 1u < (uint32_t)src_layout.height ? 2u : 1u;

      for (GLsizei x = 0; x < dst_layout.width; ++x) {
        uint32_t src_x0 = (uint32_t)x * 2u;
        uint32_t src_x_count =
            src_x0 + 1u < (uint32_t)src_layout.width ? 2u : 1u;
        uint8_t *dst_texel = dst_row + (uint32_t)x * info->dst_bytes_per_texel;
        uint32_t sample_count = src_x_count * src_y_count * src_z_count;

        if (info->bytes_per_component == 1) {
          uint32_t sums[4] = {0, 0, 0, 0};
          for (uint32_t dz = 0; dz < src_z_count; ++dz) {
            const uint8_t *src_slice =
                src_base + (src_z0 + dz) * src_layout.slice_size;
            for (uint32_t dy = 0; dy < src_y_count; ++dy) {
              const uint8_t *src_row =
                  src_slice + (src_y0 + dy) * src_row_bytes;
              for (uint32_t dx = 0; dx < src_x_count; ++dx) {
                const uint8_t *src_texel =
                    src_row + (src_x0 + dx) * info->dst_bytes_per_texel;
                for (uint32_t c = 0; c < info->dst_components; ++c) {
                  sums[c] += src_texel[c];
                }
              }
            }
          }

          for (uint32_t c = 0; c < info->dst_components; ++c) {
            dst_texel[c] =
                (uint8_t)((sums[c] + sample_count / 2u) / sample_count);
          }
        } else if (info->bytes_per_component == 4 && !info->packed_u32) {
          float sums[4] = {0.0f, 0.0f, 0.0f, 0.0f};
          for (uint32_t dz = 0; dz < src_z_count; ++dz) {
            const uint8_t *src_slice =
                src_base + (src_z0 + dz) * src_layout.slice_size;
            for (uint32_t dy = 0; dy < src_y_count; ++dy) {
              const uint8_t *src_row =
                  src_slice + (src_y0 + dy) * src_row_bytes;
              for (uint32_t dx = 0; dx < src_x_count; ++dx) {
                const uint8_t *src_texel =
                    src_row + (src_x0 + dx) * info->dst_bytes_per_texel;
                for (uint32_t c = 0; c < info->dst_components; ++c) {
                  sums[c] += decode_gpu_float(src_texel + c * 4u);
                }
              }
            }
          }

          for (uint32_t c = 0; c < info->dst_components; ++c) {
            encode_gpu_float(dst_texel + c * 4u, sums[c] / sample_count);
          }
        } else {
          const uint8_t *src_slice = src_base + src_z0 * src_layout.slice_size;
          const uint8_t *src_row = src_slice + src_y0 * src_row_bytes;
          const uint8_t *src_texel =
              src_row + src_x0 * info->dst_bytes_per_texel;
          memcpy(dst_texel, src_texel, info->dst_bytes_per_texel);
        }
      }
    }
  }

  DCFlushRange(dst_base, dst_layout.image_size);
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, dst_base, dst_layout.image_size);
  return true;
}

static uint32_t calc_full_mip_count(GLsizei width, GLsizei height,
                                    GLsizei depth) {
  uint32_t levels = 1;
  GLsizei max_dim = width;
  if (height > max_dim) {
    max_dim = height;
  }
  if (depth > max_dim) {
    max_dim = depth;
  }

  while (max_dim > 1) {
    max_dim >>= 1;
    ++levels;
  }
  return levels;
}

static GLuint get_bound_tex(GLenum target) {
  if (!g_gl_context) {
    return 0;
  }
  GLuint unit = g_gl_context->active_texture;
  switch (target) {
  case GL_TEXTURE_1D:
    return g_gl_context->bound_texture_1d[unit];
  case GL_TEXTURE_2D:
    return g_gl_context->bound_texture_2d[unit];
  case GL_TEXTURE_3D:
    return g_gl_context->bound_texture_3d[unit];
  case GL_TEXTURE_CUBE_MAP:
    return g_gl_context->bound_texture_cube[unit];
  case GL_TEXTURE_1D_ARRAY:
    return g_gl_context->bound_texture_1d_array[unit];
  case GL_TEXTURE_2D_ARRAY:
    return g_gl_context->bound_texture_2d_array[unit];
  case GL_TEXTURE_2D_MULTISAMPLE:
    return g_gl_context->bound_texture_2d_multisample[unit];
  case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
    return g_gl_context->bound_texture_2d_multisample_array[unit];
  default:
    return 0;
  }
}

static GLTexture *get_bound_texture(GLenum target) {
  GLuint id = get_bound_tex(target);

  if (id == 0) {
    return default_texture_for_target(target);
  }
  if (id >= MAX_TEXTURES || !g_textures[id].in_use) {
    return NULL;
  }
  return &g_textures[id];
}

static void set_bound_tex(GLenum target, GLuint texture) {
  GLuint unit;

  if (!g_gl_context) {
    return;
  }

  unit = g_gl_context->active_texture;
  switch (target) {
  case GL_TEXTURE_1D:
    g_gl_context->bound_texture_1d[unit] = texture;
    break;
  case GL_TEXTURE_2D:
    g_gl_context->bound_texture_2d[unit] = texture;
    break;
  case GL_TEXTURE_3D:
    g_gl_context->bound_texture_3d[unit] = texture;
    break;
  case GL_TEXTURE_CUBE_MAP:
    g_gl_context->bound_texture_cube[unit] = texture;
    break;
  case GL_TEXTURE_1D_ARRAY:
    g_gl_context->bound_texture_1d_array[unit] = texture;
    break;
  case GL_TEXTURE_2D_ARRAY:
    g_gl_context->bound_texture_2d_array[unit] = texture;
    break;
  case GL_TEXTURE_2D_MULTISAMPLE:
    g_gl_context->bound_texture_2d_multisample[unit] = texture;
    break;
  case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
    g_gl_context->bound_texture_2d_multisample_array[unit] = texture;
    break;
  default:
    break;
  }
}

static bool valid_texture_name_for_bind(GLuint texture) {
  if (texture == 0) {
    return true;
  }
  if (texture >= MAX_TEXTURES) {
    return false;
  }
  return g_textures[texture].reserved || g_textures[texture].in_use;
}

static bool ensure_texture_object(GLuint texture, GLenum target) {
  if (texture == 0 || texture >= MAX_TEXTURES) {
    return false;
  }
  if (g_textures[texture].in_use) {
    return g_textures[texture].target == target;
  }
  if (!g_textures[texture].reserved) {
    return false;
  }

  init_texture_object(&g_textures[texture], target, false);
  init_texture_sampler(&g_textures[texture]);
  return true;
}

static bool is_copy_color_format_supported(GLint internalformat,
                                           const TextureFormatInfo *info) {
  if (!info || info->bytes_per_component != 1 || info->packed_u32) {
    return false;
  }

  switch (internalformat) {
  case GL_ALPHA:
  case GL_LUMINANCE:
  case GL_LUMINANCE_ALPHA:
  case 1:
  case GL_RED:
  case GL_R8:
  case 2:
  case GL_RG:
  case GL_RG8:
  case 3:
  case GL_RGB:
  case GL_RGB8:
  case 4:
  case GL_RGBA:
  case GL_RGBA8:
    return true;
  default:
    return false;
  }
}

static GLenum texture_component_type(GLint internalformat) {
  switch (internalformat) {
  case GL_RGBA16F:
  case GL_RGBA32F:
  case GL_DEPTH_COMPONENT:
  case GL_DEPTH_COMPONENT32F:
    return GL_FLOAT;
  case GL_DEPTH_STENCIL:
  case GL_DEPTH24_STENCIL8:
    return GL_UNSIGNED_INT;
  default:
    return GL_UNSIGNED_NORMALIZED;
  }
}

static void texture_component_sizes(GLint internalformat, GLint *red,
                                    GLint *green, GLint *blue, GLint *alpha,
                                    GLint *depth, GLint *stencil) {
  *red = *green = *blue = *alpha = *depth = *stencil = 0;

  switch (internalformat) {
  case GL_ALPHA:
    *alpha = 8;
    break;
  case GL_LUMINANCE:
    *red = *green = *blue = 8;
    break;
  case GL_LUMINANCE_ALPHA:
    *red = *green = *blue = *alpha = 8;
    break;
  case 1:
  case GL_RED:
  case GL_R8:
    *red = 8;
    break;
  case 2:
  case GL_RG:
  case GL_RG8:
    *red = *green = 8;
    break;
  case 3:
  case GL_RGB:
  case GL_RGB8:
    *red = *green = *blue = 8;
    break;
  case 4:
  case GL_RGBA:
  case GL_RGBA8:
    *red = *green = *blue = *alpha = 8;
    break;
  case GL_RGBA4:
    *red = *green = *blue = *alpha = 4;
    break;
  case GL_RGB5_A1:
    *red = *green = *blue = 5;
    *alpha = 1;
    break;
  case GL_RGB565:
    *red = 5;
    *green = 6;
    *blue = 5;
    break;
  case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
    *red = 5;
    *green = 6;
    *blue = 5;
    break;
  case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
    *red = 5;
    *green = 6;
    *blue = 5;
    *alpha = 1;
    break;
  case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
  case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
    *red = 5;
    *green = 6;
    *blue = 5;
    *alpha = 8;
    break;
  case GL_RGBA16F:
    *red = *green = *blue = *alpha = 16;
    break;
  case GL_RGBA32F:
    *red = *green = *blue = *alpha = 32;
    break;
  case GL_DEPTH_COMPONENT:
  case GL_DEPTH_COMPONENT32F:
    *depth = 32;
    break;
  case GL_DEPTH_STENCIL:
  case GL_DEPTH24_STENCIL8:
    *depth = 24;
    *stencil = 8;
    break;
  default:
    break;
  }
}

static bool convert_copy_texels(const uint8_t *src_rgba, GLint internalformat,
                                const TextureFormatInfo *info,
                                GLsizei width, GLsizei height,
                                uint8_t *dst_pixels) {
  uint32_t texel_count;

  if (!src_rgba || !info || !dst_pixels || width < 0 || height < 0) {
    return false;
  }

  texel_count = (uint32_t)width * (uint32_t)height;
  for (uint32_t i = 0; i < texel_count; ++i) {
    const uint8_t *src_texel = src_rgba + i * 4u;
    uint8_t *dst_texel = dst_pixels + i * info->src_bytes_per_texel;

    switch (internalformat) {
    case GL_ALPHA:
      dst_texel[0] = src_texel[3];
      break;
    case GL_LUMINANCE:
    case 1:
    case GL_RED:
    case GL_R8:
      dst_texel[0] = src_texel[0];
      break;
    case GL_LUMINANCE_ALPHA:
      dst_texel[0] = src_texel[0];
      dst_texel[1] = src_texel[3];
      break;
    case 2:
    case GL_RG:
    case GL_RG8:
      dst_texel[0] = src_texel[0];
      dst_texel[1] = src_texel[1];
      break;
    case 3:
    case GL_RGB:
    case GL_RGB8:
      dst_texel[0] = src_texel[0];
      dst_texel[1] = src_texel[1];
      dst_texel[2] = src_texel[2];
      break;
    case 4:
    case GL_RGBA:
    case GL_RGBA8:
      memcpy(dst_texel, src_texel, 4u);
      break;
    default:
      return false;
    }
  }

  return true;
}

static uint8_t *build_copy_upload_pixels(GLint x, GLint y, GLsizei width,
                                         GLsizei height, GLint internalformat,
                                         const TextureFormatInfo *info) {
  size_t texel_count;
  size_t rgba_size;
  size_t upload_size;
  uint8_t *rgba_pixels;
  uint8_t *upload_pixels;

  if (width == 0 || height == 0) {
    return NULL;
  }

  texel_count = (size_t)width * (size_t)height;
  rgba_size = texel_count * 4u;
  upload_size = texel_count * info->src_bytes_per_texel;
  rgba_pixels = (uint8_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2, rgba_size, 64);
  if (!rgba_pixels) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return NULL;
  }
  upload_pixels = (uint8_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2, upload_size, 64);
  if (!upload_pixels) {
    gl_mem_free(GL_MEM_TYPE_MEM2, rgba_pixels);
    _gl_set_error(GL_OUT_OF_MEMORY);
    return NULL;
  }

  if (!gl_read_color_pixels_rgba8(x, y, width, height, rgba_pixels) ||
      !convert_copy_texels(rgba_pixels, internalformat, info, width, height,
                           upload_pixels)) {
    gl_mem_free(GL_MEM_TYPE_MEM2, rgba_pixels);
    gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
    return NULL;
  }

  gl_mem_free(GL_MEM_TYPE_MEM2, rgba_pixels);
  return upload_pixels;
}

void gl_texture_init(void) {
  memset(g_textures, 0, sizeof(g_textures));
  memset(g_samplers, 0, sizeof(g_samplers));
  memset(&g_proxy_multisample_2d, 0, sizeof(g_proxy_multisample_2d));
  memset(&g_proxy_multisample_array, 0, sizeof(g_proxy_multisample_array));
  init_texture_object(&g_default_texture_1d, GL_TEXTURE_1D, false);
  init_texture_object(&g_default_texture_2d, GL_TEXTURE_2D, false);
  init_texture_object(&g_default_texture_3d, GL_TEXTURE_3D, false);
  init_texture_object(&g_default_texture_cube, GL_TEXTURE_CUBE_MAP, false);
  init_texture_object(&g_default_texture_1d_array, GL_TEXTURE_1D_ARRAY, false);
  init_texture_object(&g_default_texture_2d_array, GL_TEXTURE_2D_ARRAY, false);
  init_texture_object(&g_default_texture_2d_multisample,
                      GL_TEXTURE_2D_MULTISAMPLE, false);
  init_texture_object(&g_default_texture_2d_multisample_array,
                      GL_TEXTURE_2D_MULTISAMPLE_ARRAY, false);
  init_texture_sampler(&g_default_texture_1d);
  init_texture_sampler(&g_default_texture_2d);
  init_texture_sampler(&g_default_texture_3d);
  init_texture_sampler(&g_default_texture_cube);
  init_texture_sampler(&g_default_texture_1d_array);
  init_texture_sampler(&g_default_texture_2d_array);
  init_texture_sampler(&g_default_texture_2d_multisample);
  init_texture_sampler(&g_default_texture_2d_multisample_array);
}

void _gl_GenTextures(GLsizei n, GLuint *textures) {
  if (!g_gl_context) {
    return;
  }
  if (n < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (n > 0 && !textures) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  int generated = 0;
  for (int i = 1; i < MAX_TEXTURES && generated < n; i++) {
    if (!g_textures[i].reserved && !g_textures[i].in_use) {
      init_texture_object(&g_textures[i], GL_TEXTURE_2D, true);
      textures[generated++] = i;
    }
  }
  if (generated < n) {
    for (int i = generated; i < n; ++i) {
      textures[i] = 0;
    }
    _gl_set_error(GL_OUT_OF_MEMORY);
  }
}

void _gl_DeleteTextures(GLsizei n, const GLuint *textures) {
  if (!g_gl_context) {
    return;
  }
  if (n < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (n > 0 && !textures) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  for (int i = 0; i < n; i++) {
    GLuint id = textures[i];
    if (id > 0 && id < MAX_TEXTURES &&
        (g_textures[id].reserved || g_textures[id].in_use)) {
      gl_framebuffer_mark_texture_dirty(id);
      for (int u = 0; u < GL33_MAX_TEXTURE_UNITS; u++) {
        if (g_gl_context->bound_texture_1d[u] == id) {
          g_gl_context->bound_texture_1d[u] = 0;
        }
        if (g_gl_context->bound_texture_2d[u] == id) {
          g_gl_context->bound_texture_2d[u] = 0;
        }
        if (g_gl_context->bound_texture_3d[u] == id) {
          g_gl_context->bound_texture_3d[u] = 0;
        }
        if (g_gl_context->bound_texture_cube[u] == id) {
          g_gl_context->bound_texture_cube[u] = 0;
        }
        if (g_gl_context->bound_texture_1d_array[u] == id) {
          g_gl_context->bound_texture_1d_array[u] = 0;
        }
        if (g_gl_context->bound_texture_2d_array[u] == id) {
          g_gl_context->bound_texture_2d_array[u] = 0;
        }
        if (g_gl_context->bound_texture_2d_multisample[u] == id) {
          g_gl_context->bound_texture_2d_multisample[u] = 0;
        }
        if (g_gl_context->bound_texture_2d_multisample_array[u] == id) {
          g_gl_context->bound_texture_2d_multisample_array[u] = 0;
        }
      }
      free_texture_storage(&g_textures[id]);
      memset(&g_textures[id], 0, sizeof(g_textures[id]));
    }
  }
}

GLboolean _gl_IsTexture(GLuint texture) {
  return (texture > 0 && texture < MAX_TEXTURES && g_textures[texture].in_use)
             ? GL_TRUE
             : GL_FALSE;
}

void _gl_GenSamplers(GLsizei n, GLuint *samplers) {
  int generated = 0;

  if (!g_gl_context) {
    return;
  }
  if (n < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (n > 0 && !samplers) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  for (int i = 1; i < MAX_SAMPLER_OBJECTS && generated < n; ++i) {
    if (!g_samplers[i].in_use) {
      init_sampler_defaults(&g_samplers[i]);
      init_sampler_object(&g_samplers[i]);
      samplers[generated++] = i;
    }
  }

  if (generated < n) {
    for (int i = generated; i < n; ++i) {
      samplers[i] = 0;
    }
    _gl_set_error(GL_OUT_OF_MEMORY);
  }
}

void _gl_DeleteSamplers(GLsizei n, const GLuint *samplers) {
  if (!g_gl_context) {
    return;
  }
  if (n < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (n > 0 && !samplers) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  for (int i = 0; i < n; ++i) {
    GLuint id = samplers[i];
    if (id == 0 || id >= MAX_SAMPLER_OBJECTS || !g_samplers[id].in_use) {
      continue;
    }

    for (int unit = 0; unit < GL33_MAX_TEXTURE_UNITS; ++unit) {
      if (g_gl_context->bound_sampler[unit] == id) {
        g_gl_context->bound_sampler[unit] = 0;
      }
    }

    memset(&g_samplers[id], 0, sizeof(GLSampler));
  }
}

GLboolean _gl_IsSampler(GLuint sampler) {
  if (sampler == 0) {
    return GL_FALSE;
  }
  return (sampler < MAX_SAMPLER_OBJECTS && g_samplers[sampler].in_use)
             ? GL_TRUE
             : GL_FALSE;
}

void _gl_BindTexture(GLenum target, GLuint texture) {
  if (!g_gl_context) {
    return;
  }
  if (!is_valid_texture_target(target) &&
      !is_multisample_texture_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!valid_texture_name_for_bind(texture)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (texture > 0 && !ensure_texture_object(texture, target)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  if (texture > 0) {
    gl_framebuffer_sync_texture_for_sampling(texture);
  }
  set_bound_tex(target, texture);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_BindSampler(GLuint unit, GLuint sampler) {
  if (!g_gl_context) {
    return;
  }
  if (unit >= GL33_MAX_TEXTURE_UNITS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (sampler >= MAX_SAMPLER_OBJECTS ||
      (sampler > 0 && !g_samplers[sampler].in_use)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  g_gl_context->bound_sampler[unit] = sampler;
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_ActiveTexture(GLenum texture) {
  if (!g_gl_context) {
    return;
  }
  if (texture < GL_TEXTURE0 ||
      texture >= GL_TEXTURE0 + GL33_MAX_TEXTURE_UNITS) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  g_gl_context->active_texture = texture - GL_TEXTURE0;
}

void gl_texture_shutdown(void) {
  GLTexture *defaults[] = {
      &g_default_texture_1d,
      &g_default_texture_2d,
      &g_default_texture_3d,
      &g_default_texture_cube,
      &g_default_texture_1d_array,
      &g_default_texture_2d_array,
      &g_default_texture_2d_multisample,
      &g_default_texture_2d_multisample_array,
  };

  for (uint32_t i = 1; i < MAX_TEXTURES; ++i) {
    free_texture_storage(&g_textures[i]);
  }
  for (uint32_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); ++i) {
    free_texture_storage(defaults[i]);
  }

  memset(g_textures, 0, sizeof(g_textures));
  memset(g_samplers, 0, sizeof(g_samplers));
  memset(&g_default_texture_1d, 0, sizeof(g_default_texture_1d));
  memset(&g_default_texture_2d, 0, sizeof(g_default_texture_2d));
  memset(&g_default_texture_3d, 0, sizeof(g_default_texture_3d));
  memset(&g_default_texture_cube, 0, sizeof(g_default_texture_cube));
  memset(&g_default_texture_1d_array, 0, sizeof(g_default_texture_1d_array));
  memset(&g_default_texture_2d_array, 0, sizeof(g_default_texture_2d_array));
  memset(&g_default_texture_2d_multisample, 0,
         sizeof(g_default_texture_2d_multisample));
  memset(&g_default_texture_2d_multisample_array, 0,
         sizeof(g_default_texture_2d_multisample_array));
  memset(&g_proxy_multisample_2d, 0, sizeof(g_proxy_multisample_2d));
  memset(&g_proxy_multisample_array, 0, sizeof(g_proxy_multisample_array));
}

static void set_proxy_multisample_image(GLProxyMultisampleImage *proxy,
                                        GLsizei width, GLsizei height,
                                        GLsizei depth, GLsizei samples,
                                        GLenum internalformat,
                                        GLboolean fixed_sample_locations,
                                        bool supported) {
  memset(proxy, 0, sizeof(*proxy));
  if (!supported) {
    return;
  }
  proxy->width = width;
  proxy->height = height;
  proxy->depth = depth;
  proxy->samples = samples;
  proxy->internal_format = (GLint)internalformat;
  proxy->fixed_sample_locations = fixed_sample_locations ? GL_TRUE : GL_FALSE;
  proxy->defined = true;
}

static void define_multisample_texture(GLenum target, GLsizei samples,
                                       GLenum internalformat, GLsizei width,
                                       GLsizei height, GLsizei depth,
                                       GLboolean fixed_sample_locations) {
  TextureFormatInfo info;
  GX2AAMode aa_mode;
  GLsizei actual_samples;
  bool is_proxy = target == GL_PROXY_TEXTURE_2D_MULTISAMPLE ||
                  target == GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY;
  bool is_array = target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY ||
                  target == GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY;
  GLProxyMultisampleImage *proxy =
      is_array ? &g_proxy_multisample_array : &g_proxy_multisample_2d;

  if (samples <= 0 || samples > GL33_MAX_SAMPLES || width < 0 || height < 0 ||
      depth < 0 || width > 8192 || height > 8192 || depth > 2048) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_texture_format_info((GLint)internalformat, GL_RGBA,
                               GL_UNSIGNED_BYTE, false, &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if ((info.surface_use &
       (GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_DEPTH_BUFFER)) == 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!map_sample_count(samples, &aa_mode, &actual_samples)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (actual_samples > 1 && multisample_format_is_integer(info.gx2_format)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  if (is_proxy) {
    set_proxy_multisample_image(proxy, width, height, depth, actual_samples,
                                internalformat, fixed_sample_locations, true);
    return;
  }

  GLTexture *tex = get_bound_texture(target);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!rebuild_multisample_texture_storage(
          tex, width, height, depth, internalformat, samples,
          fixed_sample_locations)) {
    _gl_set_error(GL_OUT_OF_MEMORY);
  }
}

void _gl_TexImage2DMultisample(GLenum target, GLsizei samples,
                               GLenum internalformat, GLsizei width,
                               GLsizei height,
                               GLboolean fixedsamplelocations) {
  if (!g_gl_context) return;
  if (target != GL_TEXTURE_2D_MULTISAMPLE &&
      target != GL_PROXY_TEXTURE_2D_MULTISAMPLE) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  define_multisample_texture(target, samples, internalformat, width, height, 1,
                             fixedsamplelocations);
}

void _gl_TexImage3DMultisample(GLenum target, GLsizei samples,
                               GLenum internalformat, GLsizei width,
                               GLsizei height, GLsizei depth,
                               GLboolean fixedsamplelocations) {
  if (!g_gl_context) return;
  if (target != GL_TEXTURE_2D_MULTISAMPLE_ARRAY &&
      target != GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  define_multisample_texture(target, samples, internalformat, width, height,
                             depth, fixedsamplelocations);
}

void _gl_TexImage2D(GLenum target, GLint level, GLint internalformat,
                    GLsizei width, GLsizei height, GLint border, GLenum format,
                    GLenum type, const GLvoid *pixels) {
  GLTexture *tex;
  TextureFormatInfo info;
  GLsizei expected_width;
  GLsizei expected_height;
  GLsizei storage_width;
  GLsizei storage_height;
  GLsizei storage_depth;
  GLsizei upload_height;
  GLsizei upload_depth;
  GLenum bind_target;
  uint32_t face_index;
  const GLvoid *upload_pixels = pixels;
  bool has_unpack_pixels;

  if (!g_gl_context) {
    return;
  }

  log_texture_step("_gl_TexImage2D: begin target=0x%X level=%d internal=0x%X size=%dx%d border=%d format=0x%X type=0x%X pixels=%p",
                   (unsigned int)target, (int)level, (unsigned int)internalformat,
                   (int)width, (int)height, (int)border, (unsigned int)format,
                   (unsigned int)type, pixels);
  if (target != GL_TEXTURE_2D && target != GL_TEXTURE_1D_ARRAY &&
      !is_cube_map_face_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  bind_target = is_cube_map_face_target(target) ? GL_TEXTURE_CUBE_MAP : target;
  face_index = is_cube_map_face_target(target) ? cube_map_face_index(target) : 0;
  tex = get_bound_texture(bind_target);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!level_index_valid(level) || width < 0 || height < 0 || border != 0 ||
      width > 8192 || height > 8192 ||
      (bind_target == GL_TEXTURE_CUBE_MAP && width != height)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  storage_width = width;
  storage_height = bind_target == GL_TEXTURE_1D_ARRAY ? 1 : height;
  storage_depth = bind_target == GL_TEXTURE_CUBE_MAP
                      ? 6
                      : (bind_target == GL_TEXTURE_1D_ARRAY ? height : 1);
  upload_height = bind_target == GL_TEXTURE_1D_ARRAY ? 1 : height;
  upload_depth = bind_target == GL_TEXTURE_1D_ARRAY ? height : 1;
  has_unpack_pixels = pixels != NULL ||
                      g_gl_context->bound_pixel_unpack_buffer != 0;
  if (!get_texture_format_info(internalformat, format, type,
                               has_unpack_pixels, &info)) {
    log_texture_step("_gl_TexImage2D: format lookup failed");
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!resolve_unpack_pixels(&info, width, height, 1, pixels,
                             &upload_pixels)) {
    return;
  }

  log_texture_step("_gl_TexImage2D: format lookup ok gx2fmt=0x%X srcBytes=%u dstBytes=%u",
                   (unsigned int)info.gx2_format, (unsigned int)info.src_bytes_per_texel,
                   (unsigned int)info.dst_bytes_per_texel);

  if (level == 0) {
    if (bind_target == GL_TEXTURE_CUBE_MAP) {
      if (!tex->storage_allocated || tex->internal_format != internalformat ||
          tex->width != width || tex->height != height || tex->depth != 6) {
        log_texture_step("_gl_TexImage2D: rebuilding cube level 0 storage");
        if (!rebuild_texture_storage(tex, width, height, 6, internalformat, 1,
                                     false)) {
          log_texture_step("_gl_TexImage2D: rebuild failed");
          _gl_set_error(GL_OUT_OF_MEMORY);
          return;
        }
      }
    } else {
      log_texture_step("_gl_TexImage2D: rebuilding level 0 storage");
      if (!rebuild_texture_storage(tex, storage_width, storage_height,
                                   storage_depth, internalformat, 1, false)) {
        log_texture_step("_gl_TexImage2D: rebuild failed");
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
      }
    }
  } else {
    if (!tex->storage_allocated) {
      GLsizei base_width = width << level;
      GLsizei base_height = bind_target == GL_TEXTURE_1D_ARRAY ? 1
                                                               : height << level;
      GLsizei base_depth = bind_target == GL_TEXTURE_1D_ARRAY ? height : 1;
      if (bind_target == GL_TEXTURE_CUBE_MAP) {
        if (!rebuild_texture_storage(tex, base_width, base_height, 6,
                                     internalformat, (uint32_t)(level + 1),
                                     false)) {
          _gl_set_error(GL_OUT_OF_MEMORY);
          return;
        }
      } else if (!rebuild_texture_storage(tex, base_width, base_height,
                                          base_depth,
                                          internalformat,
                                          (uint32_t)(level + 1), false)) {
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
      }
    } else if (tex->internal_format != internalformat) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }

    expected_width = tex->width >> level;
    expected_height = bind_target == GL_TEXTURE_1D_ARRAY
                          ? tex->depth
                          : tex->height >> level;
    if (expected_width < 1) {
      expected_width = 1;
    }
    if (expected_height < 1 && bind_target != GL_TEXTURE_1D_ARRAY) {
      expected_height = 1;
    }
    if (width != expected_width || height != expected_height) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }

    if ((uint32_t)(level + 1) > tex->gx2_texture.surface.mipLevels &&
        !rebuild_texture_storage(tex, tex->width, tex->height, tex->depth,
                                 tex->internal_format, (uint32_t)(level + 1),
                                 true)) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
  }

  log_texture_step("_gl_TexImage2D: uploading level data");
  if ((bind_target == GL_TEXTURE_CUBE_MAP && upload_pixels &&
       !upload_texture_sub_region(tex, level, 0, 0, (GLint)face_index, width,
                                  height, 1, &info, upload_pixels)) ||
      (bind_target == GL_TEXTURE_1D_ARRAY &&
       !upload_texture_level(tex, level, width, upload_height, upload_depth,
                             &info, upload_pixels)) ||
      (bind_target != GL_TEXTURE_CUBE_MAP &&
       bind_target != GL_TEXTURE_1D_ARRAY &&
       !upload_texture_level(tex, level, width, height, 1, &info,
                             upload_pixels))) {
    log_texture_step("_gl_TexImage2D: upload failed");
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  mark_texture_level_defined(tex, (uint32_t)level, face_index);
  update_texture_view(tex);
  tex->complete = true;
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
  log_texture_step("_gl_TexImage2D: success");
}

void _gl_TexImage3D(GLenum target, GLint level, GLint internalformat,
                    GLsizei width, GLsizei height, GLsizei depth, GLint border,
                    GLenum format, GLenum type, const GLvoid *pixels) {
  GLTexture *tex;
  TextureFormatInfo info;
  GLsizei expected_width;
  GLsizei expected_height;
  GLsizei expected_depth;
  const GLvoid *upload_pixels = pixels;
  bool has_unpack_pixels;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  tex = get_bound_texture(target);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!level_index_valid(level) || width < 0 || height < 0 || depth < 0 ||
      border != 0 || width > 8192 || height > 8192 || depth > 2048) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  has_unpack_pixels = pixels != NULL ||
                      g_gl_context->bound_pixel_unpack_buffer != 0;
  if (!get_texture_format_info(internalformat, format, type,
                               has_unpack_pixels, &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!resolve_unpack_pixels(&info, width, height, depth, pixels,
                             &upload_pixels)) {
    return;
  }

  if (level == 0) {
    if (!rebuild_texture_storage(tex, width, height, depth, internalformat, 1,
                                 false)) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
  } else {
    if (!tex->storage_allocated) {
      GLsizei base_width = width << level;
      GLsizei base_height = height << level;
      GLsizei base_depth =
          texture_depth_is_spatial(target) ? depth << level : depth;
      if (!rebuild_texture_storage(tex, base_width, base_height, base_depth,
                                   internalformat, (uint32_t)(level + 1),
                                   false)) {
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
      }
    } else if (tex->internal_format != internalformat) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }

    expected_width = tex->width >> level;
    expected_height = tex->height >> level;
    expected_depth =
        texture_depth_is_spatial(target) ? tex->depth >> level : tex->depth;
    if (expected_width < 1) {
      expected_width = 1;
    }
    if (expected_height < 1) {
      expected_height = 1;
    }
    if (expected_depth < 1) {
      expected_depth = 1;
    }
    if (width != expected_width || height != expected_height ||
        depth != expected_depth) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }

    if ((uint32_t)(level + 1) > tex->gx2_texture.surface.mipLevels &&
        !rebuild_texture_storage(tex, tex->width, tex->height, tex->depth,
                                 tex->internal_format, (uint32_t)(level + 1),
                                 true)) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
  }

  if (!upload_texture_level(tex, level, width, height, depth, &info,
                            upload_pixels)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  mark_texture_level_defined(tex, (uint32_t)level, 0);
  update_texture_view(tex);
  tex->complete = true;
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                       GLsizei width, GLsizei height, GLenum format,
                       GLenum type, const GLvoid *pixels) {
  GLTexture *tex;
  TextureFormatInfo info;
  GLenum bind_target;
  uint32_t face_index;
  const GLvoid *upload_pixels = pixels;
  bool has_unpack_pixels;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_2D && target != GL_TEXTURE_1D_ARRAY &&
      !is_cube_map_face_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  bind_target = is_cube_map_face_target(target) ? GL_TEXTURE_CUBE_MAP : target;
  face_index = is_cube_map_face_target(target) ? cube_map_face_index(target) : 0;
  tex = get_bound_texture(bind_target);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!level_index_valid(level) || xoffset < 0 || yoffset < 0 ||
      width < 0 || height < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  if (!tex->storage_allocated) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if ((uint32_t)level >= tex->gx2_texture.surface.mipLevels) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!texture_face_level_defined(tex, (uint32_t)level, face_index)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  has_unpack_pixels = pixels != NULL ||
                      g_gl_context->bound_pixel_unpack_buffer != 0;
  if (!get_texture_format_info(tex->internal_format, format, type,
                               has_unpack_pixels, &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!resolve_unpack_pixels(&info, width, height, 1, pixels,
                             &upload_pixels)) {
    return;
  }
  if ((bind_target == GL_TEXTURE_1D_ARRAY &&
       !upload_texture_sub_region(tex, level, xoffset, 0, yoffset, width, 1,
                                  height, &info, upload_pixels)) ||
      (bind_target != GL_TEXTURE_1D_ARRAY &&
       !upload_texture_sub_region(tex, level, xoffset, yoffset,
                                  (GLint)face_index, width, height, 1, &info,
                                  upload_pixels))) {
    _gl_set_error(upload_pixels ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
    return;
  }

  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_TexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                       GLint zoffset, GLsizei width, GLsizei height,
                       GLsizei depth, GLenum format, GLenum type,
                       const GLvoid *pixels) {
  GLTexture *tex;
  TextureFormatInfo info;
  const GLvoid *upload_pixels = pixels;
  bool has_unpack_pixels;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  tex = get_bound_texture(target);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!level_index_valid(level) || xoffset < 0 || yoffset < 0 ||
      zoffset < 0 || width < 0 || height < 0 || depth < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  if (!tex->storage_allocated) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if ((uint32_t)level >= tex->gx2_texture.surface.mipLevels) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!texture_level_defined(tex, (uint32_t)level)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  has_unpack_pixels = pixels != NULL ||
                      g_gl_context->bound_pixel_unpack_buffer != 0;
  if (!get_texture_format_info(tex->internal_format, format, type,
                               has_unpack_pixels, &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!resolve_unpack_pixels(&info, width, height, depth, pixels,
                             &upload_pixels)) {
    return;
  }
  if (!upload_texture_sub_region(tex, level, xoffset, yoffset, zoffset, width,
                                 height, depth, &info, upload_pixels)) {
    _gl_set_error(upload_pixels ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
    return;
  }

  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_CopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                        GLint x, GLint y, GLsizei width, GLsizei height,
                        GLint border) {
  GLTexture *tex;
  TextureFormatInfo info;
  GLsizei expected_width;
  GLsizei expected_height;
  GLenum bind_target;
  uint32_t face_index;
  uint8_t *upload_pixels = NULL;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_2D && !is_cube_map_face_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  bind_target = target == GL_TEXTURE_2D ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP;
  face_index = is_cube_map_face_target(target) ? cube_map_face_index(target) : 0;
  tex = get_bound_texture(bind_target);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!level_index_valid(level) || width < 0 || height < 0 || border != 0 ||
      (bind_target == GL_TEXTURE_CUBE_MAP && width != height)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_texture_format_info(internalformat, GL_RGBA, GL_UNSIGNED_BYTE, false,
                               &info) ||
      !is_copy_color_format_supported(internalformat, &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  if (level > 0) {
    if (!tex->storage_allocated) {
      GLsizei base_width = width << level;
      GLsizei base_height = height << level;
      if (!rebuild_texture_storage(tex, base_width, base_height,
                                   bind_target == GL_TEXTURE_CUBE_MAP ? 6 : 1,
                                   internalformat, (uint32_t)(level + 1),
                                   false)) {
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
      }
    } else if (tex->internal_format != internalformat) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }

    expected_width = tex->width >> level;
    expected_height = tex->height >> level;
    if (expected_width < 1) {
      expected_width = 1;
    }
    if (expected_height < 1) {
      expected_height = 1;
    }
    if (width != expected_width || height != expected_height) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
  }

  upload_pixels =
      build_copy_upload_pixels(x, y, width, height, internalformat, &info);
  if ((width > 0 && height > 0) && !upload_pixels) {
    return;
  }

  if (level == 0) {
    if ((bind_target != GL_TEXTURE_CUBE_MAP ||
         !tex->storage_allocated || tex->internal_format != internalformat ||
         tex->width != width || tex->height != height || tex->depth != 6) &&
        !rebuild_texture_storage(
            tex, width, height,
            bind_target == GL_TEXTURE_CUBE_MAP ? 6 : 1, internalformat, 1,
            false)) {
      if (upload_pixels) {
        gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
      }
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
  } else if ((uint32_t)(level + 1) > tex->gx2_texture.surface.mipLevels &&
             !rebuild_texture_storage(tex, tex->width, tex->height, tex->depth,
                                      tex->internal_format,
                                      (uint32_t)(level + 1), true)) {
    if (upload_pixels) {
      gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
    }
    _gl_set_error(GL_OUT_OF_MEMORY);
    return;
  }

  if (upload_pixels &&
      ((bind_target == GL_TEXTURE_CUBE_MAP &&
        !upload_texture_sub_region(tex, level, 0, 0, (GLint)face_index, width,
                                   height, 1, &info, upload_pixels)) ||
       (bind_target != GL_TEXTURE_CUBE_MAP &&
        !upload_texture_level(tex, level, width, height, 1, &info,
                              upload_pixels)))) {
    gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  if (upload_pixels) {
    gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
  }
  mark_texture_level_defined(tex, (uint32_t)level, face_index);
  update_texture_view(tex);
  tex->complete = true;
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_CopyTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                           GLint yoffset, GLint x, GLint y, GLsizei width,
                           GLsizei height) {
  GLTexture *tex;
  TextureFormatInfo info;
  GLenum bind_target;
  uint32_t face_index;
  uint8_t *upload_pixels = NULL;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_2D && !is_cube_map_face_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  bind_target = target == GL_TEXTURE_2D ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP;
  face_index = is_cube_map_face_target(target) ? cube_map_face_index(target) : 0;
  tex = get_bound_texture(bind_target);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!level_index_valid(level) || xoffset < 0 || yoffset < 0 ||
      width < 0 || height < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  if (!tex->storage_allocated) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if ((uint32_t)level >= tex->gx2_texture.surface.mipLevels) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!texture_face_level_defined(tex, (uint32_t)level, face_index)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!get_texture_format_info(tex->internal_format, GL_RGBA,
                               GL_UNSIGNED_BYTE, false, &info) ||
      !is_copy_color_format_supported(tex->internal_format, &info)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (width == 0 || height == 0) {
    return;
  }

  upload_pixels =
      build_copy_upload_pixels(x, y, width, height, tex->internal_format,
                               &info);
  if (!upload_pixels) {
    return;
  }

  if (!upload_texture_sub_region(tex, level, xoffset, yoffset,
                                 (GLint)face_index, width, height, 1, &info,
                                 upload_pixels)) {
    gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_CompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                              GLsizei width, GLsizei height, GLint border,
                              GLsizei imageSize, const GLvoid *data) {
  GLTexture *tex;
  TextureFormatInfo info;
  GLenum bind_target;
  uint32_t face_index;
  GLsizei expected_size;
  const GLvoid *upload_data = data;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_2D && !is_cube_map_face_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!level_index_valid(level) || width < 0 || height < 0 || border < 0 ||
      border > 1 || imageSize < 0 || width > 8192 || height > 8192 ||
      (is_cube_map_face_target(target) && width != height)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_compressed_format_info(internalformat, &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (border != 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!compressed_image_size(width, height, info.block_bytes,
                             &expected_size) || imageSize != expected_size) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  bind_target = target == GL_TEXTURE_2D ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP;
  face_index = is_cube_map_face_target(target) ? cube_map_face_index(target) : 0;
  tex = get_bound_texture(bind_target);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!resolve_compressed_unpack_data(data, imageSize, &upload_data)) {
    return;
  }

  if (level == 0) {
    if (bind_target == GL_TEXTURE_CUBE_MAP) {
      if ((!tex->storage_allocated || tex->internal_format != internalformat ||
           tex->width != width || tex->height != height || tex->depth != 6) &&
          !rebuild_texture_storage(tex, width, height, 6, internalformat, 1,
                                   false)) {
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
      }
    } else if (!rebuild_texture_storage(tex, width, height, 1,
                                        internalformat, 1, false)) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
  } else {
    if (!tex->storage_allocated) {
      uint64_t base_width = (uint64_t)(uint32_t)width << (uint32_t)level;
      uint64_t base_height = (uint64_t)(uint32_t)height << (uint32_t)level;
      if (base_width > 8192u || base_height > 8192u ||
          !rebuild_texture_storage(
              tex, (GLsizei)base_width, (GLsizei)base_height,
              bind_target == GL_TEXTURE_CUBE_MAP ? 6 : 1, internalformat,
              (uint32_t)(level + 1), false)) {
        _gl_set_error(base_width > 8192u || base_height > 8192u
                          ? GL_INVALID_VALUE
                          : GL_OUT_OF_MEMORY);
        return;
      }
    } else if (tex->internal_format != internalformat) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }

    GLsizei expected_width = tex->width >> level;
    GLsizei expected_height = tex->height >> level;
    if (expected_width < 1) expected_width = 1;
    if (expected_height < 1) expected_height = 1;
    if (width != expected_width || height != expected_height) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    if ((uint32_t)(level + 1) > tex->gx2_texture.surface.mipLevels &&
        !rebuild_texture_storage(tex, tex->width, tex->height, tex->depth,
                                 tex->internal_format,
                                 (uint32_t)(level + 1), true)) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
  }

  if (imageSize > 0 && upload_data &&
      !upload_compressed_texture_region(tex, level, 0, 0, (GLint)face_index,
                                        width, height, &info, upload_data)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  mark_texture_level_defined(tex, (uint32_t)level, face_index);
  update_texture_view(tex);
  tex->complete = true;
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_CompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                 GLint yoffset, GLsizei width, GLsizei height,
                                 GLenum format, GLsizei imageSize,
                                 const GLvoid *data) {
  GLTexture *tex;
  TextureFormatInfo info;
  TextureLevelLayout layout;
  GLenum bind_target;
  uint32_t face_index;
  GLsizei expected_size;
  const GLvoid *upload_data = data;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_2D && !is_cube_map_face_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!level_index_valid(level) || xoffset < 0 || yoffset < 0 ||
      width < 0 || height < 0 || imageSize < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_compressed_format_info(format, &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  bind_target = target == GL_TEXTURE_2D ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP;
  face_index = is_cube_map_face_target(target) ? cube_map_face_index(target) : 0;
  tex = get_bound_texture(bind_target);
  if (!tex || !tex->storage_allocated) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if ((uint32_t)level >= tex->gx2_texture.surface.mipLevels) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!texture_face_level_defined(tex, (uint32_t)level, face_index) ||
      !get_compressed_format_info((GLenum)tex->internal_format, &info)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if ((GLenum)tex->internal_format != format) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!calc_level_layout(tex->target, tex->gx2_texture.surface.format,
                         tex->width, tex->height, tex->depth,
                         (uint32_t)level, &layout) ||
      (uint64_t)(uint32_t)xoffset + (uint32_t)width >
          (uint32_t)layout.width ||
      (uint64_t)(uint32_t)yoffset + (uint32_t)height >
          (uint32_t)layout.height) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if ((xoffset & 3) != 0 || (yoffset & 3) != 0 ||
      ((width & 3) != 0 &&
       (uint64_t)(uint32_t)xoffset + (uint32_t)width !=
           (uint32_t)layout.width) ||
      ((height & 3) != 0 &&
       (uint64_t)(uint32_t)yoffset + (uint32_t)height !=
           (uint32_t)layout.height)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!compressed_image_size(width, height, info.block_bytes,
                             &expected_size) || imageSize != expected_size) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (width == 0 || height == 0) {
    return;
  }
  if (!resolve_compressed_unpack_data(data, imageSize, &upload_data)) {
    return;
  }
  if (!upload_data || !upload_compressed_texture_region(
                          tex, level, xoffset, yoffset, (GLint)face_index,
                          width, height, &info, upload_data)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

static bool apply_sampler_integer_parameter(GLenum pname, GLint param,
                                            GLenum *min_filter,
                                            GLenum *mag_filter,
                                            GLenum *wrap_s, GLenum *wrap_t,
                                            GLenum *wrap_r,
                                            GLenum *compare_mode,
                                            GLenum *compare_func,
                                            GLenum *error_out) {
  if (error_out) {
    *error_out = GL_INVALID_ENUM;
  }

  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    if (!is_valid_min_filter(param)) {
      if (error_out) *error_out = GL_INVALID_ENUM;
      return false;
    }
    *min_filter = param;
    return true;
  case GL_TEXTURE_MAG_FILTER:
    if (!is_valid_mag_filter(param)) {
      if (error_out) *error_out = GL_INVALID_ENUM;
      return false;
    }
    *mag_filter = param;
    return true;
  case GL_TEXTURE_WRAP_S:
    if (!is_valid_wrap_mode(param)) {
      if (error_out) *error_out = GL_INVALID_ENUM;
      return false;
    }
    *wrap_s = param;
    return true;
  case GL_TEXTURE_WRAP_T:
    if (!is_valid_wrap_mode(param)) {
      if (error_out) *error_out = GL_INVALID_ENUM;
      return false;
    }
    *wrap_t = param;
    return true;
  case GL_TEXTURE_WRAP_R:
    if (!is_valid_wrap_mode(param)) {
      if (error_out) *error_out = GL_INVALID_ENUM;
      return false;
    }
    *wrap_r = param;
    return true;
  case GL_TEXTURE_COMPARE_MODE:
    if (!is_valid_compare_mode(param)) {
      if (error_out) *error_out = GL_INVALID_ENUM;
      return false;
    }
    *compare_mode = param;
    return true;
  case GL_TEXTURE_COMPARE_FUNC:
    if (!is_valid_compare_func(param)) {
      if (error_out) *error_out = GL_INVALID_ENUM;
      return false;
    }
    *compare_func = param;
    return true;
  default:
    return false;
  }
}

static bool apply_texture_integer_parameter(GLTexture *tex, GLenum pname,
                                            GLint param, GLenum *error_out) {
  if (error_out) {
    *error_out = GL_INVALID_ENUM;
  }

  switch (pname) {
  case GL_TEXTURE_BASE_LEVEL:
    if (param < 0) {
      if (error_out) *error_out = GL_INVALID_VALUE;
      return false;
    }
    tex->base_level = param;
    update_texture_view(tex);
    return true;
  case GL_TEXTURE_MAX_LEVEL:
    if (param < 0) {
      if (error_out) *error_out = GL_INVALID_VALUE;
      return false;
    }
    tex->max_level = param;
    update_texture_view(tex);
    return true;
  case GL_TEXTURE_SWIZZLE_R:
    if (!is_valid_swizzle(param)) return false;
    tex->swizzle[0] = param;
    return true;
  case GL_TEXTURE_SWIZZLE_G:
    if (!is_valid_swizzle(param)) return false;
    tex->swizzle[1] = param;
    return true;
  case GL_TEXTURE_SWIZZLE_B:
    if (!is_valid_swizzle(param)) return false;
    tex->swizzle[2] = param;
    return true;
  case GL_TEXTURE_SWIZZLE_A:
    if (!is_valid_swizzle(param)) return false;
    tex->swizzle[3] = param;
    return true;
  default:
    return apply_sampler_integer_parameter(
        pname, param, &tex->min_filter, &tex->mag_filter, &tex->wrap_s,
        &tex->wrap_t, &tex->wrap_r, &tex->compare_mode, &tex->compare_func,
        error_out);
  }
}

static bool apply_texture_float_parameter(GLTexture *tex, GLenum pname,
                                          GLfloat param, GLenum *error_out) {
  if (error_out) {
    *error_out = GL_INVALID_ENUM;
  }

  switch (pname) {
  case GL_TEXTURE_MIN_LOD:
    tex->min_lod = param;
    return true;
  case GL_TEXTURE_MAX_LOD:
    tex->max_lod = param;
    return true;
  case GL_TEXTURE_LOD_BIAS:
    tex->lod_bias = param;
    return true;
  default:
    return apply_texture_integer_parameter(tex, pname, (GLint)param, error_out);
  }
}

static bool apply_sampler_float_parameter(GLSampler *sampler, GLenum pname,
                                          GLfloat param, GLenum *error_out) {
  if (error_out) {
    *error_out = GL_INVALID_ENUM;
  }

  switch (pname) {
  case GL_TEXTURE_MIN_LOD:
    sampler->min_lod = param;
    return true;
  case GL_TEXTURE_MAX_LOD:
    sampler->max_lod = param;
    return true;
  case GL_TEXTURE_LOD_BIAS:
    sampler->lod_bias = param;
    return true;
  default:
    return apply_sampler_integer_parameter(
        pname, (GLint)param, &sampler->min_filter, &sampler->mag_filter,
        &sampler->wrap_s, &sampler->wrap_t, &sampler->wrap_r,
        &sampler->compare_mode, &sampler->compare_func, error_out);
  }
}

static GLTexture *texture_for_parameter_target(GLenum target) {
  if (!is_valid_texture_target(target)) {
    _gl_set_error(GL_INVALID_ENUM);
    return NULL;
  }

  GLTexture *tex = get_bound_texture(target);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return NULL;
  }
  return tex;
}

void _gl_TexParameteri(GLenum target, GLenum pname, GLint param) {
  GLTexture *tex;
  GLenum error = GL_INVALID_ENUM;

  if (!g_gl_context) {
    return;
  }

  tex = texture_for_parameter_target(target);
  if (!tex) {
    return;
  }

  if (!apply_texture_integer_parameter(tex, pname, param, &error)) {
    _gl_set_error(error);
    return;
  }

  init_texture_sampler(tex);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_TexParameterf(GLenum target, GLenum pname, GLfloat param) {
  GLTexture *tex;
  GLenum error = GL_INVALID_ENUM;

  if (!g_gl_context) {
    return;
  }

  tex = texture_for_parameter_target(target);
  if (!tex) {
    return;
  }

  if (!apply_texture_float_parameter(tex, pname, param, &error)) {
    _gl_set_error(error);
    return;
  }

  init_texture_sampler(tex);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_TexParameteriv(GLenum target, GLenum pname, const GLint *params) {
  GLTexture *tex;
  GLenum error = GL_INVALID_ENUM;

  if (!params || !g_gl_context) {
    return;
  }

  tex = texture_for_parameter_target(target);
  if (!tex) {
    return;
  }

  if (pname == GL_TEXTURE_SWIZZLE_RGBA) {
    for (uint32_t i = 0; i < 4; ++i) {
      if (!is_valid_swizzle(params[i])) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
      }
    }
    tex->swizzle[0] = params[0];
    tex->swizzle[1] = params[1];
    tex->swizzle[2] = params[2];
    tex->swizzle[3] = params[3];
  } else if (pname == GL_TEXTURE_BORDER_COLOR) {
    tex->border_color[0] = (GLfloat)params[0];
    tex->border_color[1] = (GLfloat)params[1];
    tex->border_color[2] = (GLfloat)params[2];
    tex->border_color[3] = (GLfloat)params[3];
  } else if (!apply_texture_integer_parameter(tex, pname, params[0], &error)) {
    _gl_set_error(error);
    return;
  }

  init_texture_sampler(tex);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_TexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
  GLTexture *tex;
  GLenum error = GL_INVALID_ENUM;

  if (!params || !g_gl_context) {
    return;
  }

  tex = texture_for_parameter_target(target);
  if (!tex) {
    return;
  }

  if (pname == GL_TEXTURE_BORDER_COLOR) {
    tex->border_color[0] = params[0];
    tex->border_color[1] = params[1];
    tex->border_color[2] = params[2];
    tex->border_color[3] = params[3];
  } else if (pname == GL_TEXTURE_SWIZZLE_RGBA) {
    GLint swizzle[4] = {(GLint)params[0], (GLint)params[1],
                        (GLint)params[2], (GLint)params[3]};
    for (uint32_t i = 0; i < 4; ++i) {
      if (!is_valid_swizzle(swizzle[i])) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
      }
    }
    tex->swizzle[0] = swizzle[0];
    tex->swizzle[1] = swizzle[1];
    tex->swizzle[2] = swizzle[2];
    tex->swizzle[3] = swizzle[3];
  } else if (!apply_texture_float_parameter(tex, pname, params[0], &error)) {
    _gl_set_error(error);
    return;
  }

  init_texture_sampler(tex);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_GetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
  GLTexture *tex;

  if (!g_gl_context || !params) {
    return;
  }

  tex = texture_for_parameter_target(target);
  if (!tex) {
    return;
  }

  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    *params = (GLint)tex->min_filter;
    break;
  case GL_TEXTURE_MAG_FILTER:
    *params = (GLint)tex->mag_filter;
    break;
  case GL_TEXTURE_WRAP_S:
    *params = (GLint)tex->wrap_s;
    break;
  case GL_TEXTURE_WRAP_T:
    *params = (GLint)tex->wrap_t;
    break;
  case GL_TEXTURE_WRAP_R:
    *params = (GLint)tex->wrap_r;
    break;
  case GL_TEXTURE_BASE_LEVEL:
    *params = tex->base_level;
    break;
  case GL_TEXTURE_MAX_LEVEL:
    *params = tex->max_level;
    break;
  case GL_TEXTURE_MIN_LOD:
    *params = (GLint)tex->min_lod;
    break;
  case GL_TEXTURE_MAX_LOD:
    *params = (GLint)tex->max_lod;
    break;
  case GL_TEXTURE_LOD_BIAS:
    *params = (GLint)tex->lod_bias;
    break;
  case GL_TEXTURE_COMPARE_MODE:
    *params = (GLint)tex->compare_mode;
    break;
  case GL_TEXTURE_COMPARE_FUNC:
    *params = (GLint)tex->compare_func;
    break;
  case GL_TEXTURE_SWIZZLE_R:
    *params = (GLint)tex->swizzle[0];
    break;
  case GL_TEXTURE_SWIZZLE_G:
    *params = (GLint)tex->swizzle[1];
    break;
  case GL_TEXTURE_SWIZZLE_B:
    *params = (GLint)tex->swizzle[2];
    break;
  case GL_TEXTURE_SWIZZLE_A:
    *params = (GLint)tex->swizzle[3];
    break;
  case GL_TEXTURE_SWIZZLE_RGBA:
    params[0] = (GLint)tex->swizzle[0];
    params[1] = (GLint)tex->swizzle[1];
    params[2] = (GLint)tex->swizzle[2];
    params[3] = (GLint)tex->swizzle[3];
    break;
  case GL_TEXTURE_BORDER_COLOR:
    params[0] = (GLint)tex->border_color[0];
    params[1] = (GLint)tex->border_color[1];
    params[2] = (GLint)tex->border_color[2];
    params[3] = (GLint)tex->border_color[3];
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_GetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {
  GLTexture *tex;

  if (!g_gl_context || !params) {
    return;
  }
  tex = texture_for_parameter_target(target);
  if (!tex) {
    return;
  }

  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    *params = (GLfloat)tex->min_filter;
    break;
  case GL_TEXTURE_MAG_FILTER:
    *params = (GLfloat)tex->mag_filter;
    break;
  case GL_TEXTURE_WRAP_S:
    *params = (GLfloat)tex->wrap_s;
    break;
  case GL_TEXTURE_WRAP_T:
    *params = (GLfloat)tex->wrap_t;
    break;
  case GL_TEXTURE_WRAP_R:
    *params = (GLfloat)tex->wrap_r;
    break;
  case GL_TEXTURE_BASE_LEVEL:
    *params = (GLfloat)tex->base_level;
    break;
  case GL_TEXTURE_MAX_LEVEL:
    *params = (GLfloat)tex->max_level;
    break;
  case GL_TEXTURE_MIN_LOD:
    *params = tex->min_lod;
    break;
  case GL_TEXTURE_MAX_LOD:
    *params = tex->max_lod;
    break;
  case GL_TEXTURE_LOD_BIAS:
    *params = tex->lod_bias;
    break;
  case GL_TEXTURE_COMPARE_MODE:
    *params = (GLfloat)tex->compare_mode;
    break;
  case GL_TEXTURE_COMPARE_FUNC:
    *params = (GLfloat)tex->compare_func;
    break;
  case GL_TEXTURE_SWIZZLE_R:
    *params = (GLfloat)tex->swizzle[0];
    break;
  case GL_TEXTURE_SWIZZLE_G:
    *params = (GLfloat)tex->swizzle[1];
    break;
  case GL_TEXTURE_SWIZZLE_B:
    *params = (GLfloat)tex->swizzle[2];
    break;
  case GL_TEXTURE_SWIZZLE_A:
    *params = (GLfloat)tex->swizzle[3];
    break;
  case GL_TEXTURE_SWIZZLE_RGBA:
    params[0] = (GLfloat)tex->swizzle[0];
    params[1] = (GLfloat)tex->swizzle[1];
    params[2] = (GLfloat)tex->swizzle[2];
    params[3] = (GLfloat)tex->swizzle[3];
    break;
  case GL_TEXTURE_BORDER_COLOR:
    params[0] = tex->border_color[0];
    params[1] = tex->border_color[1];
    params[2] = tex->border_color[2];
    params[3] = tex->border_color[3];
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_SamplerParameteriv(GLuint sampler, GLenum pname, const GLint *param) {
  if (!param || !g_gl_context) {
    return;
  }

  if (pname == GL_TEXTURE_BORDER_COLOR) {
    GLfloat values[4] = {(GLfloat)param[0], (GLfloat)param[1],
                         (GLfloat)param[2], (GLfloat)param[3]};
    _gl_SamplerParameterfv(sampler, pname, values);
    return;
  }

  _gl_SamplerParameteri(sampler, pname, param[0]);
}

void _gl_SamplerParameterfv(GLuint sampler, GLenum pname,
                            const GLfloat *param) {
  GLenum error = GL_INVALID_ENUM;

  if (!param || !g_gl_context) {
    return;
  }

  if (sampler == 0 || sampler >= MAX_SAMPLER_OBJECTS ||
      !g_samplers[sampler].in_use) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  if (pname == GL_TEXTURE_BORDER_COLOR) {
    g_samplers[sampler].border_color[0] = param[0];
    g_samplers[sampler].border_color[1] = param[1];
    g_samplers[sampler].border_color[2] = param[2];
    g_samplers[sampler].border_color[3] = param[3];
  } else if (!apply_sampler_float_parameter(&g_samplers[sampler], pname,
                                            param[0], &error)) {
    _gl_set_error(error);
    return;
  }

  init_sampler_object(&g_samplers[sampler]);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_SamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
  GLenum error = GL_INVALID_ENUM;

  if (!g_gl_context) {
    return;
  }
  if (sampler == 0 || sampler >= MAX_SAMPLER_OBJECTS ||
      !g_samplers[sampler].in_use) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!apply_sampler_integer_parameter(
          pname, param, &g_samplers[sampler].min_filter,
          &g_samplers[sampler].mag_filter, &g_samplers[sampler].wrap_s,
          &g_samplers[sampler].wrap_t, &g_samplers[sampler].wrap_r,
          &g_samplers[sampler].compare_mode,
          &g_samplers[sampler].compare_func, &error)) {
    _gl_set_error(error);
    return;
  }

  init_sampler_object(&g_samplers[sampler]);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_SamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
  GLenum error = GL_INVALID_ENUM;

  if (!g_gl_context) {
    return;
  }
  if (sampler == 0 || sampler >= MAX_SAMPLER_OBJECTS ||
      !g_samplers[sampler].in_use) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (pname == GL_TEXTURE_BORDER_COLOR ||
      !apply_sampler_float_parameter(&g_samplers[sampler], pname, param,
                                     &error)) {
    _gl_set_error(error);
    return;
  }

  init_sampler_object(&g_samplers[sampler]);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_GetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params) {
  if (!g_gl_context || !params) {
    return;
  }
  if (sampler == 0 || sampler >= MAX_SAMPLER_OBJECTS ||
      !g_samplers[sampler].in_use) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    *params = (GLint)g_samplers[sampler].min_filter;
    break;
  case GL_TEXTURE_MAG_FILTER:
    *params = (GLint)g_samplers[sampler].mag_filter;
    break;
  case GL_TEXTURE_WRAP_S:
    *params = (GLint)g_samplers[sampler].wrap_s;
    break;
  case GL_TEXTURE_WRAP_T:
    *params = (GLint)g_samplers[sampler].wrap_t;
    break;
  case GL_TEXTURE_WRAP_R:
    *params = (GLint)g_samplers[sampler].wrap_r;
    break;
  case GL_TEXTURE_MIN_LOD:
    *params = (GLint)g_samplers[sampler].min_lod;
    break;
  case GL_TEXTURE_MAX_LOD:
    *params = (GLint)g_samplers[sampler].max_lod;
    break;
  case GL_TEXTURE_LOD_BIAS:
    *params = (GLint)g_samplers[sampler].lod_bias;
    break;
  case GL_TEXTURE_COMPARE_MODE:
    *params = (GLint)g_samplers[sampler].compare_mode;
    break;
  case GL_TEXTURE_COMPARE_FUNC:
    *params = (GLint)g_samplers[sampler].compare_func;
    break;
  case GL_TEXTURE_BORDER_COLOR:
    params[0] = (GLint)g_samplers[sampler].border_color[0];
    params[1] = (GLint)g_samplers[sampler].border_color[1];
    params[2] = (GLint)g_samplers[sampler].border_color[2];
    params[3] = (GLint)g_samplers[sampler].border_color[3];
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_GetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params) {
  if (!g_gl_context || !params) {
    return;
  }
  if (sampler == 0 || sampler >= MAX_SAMPLER_OBJECTS ||
      !g_samplers[sampler].in_use) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    *params = (GLfloat)g_samplers[sampler].min_filter;
    break;
  case GL_TEXTURE_MAG_FILTER:
    *params = (GLfloat)g_samplers[sampler].mag_filter;
    break;
  case GL_TEXTURE_WRAP_S:
    *params = (GLfloat)g_samplers[sampler].wrap_s;
    break;
  case GL_TEXTURE_WRAP_T:
    *params = (GLfloat)g_samplers[sampler].wrap_t;
    break;
  case GL_TEXTURE_WRAP_R:
    *params = (GLfloat)g_samplers[sampler].wrap_r;
    break;
  case GL_TEXTURE_MIN_LOD:
    *params = g_samplers[sampler].min_lod;
    break;
  case GL_TEXTURE_MAX_LOD:
    *params = g_samplers[sampler].max_lod;
    break;
  case GL_TEXTURE_LOD_BIAS:
    *params = g_samplers[sampler].lod_bias;
    break;
  case GL_TEXTURE_COMPARE_MODE:
    *params = (GLfloat)g_samplers[sampler].compare_mode;
    break;
  case GL_TEXTURE_COMPARE_FUNC:
    *params = (GLfloat)g_samplers[sampler].compare_func;
    break;
  case GL_TEXTURE_BORDER_COLOR:
    params[0] = g_samplers[sampler].border_color[0];
    params[1] = g_samplers[sampler].border_color[1];
    params[2] = g_samplers[sampler].border_color[2];
    params[3] = g_samplers[sampler].border_color[3];
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}

void _gl_TexImage1D(GLenum target, GLint level, GLint internalformat,
                   GLsizei width, GLint border, GLenum format,
                   GLenum type, const GLvoid *pixels) {
  GLTexture *tex;
  TextureFormatInfo info;
  GLsizei expected_width;
  const GLvoid *upload_pixels = pixels;
  bool has_unpack_pixels;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_1D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  tex = get_bound_texture(GL_TEXTURE_1D);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!level_index_valid(level) || width < 0 || border != 0 || width > 8192) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  has_unpack_pixels = pixels != NULL ||
                      g_gl_context->bound_pixel_unpack_buffer != 0;
  if (!get_texture_format_info(internalformat, format, type,
                               has_unpack_pixels, &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!resolve_unpack_pixels(&info, width, 1, 1, pixels,
                             &upload_pixels)) {
    return;
  }

  if (level == 0) {
    if (!rebuild_texture_storage(tex, width, 1, 1, internalformat, 1, false)) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
  } else {
    if (!tex->storage_allocated) {
      GLsizei base_width = width << level;
      if (!rebuild_texture_storage(tex, base_width, 1, 1, internalformat,
                                   (uint32_t)(level + 1), false)) {
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
      }
    } else if (tex->internal_format != internalformat) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }
    expected_width = tex->width >> level;
    if (expected_width < 1) {
      expected_width = 1;
    }
    if (width != expected_width) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    if ((uint32_t)(level + 1) > tex->gx2_texture.surface.mipLevels &&
        !rebuild_texture_storage(tex, tex->width, tex->height, tex->depth,
                                 tex->internal_format, (uint32_t)(level + 1),
                                 true)) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
  }

  if (!upload_texture_level(tex, level, width, 1, 1, &info, upload_pixels)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  mark_texture_level_defined(tex, (uint32_t)level, 0);
  update_texture_view(tex);
  tex->complete = true;
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_TexSubImage1D(GLenum target, GLint level, GLint xoffset,
                       GLsizei width, GLenum format, GLenum type,
                       const GLvoid *pixels) {
  GLTexture *tex;
  TextureFormatInfo info;
  const GLvoid *upload_pixels = pixels;
  bool has_unpack_pixels;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_1D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  tex = get_bound_texture(GL_TEXTURE_1D);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!level_index_valid(level) || xoffset < 0 || width < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!tex->storage_allocated) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if ((uint32_t)level >= tex->gx2_texture.surface.mipLevels) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!texture_level_defined(tex, (uint32_t)level)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  has_unpack_pixels = pixels != NULL ||
                      g_gl_context->bound_pixel_unpack_buffer != 0;
  if (!get_texture_format_info(tex->internal_format, format, type,
                               has_unpack_pixels, &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!resolve_unpack_pixels(&info, width, 1, 1, pixels, &upload_pixels)) {
    return;
  }
  if (!upload_texture_sub_region(tex, level, xoffset, 0, 0, width, 1, 1,
                                 &info, upload_pixels)) {
    _gl_set_error(upload_pixels ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
    return;
  }
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_CopyTexImage1D(GLenum target, GLint level, GLenum internalformat,
                        GLint x, GLint y, GLsizei width, GLint border) {
  GLTexture *tex;
  TextureFormatInfo info;
  uint8_t *upload_pixels;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_1D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  tex = get_bound_texture(GL_TEXTURE_1D);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!level_index_valid(level) || width < 0 || border != 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_texture_format_info(internalformat, GL_RGBA, GL_UNSIGNED_BYTE, false,
                               &info) ||
      !is_copy_color_format_supported(internalformat, &info)) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  upload_pixels = build_copy_upload_pixels(x, y, width, 1, internalformat,
                                           &info);
  if (width > 0 && !upload_pixels) {
    return;
  }
  if (!rebuild_texture_storage(tex, level > 0 ? (width << level) : width, 1,
                               1, internalformat, (uint32_t)(level + 1),
                               level > 0)) {
    if (upload_pixels) {
      gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
    }
    _gl_set_error(GL_OUT_OF_MEMORY);
    return;
  }
  if (upload_pixels &&
      !upload_texture_level(tex, level, width, 1, 1, &info, upload_pixels)) {
    gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (upload_pixels) {
    gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
  }
  mark_texture_level_defined(tex, (uint32_t)level, 0);
  update_texture_view(tex);
  tex->complete = true;
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_CopyTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                           GLint x, GLint y, GLsizei width) {
  GLTexture *tex;
  TextureFormatInfo info;
  uint8_t *upload_pixels;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_1D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  tex = get_bound_texture(GL_TEXTURE_1D);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!level_index_valid(level) || xoffset < 0 || width < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!tex->storage_allocated) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if ((uint32_t)level >= tex->gx2_texture.surface.mipLevels) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!texture_level_defined(tex, (uint32_t)level)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!get_texture_format_info(tex->internal_format, GL_RGBA,
                               GL_UNSIGNED_BYTE, false, &info) ||
      !is_copy_color_format_supported(tex->internal_format, &info)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (width == 0) {
    return;
  }
  upload_pixels = build_copy_upload_pixels(x, y, width, 1,
                                           tex->internal_format, &info);
  if (!upload_pixels) {
    return;
  }
  if (!upload_texture_sub_region(tex, level, xoffset, 0, 0, width, 1, 1,
                                 &info, upload_pixels)) {
    gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_CopyTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                           GLint yoffset, GLint zoffset, GLint x, GLint y,
                           GLsizei width, GLsizei height) {
  GLTexture *tex;
  TextureFormatInfo info;
  uint8_t *upload_pixels;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  tex = get_bound_texture(target);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!level_index_valid(level) || xoffset < 0 || yoffset < 0 ||
      zoffset < 0 || width < 0 || height < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!tex->storage_allocated) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if ((uint32_t)level >= tex->gx2_texture.surface.mipLevels) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!texture_level_defined(tex, (uint32_t)level)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!get_texture_format_info(tex->internal_format, GL_RGBA,
                               GL_UNSIGNED_BYTE, false, &info) ||
      !is_copy_color_format_supported(tex->internal_format, &info)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (width == 0 || height == 0) {
    return;
  }

  upload_pixels = build_copy_upload_pixels(x, y, width, height,
                                           tex->internal_format, &info);
  if (!upload_pixels) {
    return;
  }
  if (!upload_texture_sub_region(tex, level, xoffset, yoffset, zoffset, width,
                                 height, 1, &info, upload_pixels)) {
    gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  gl_mem_free(GL_MEM_TYPE_MEM2, upload_pixels);
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void _gl_CompressedTexImage1D(GLenum target, GLint level, GLenum internalformat,
                              GLsizei width, GLint border, GLsizei imageSize,
                              const GLvoid *data) {
  (void)internalformat;
  (void)data;
  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_1D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!level_index_valid(level) || width < 0 || border != 0 ||
      imageSize < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_bound_texture(GL_TEXTURE_1D)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  _gl_set_error(GL_INVALID_ENUM);
}

void _gl_CompressedTexImage3D(GLenum target, GLint level, GLenum internalformat,
                              GLsizei width, GLsizei height, GLsizei depth,
                              GLint border, GLsizei imageSize, const GLvoid *data) {
  (void)internalformat;
  (void)data;
  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!level_index_valid(level) || width < 0 || height < 0 || depth < 0 ||
      border != 0 || imageSize < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_bound_texture(target)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  _gl_set_error(GL_INVALID_ENUM);
}

void _gl_CompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                                 GLsizei width, GLenum format, GLsizei imageSize,
                                 const GLvoid *data) {
  (void)format;
  (void)data;
  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_1D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!level_index_valid(level) || xoffset < 0 || width < 0 ||
      imageSize < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_bound_texture(GL_TEXTURE_1D)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  _gl_set_error(GL_INVALID_ENUM);
}

void _gl_CompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                 GLint yoffset, GLint zoffset, GLsizei width,
                                 GLsizei height, GLsizei depth, GLenum format,
                                 GLsizei imageSize, const GLvoid *data) {
  (void)format;
  (void)data;
  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!level_index_valid(level) || xoffset < 0 || yoffset < 0 ||
      zoffset < 0 || width < 0 || height < 0 || depth < 0 ||
      imageSize < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!get_bound_texture(target)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  _gl_set_error(GL_INVALID_ENUM);
}

void _gl_GetTexLevelParameteriv(GLenum target, GLint level, GLenum pname,
                                GLint *params) {
  GLTexture *tex;
  GLProxyMultisampleImage *proxy = NULL;
  uint32_t face = 0;
  bool defined;
  bool multisample_target = false;
  TextureLevelLayout layout;
  GLint red_bits, green_bits, blue_bits, alpha_bits, depth_bits, stencil_bits;

  if (!g_gl_context || !params) {
    return;
  }
  if (!level_index_valid(level)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  if (target == GL_PROXY_TEXTURE_2D_MULTISAMPLE) {
    proxy = &g_proxy_multisample_2d;
    tex = NULL;
    multisample_target = true;
  } else if (target == GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY) {
    proxy = &g_proxy_multisample_array;
    tex = NULL;
    multisample_target = true;
  } else if (is_multisample_texture_target(target)) {
    tex = get_bound_texture(target);
    multisample_target = true;
  } else if (is_cube_map_face_target(target)) {
    face = cube_map_face_index(target);
    tex = get_bound_texture(GL_TEXTURE_CUBE_MAP);
  } else if (is_valid_texture_target(target)) {
    tex = get_bound_texture(target);
  } else {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!tex && !proxy) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  switch (pname) {
  case GL_TEXTURE_WIDTH:
  case GL_TEXTURE_HEIGHT:
  case GL_TEXTURE_DEPTH:
  case GL_TEXTURE_INTERNAL_FORMAT:
  case GL_TEXTURE_RED_SIZE:
  case GL_TEXTURE_GREEN_SIZE:
  case GL_TEXTURE_BLUE_SIZE:
  case GL_TEXTURE_ALPHA_SIZE:
  case GL_TEXTURE_DEPTH_SIZE:
  case GL_TEXTURE_STENCIL_SIZE:
  case GL_TEXTURE_SHARED_SIZE:
  case GL_TEXTURE_RED_TYPE:
  case GL_TEXTURE_GREEN_TYPE:
  case GL_TEXTURE_BLUE_TYPE:
  case GL_TEXTURE_ALPHA_TYPE:
  case GL_TEXTURE_DEPTH_TYPE:
  case GL_TEXTURE_COMPRESSED:
  case GL_TEXTURE_COMPRESSED_IMAGE_SIZE:
    break;
  case GL_TEXTURE_SAMPLES:
  case GL_TEXTURE_FIXED_SAMPLE_LOCATIONS:
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  if (multisample_target) {
    if (level != 0) {
      *params = 0;
      return;
    }

    GLsizei width = proxy ? proxy->width : tex->width;
    GLsizei height = proxy ? proxy->height : tex->height;
    GLsizei depth = proxy ? proxy->depth : tex->depth;
    GLsizei samples = proxy ? proxy->samples : tex->samples;
    GLint internal_format = proxy ? proxy->internal_format : tex->internal_format;
    GLboolean fixed = proxy ? proxy->fixed_sample_locations
                            : tex->fixed_sample_locations;
    defined = proxy ? proxy->defined : texture_level_defined(tex, 0);
    if (!defined) {
      *params = 0;
      return;
    }

    texture_component_sizes(internal_format, &red_bits, &green_bits,
                            &blue_bits, &alpha_bits, &depth_bits,
                            &stencil_bits);
    switch (pname) {
    case GL_TEXTURE_WIDTH: *params = width; break;
    case GL_TEXTURE_HEIGHT: *params = height; break;
    case GL_TEXTURE_DEPTH: *params = depth; break;
    case GL_TEXTURE_INTERNAL_FORMAT: *params = internal_format; break;
    case GL_TEXTURE_RED_SIZE: *params = red_bits; break;
    case GL_TEXTURE_GREEN_SIZE: *params = green_bits; break;
    case GL_TEXTURE_BLUE_SIZE: *params = blue_bits; break;
    case GL_TEXTURE_ALPHA_SIZE: *params = alpha_bits; break;
    case GL_TEXTURE_DEPTH_SIZE: *params = depth_bits; break;
    case GL_TEXTURE_STENCIL_SIZE: *params = stencil_bits; break;
    case GL_TEXTURE_SHARED_SIZE: *params = 0; break;
    case GL_TEXTURE_RED_TYPE:
    case GL_TEXTURE_GREEN_TYPE:
    case GL_TEXTURE_BLUE_TYPE:
    case GL_TEXTURE_ALPHA_TYPE:
    case GL_TEXTURE_DEPTH_TYPE:
      *params = (GLint)texture_component_type(internal_format);
      break;
    case GL_TEXTURE_COMPRESSED: *params = GL_FALSE; break;
    case GL_TEXTURE_SAMPLES: *params = samples; break;
    case GL_TEXTURE_FIXED_SAMPLE_LOCATIONS: *params = fixed; break;
    case GL_TEXTURE_COMPRESSED_IMAGE_SIZE:
      _gl_set_error(GL_INVALID_OPERATION);
      break;
    default: break;
    }
    return;
  }

  defined = is_cube_map_face_target(target)
                ? texture_face_level_defined(tex, (uint32_t)level, face)
                : texture_level_defined(tex, (uint32_t)level);
  if (!tex->storage_allocated || !defined ||
      (uint32_t)level >= tex->gx2_texture.surface.mipLevels ||
      !calc_level_layout(tex->target, tex->gx2_texture.surface.format,
                         tex->width, tex->height, tex->depth,
                         (uint32_t)level, &layout)) {
    *params = 0;
    return;
  }

  texture_component_sizes(tex->internal_format, &red_bits, &green_bits,
                          &blue_bits, &alpha_bits, &depth_bits,
                          &stencil_bits);

  switch (pname) {
  case GL_TEXTURE_WIDTH:
    *params = (GLint)layout.width;
    break;
  case GL_TEXTURE_HEIGHT:
    *params = tex->target == GL_TEXTURE_1D
                  ? 1
                  : (tex->target == GL_TEXTURE_1D_ARRAY
                         ? (GLint)layout.depth
                         : (GLint)layout.height);
    break;
  case GL_TEXTURE_DEPTH:
    *params = (tex->target == GL_TEXTURE_3D ||
               tex->target == GL_TEXTURE_2D_ARRAY)
                  ? (GLint)layout.depth
                  : 1;
    break;
  case GL_TEXTURE_INTERNAL_FORMAT:
    *params = tex->internal_format;
    break;
  case GL_TEXTURE_RED_SIZE:
    *params = red_bits;
    break;
  case GL_TEXTURE_GREEN_SIZE:
    *params = green_bits;
    break;
  case GL_TEXTURE_BLUE_SIZE:
    *params = blue_bits;
    break;
  case GL_TEXTURE_ALPHA_SIZE:
    *params = alpha_bits;
    break;
  case GL_TEXTURE_DEPTH_SIZE:
    *params = depth_bits;
    break;
  case GL_TEXTURE_STENCIL_SIZE:
    *params = stencil_bits;
    break;
  case GL_TEXTURE_SHARED_SIZE:
    *params = 0;
    break;
  case GL_TEXTURE_COMPRESSED_IMAGE_SIZE: {
    TextureFormatInfo info;
    GLsizei image_size = 0;
    if (!get_compressed_format_info((GLenum)tex->internal_format, &info)) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }
    if (!compressed_image_size(layout.width, layout.height, info.block_bytes,
                               &image_size)) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }
    *params = image_size;
    break;
  }
  case GL_TEXTURE_RED_TYPE:
  case GL_TEXTURE_GREEN_TYPE:
  case GL_TEXTURE_BLUE_TYPE:
  case GL_TEXTURE_ALPHA_TYPE:
  case GL_TEXTURE_DEPTH_TYPE:
    *params = (GLint)texture_component_type(tex->internal_format);
    break;
  case GL_TEXTURE_COMPRESSED:
    {
      TextureFormatInfo info;
      *params = get_compressed_format_info((GLenum)tex->internal_format,
                                           &info)
                    ? GL_TRUE
                    : GL_FALSE;
    }
    break;
  case GL_TEXTURE_SAMPLES:
    *params = 0;
    break;
  case GL_TEXTURE_FIXED_SAMPLE_LOCATIONS:
    *params = GL_TRUE;
    break;
  default:
    break;
  }
}

void _gl_GetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params) {
    GLint iparams;
    _gl_GetTexLevelParameteriv(target, level, pname, &iparams);
    if (params) *params = (GLfloat)iparams;
}

void _gl_GetTexImage(GLenum target, GLint level, GLenum format, GLenum type,
                     GLvoid *pixels) {
  GLTexture *tex;
  TextureFormatInfo info;
  TextureLevelLayout layout;
  uint8_t *src_level;
  uint8_t *dst_base;
  GLvoid *resolved_pixels = pixels;
  GLintptr pack_buffer_offset = 0;
  GLsizeiptr pack_byte_count = 0;
  uint32_t dst_row_bytes;
  uint32_t dst_image_bytes;
  uint32_t src_row_bytes;
  uint32_t start_slice = 0;
  uint32_t slice_count = 1;
  GLsizei read_width;
  GLsizei read_height;
  GLsizei read_depth;
  size_t dst_base_offset;
  GLuint texture_name = 0;

  if (!g_gl_context) {
    return;
  }
  if (!pixels && g_gl_context->bound_pixel_pack_buffer == 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  if (target == GL_TEXTURE_1D || target == GL_TEXTURE_2D ||
      target == GL_TEXTURE_3D || target == GL_TEXTURE_1D_ARRAY ||
      target == GL_TEXTURE_2D_ARRAY) {
    texture_name = get_bound_tex(target);
    tex = get_bound_texture(target);
  } else if (is_cube_map_face_target(target)) {
    texture_name = get_bound_tex(GL_TEXTURE_CUBE_MAP);
    tex = get_bound_texture(GL_TEXTURE_CUBE_MAP);
    start_slice = cube_map_face_index(target);
  } else {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  if (!level_index_valid(level)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!tex->storage_allocated) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if ((uint32_t)level >= tex->gx2_texture.surface.mipLevels) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!(is_cube_map_face_target(target)
            ? texture_face_level_defined(tex, (uint32_t)level, start_slice)
            : texture_level_defined(tex, (uint32_t)level))) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!get_texture_format_info(tex->internal_format, format, type, true,
                               &info)) {
    /* gx2gl currently supports readback only for the texture's native upload
     * format/type pair, not arbitrary GL conversion targets. */
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!calc_level_layout(tex->target, tex->gx2_texture.surface.format,
                         tex->width, tex->height, tex->depth, (uint32_t)level,
                         &layout)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  read_width = layout.width;
  read_height = target == GL_TEXTURE_1D_ARRAY ? layout.depth : layout.height;
  read_depth = (target == GL_TEXTURE_3D || target == GL_TEXTURE_2D_ARRAY)
                   ? layout.depth
                   : 1;
  if (!get_pack_layout(&info, read_width, read_height, &dst_row_bytes,
                       &dst_image_bytes, &dst_base_offset)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!resolve_pack_pixels(&info, read_width, read_height, read_depth,
                           pixels, &resolved_pixels, &pack_buffer_offset,
                           &pack_byte_count)) {
    return;
  }

  if (target == GL_TEXTURE_3D || target == GL_TEXTURE_2D_ARRAY ||
      target == GL_TEXTURE_1D_ARRAY) {
    slice_count = (uint32_t)layout.depth;
  } else if (is_cube_map_face_target(target)) {
    if (start_slice >= (uint32_t)layout.depth) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }
    slice_count = 1;
  }

  src_level = get_texture_level_ptr(tex, (uint32_t)level);
  if (!src_level) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  if (texture_name != 0) {
    gl_framebuffer_sync_texture_for_sampling(texture_name);
  }
  GX2DrawDone();
  DCInvalidateRange(src_level, layout.image_size);

  dst_base = (uint8_t *)resolved_pixels + dst_base_offset;
  src_row_bytes = layout.pitch * info.dst_bytes_per_texel;
  for (uint32_t z = 0; z < slice_count; ++z) {
    const uint8_t *src_slice =
        src_level + (start_slice + z) * layout.slice_size;
    if (target == GL_TEXTURE_1D_ARRAY) {
      read_texture_row(dst_base + (size_t)z * dst_row_bytes,
                       src_slice, (uint32_t)layout.width, &info);
    } else {
      uint8_t *dst_slice = dst_base + (size_t)z * dst_image_bytes;
      for (GLsizei y = 0; y < layout.height; ++y) {
        read_texture_row(dst_slice + (size_t)y * dst_row_bytes,
                         src_slice + (size_t)y * src_row_bytes,
                         (uint32_t)layout.width, &info);
      }
    }
  }

  if (g_gl_context->bound_pixel_pack_buffer != 0 &&
      gl_buffer_flush_range(g_gl_context->bound_pixel_pack_buffer,
                            pack_buffer_offset, pack_byte_count) != GL_TRUE) {
    return;
  }
}

void _gl_GetCompressedTexImage(GLenum target, GLint level, GLvoid *pixels) {
  GLTexture *tex;
  TextureFormatInfo info;
  TextureLevelLayout layout;
  GLvoid *resolved_pixels = pixels;
  GLintptr pack_buffer_offset = 0;
  GLsizei image_size;
  uint32_t face_index = 0;
  GLuint texture_name;

  if (!g_gl_context) {
    return;
  }
  if (target == GL_TEXTURE_1D || target == GL_TEXTURE_2D ||
      target == GL_TEXTURE_3D) {
    texture_name = get_bound_tex(target);
    tex = get_bound_texture(target);
  } else if (is_cube_map_face_target(target)) {
    texture_name = get_bound_tex(GL_TEXTURE_CUBE_MAP);
    tex = get_bound_texture(GL_TEXTURE_CUBE_MAP);
    face_index = cube_map_face_index(target);
  } else {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (!level_index_valid(level)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!tex || !tex->storage_allocated ||
      (uint32_t)level >= tex->gx2_texture.surface.mipLevels ||
      !(is_cube_map_face_target(target)
            ? texture_face_level_defined(tex, (uint32_t)level, face_index)
            : texture_level_defined(tex, (uint32_t)level)) ||
      !get_compressed_format_info((GLenum)tex->internal_format, &info)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!calc_level_layout(tex->target, tex->gx2_texture.surface.format,
                         tex->width, tex->height, tex->depth,
                         (uint32_t)level, &layout) ||
      !compressed_image_size(layout.width, layout.height, info.block_bytes,
                             &image_size)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (image_size > 0 && !pixels &&
      g_gl_context->bound_pixel_pack_buffer == 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!resolve_compressed_pack_data(pixels, image_size, &resolved_pixels,
                                    &pack_buffer_offset)) {
    return;
  }

  if (texture_name != 0) {
    gl_framebuffer_sync_texture_for_sampling(texture_name);
  }
  GX2DrawDone();
  uint8_t *level_ptr = get_texture_level_ptr(tex, (uint32_t)level);
  if (!level_ptr) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  DCInvalidateRange(level_ptr, layout.image_size);
  if (image_size > 0 &&
      !read_compressed_texture_image(tex, level, (GLint)face_index, &info,
                                     resolved_pixels)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (g_gl_context->bound_pixel_pack_buffer != 0 &&
      gl_buffer_flush_range(g_gl_context->bound_pixel_pack_buffer,
                            pack_buffer_offset, image_size) != GL_TRUE) {
    return;
  }
}

void _gl_GenerateMipmap(GLenum target) {
  GLTexture *tex;
  TextureFormatInfo info;
  uint32_t mip_count;
  GLuint texture_name;

  if (!g_gl_context) {
    return;
  }
  if (target != GL_TEXTURE_1D && target != GL_TEXTURE_2D &&
      target != GL_TEXTURE_3D && target != GL_TEXTURE_CUBE_MAP &&
      target != GL_TEXTURE_1D_ARRAY && target != GL_TEXTURE_2D_ARRAY) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  texture_name = get_bound_tex(target);
  tex = get_bound_texture(target);
  if (!tex) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  if (!tex->storage_allocated || !texture_level_defined(tex, 0) ||
      !get_texture_format_info(tex->internal_format, GL_RGBA,
                               GL_UNSIGNED_BYTE, false, &info) ||
      !info.mipmap_supported) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  mip_count = calc_full_mip_count(
      tex->width,
      target == GL_TEXTURE_1D_ARRAY ? 1 : tex->height,
      texture_depth_is_spatial(target) ? tex->depth : 1);
  if (mip_count <= 1) {
    return;
  }

  if (tex->gx2_texture.surface.mipLevels != mip_count &&
      !rebuild_texture_storage(tex, tex->width, tex->height, tex->depth,
                               tex->internal_format, mip_count, true)) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return;
  }

  for (uint32_t level = 1; level < mip_count; ++level) {
    if (!generate_mipmap_level(tex, level, &info)) {
      _gl_set_error(GL_INVALID_OPERATION);
      return;
    }
    if (target == GL_TEXTURE_CUBE_MAP) {
      mark_cube_level_complete(tex, level);
    } else {
      mark_texture_level_defined(tex, level, 0);
    }
  }

  update_texture_view(tex);
  tex->complete = true;
  if (texture_name != 0) {
    gl_framebuffer_mark_texture_dirty(texture_name);
  }
  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
}

void gl_bind_textures(void) {
  gl_bind_program_textures();
}

GX2Texture *gl_get_gx2_texture(GLuint id) {
  if (id > 0 && id < MAX_TEXTURES && g_textures[id].in_use &&
      g_textures[id].complete) {
    return &g_textures[id].gx2_texture;
  }
  return NULL;
}

GLint gl_get_texture_internal_format(GLuint id) {
  if (id > 0 && id < MAX_TEXTURES && g_textures[id].in_use &&
      g_textures[id].complete) {
    return g_textures[id].internal_format;
  }
  return 0;
}

GLenum gl_get_texture_target(GLuint id) {
  if (id > 0 && id < MAX_TEXTURES && g_textures[id].in_use) {
    return g_textures[id].target;
  }
  return 0;
}

GLsizei gl_get_texture_samples(GLuint id) {
  if (id > 0 && id < MAX_TEXTURES && g_textures[id].in_use) {
    return g_textures[id].samples;
  }
  return 0;
}

GLboolean gl_get_texture_fixed_sample_locations(GLuint id) {
  if (id > 0 && id < MAX_TEXTURES && g_textures[id].in_use) {
    return g_textures[id].fixed_sample_locations;
  }
  return GL_TRUE;
}


GX2Sampler *gl_get_gx2_sampler(GLuint id, bool use_sampler_obj) {
  if (use_sampler_obj) {
    if (id > 0 && id < MAX_SAMPLER_OBJECTS && g_samplers[id].in_use)
      return &g_samplers[id].gx2_sampler;
    return NULL;
  }
  if (id > 0 && id < MAX_TEXTURES && g_textures[id].in_use && g_textures[id].complete)
    return &g_textures[id].gx2_sampler;
  return NULL;
}

GX2Sampler *gl_get_effective_gx2_sampler(GLuint unit, GLuint texture) {
  GLuint sampler_id;

  if (!g_gl_context || unit >= 32) {
    return NULL;
  }

  sampler_id = g_gl_context->bound_sampler[unit];
  if (sampler_id > 0 && sampler_id < MAX_SAMPLER_OBJECTS &&
      g_samplers[sampler_id].in_use) {
    return &g_samplers[sampler_id].gx2_sampler;
  }

  return gl_get_gx2_sampler(texture, false);
}

#ifdef __cplusplus
}
#endif
