#include "gl_shader.h"
#include "gl_buffer.h"
#include "gl_framebuffer.h"
#include "gx2gl_cafeglsl.h"
#include "gx2gl_shader_compat.h"
#include "gl_vao.h"
#include "endian/endian.h"
#include "mem/gl_mem.h"
#include "gl_texture.h"
#include "Platform/WiiU_Log.hpp"

#ifdef __cplusplus
extern "C" {
#endif
#include <coreinit/cache.h>
#include <gfd.h>
#include <gx2/draw.h>
#include <gx2/mem.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/sampler.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <gx2r/draw.h>
#include <stdio.h>
#include <string.h>
#include <whb/gfx.h>
#include <stdlib.h>

#define MAX_PROGRAMS 512
#define MAX_SHADERS 512
#define MAX_PROGRAM_SAMPLERS GL33_MAX_COMBINED_TEXTURE_IMAGE_UNITS

#define GL_LOCATION_STAGE_PIXEL 0x80000000u
#define GL_LOCATION_KIND_SAMPLER 0x40000000u
#define GL_LOCATION_KIND_DIRECT 0x20000000u
#define GL_LOCATION_BLOCK_MASK 0x3FFFu
#define GX2GL_INFO_LOG_CAPACITY 4096
#define GX2GL_CAFEGLSL_VIRTUAL_UNIFORM_BLOCK_LOCATION 15u
#define GX2GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS 64u
#define GX2GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS 4u

#ifndef GL_UNIFORM_BLOCK_BINDING
#define GL_UNIFORM_BLOCK_BINDING 0x8A3F
#endif
#ifndef GL_UNIFORM_BLOCK_DATA_SIZE
#define GL_UNIFORM_BLOCK_DATA_SIZE 0x8A40
#endif
#ifndef GL_UNIFORM_BLOCK_NAME_LENGTH
#define GL_UNIFORM_BLOCK_NAME_LENGTH 0x8A41
#endif
#ifndef GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER
#define GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER 0x8A44
#endif
#ifndef GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER
#define GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER 0x8A46
#endif
#ifndef GL_UNIFORM_TYPE
#define GL_UNIFORM_TYPE 0x8A37
#endif
#ifndef GL_UNIFORM_SIZE
#define GL_UNIFORM_SIZE 0x8A38
#endif
#ifndef GL_UNIFORM_NAME_LENGTH
#define GL_UNIFORM_NAME_LENGTH 0x8A39
#endif
#ifndef GL_UNIFORM_BLOCK_INDEX
#define GL_UNIFORM_BLOCK_INDEX 0x8A3A
#endif
#ifndef GL_UNIFORM_OFFSET
#define GL_UNIFORM_OFFSET 0x8A3B
#endif
#ifndef GL_UNSIGNED_INT_VEC2
#define GL_UNSIGNED_INT_VEC2 0x8DC6
#endif
#ifndef GL_UNSIGNED_INT_VEC3
#define GL_UNSIGNED_INT_VEC3 0x8DC7
#endif
#ifndef GL_UNSIGNED_INT_VEC4
#define GL_UNSIGNED_INT_VEC4 0x8DC8
#endif
#ifndef GL_FLOAT_MAT2x3
#define GL_FLOAT_MAT2x3 0x8B65
#endif
#ifndef GL_FLOAT_MAT2x4
#define GL_FLOAT_MAT2x4 0x8B66
#endif
#ifndef GL_FLOAT_MAT3x2
#define GL_FLOAT_MAT3x2 0x8B67
#endif
#ifndef GL_FLOAT_MAT3x4
#define GL_FLOAT_MAT3x4 0x8B68
#endif
#ifndef GL_FLOAT_MAT4x2
#define GL_FLOAT_MAT4x2 0x8B69
#endif
#ifndef GL_FLOAT_MAT4x3
#define GL_FLOAT_MAT4x3 0x8B6A
#endif
#ifndef GL_SAMPLER_1D
#define GL_SAMPLER_1D 0x8B5D
#endif
#ifndef GL_SAMPLER_3D
#define GL_SAMPLER_3D 0x8B5F
#endif

typedef struct GLCompiledShader {
  GLenum type;
  uint32_t reference_count;
  char *source;
  GX2VertexShader *vertex_shader;
  GX2PixelShader *pixel_shader;
} GLCompiledShader;

typedef struct {
  bool in_use;
  bool delete_pending;
  GLenum type;
  char *source;
  char *info_log;
  uint32_t source_length;
  bool source_present;
  bool compile_requested;
  bool compile_succeeded;
  GLCompiledShader *compiled_binary;
  GX2VertexShader *compiled_vertex_shader;
  GX2PixelShader *compiled_pixel_shader;
} GLShader;

typedef struct {
  uint32_t size;
  void *buffer;
} UniformBlockShadow;

typedef struct {
  char *name;
  int32_t vertex_block_index;
  int32_t pixel_block_index;
  uint32_t vertex_location;
  uint32_t pixel_location;
  uint32_t vertex_size;
  uint32_t pixel_size;
  GLuint binding_point;
  bool binding_initialized;
  bool implicit;
} ProgramUniformBlock;

typedef struct {
  bool active;
  char *name;
  GLenum base_type;
  uint32_t component_count;
} ProgramAttribReflection;

typedef struct {
  GLint location;
  uint32_t word_count;
  uint32_t *words;
} ProgramUniformValueShadow;

typedef struct {
  char *name;
  GLint size;
  GLenum type;
  uint32_t component_count;
} ProgramTransformFeedbackVarying;

typedef struct {
  bool in_use;
  bool linked;
  bool link_attempted;
  bool validated;
  bool delete_pending;
  GLuint attached_vertex_shader;
  GLuint attached_pixel_shader;
  GLuint attached_geometry_shader;
  char *info_log;
  const WHBGfxShaderGroup *group;
  bool owns_group;
  bool owns_whb_group;
  WHBGfxShaderGroup owned_group;
  GLCompiledShader *linked_vertex_binary;
  GLCompiledShader *linked_pixel_binary;
  UniformBlockShadow *vs_blocks;
  uint32_t vs_block_count;
  UniformBlockShadow *ps_blocks;
  uint32_t ps_block_count;
  ProgramUniformBlock *uniform_blocks;
  uint32_t uniform_block_count;
  GLint vertex_sampler_units[MAX_PROGRAM_SAMPLERS];
  GLint pixel_sampler_units[MAX_PROGRAM_SAMPLERS];
  GLint vertex_sampler_bindings[MAX_PROGRAM_SAMPLERS];
  GLint pixel_sampler_bindings[MAX_PROGRAM_SAMPLERS];
  GLenum vertex_sampler_targets[MAX_PROGRAM_SAMPLERS];
  GLenum pixel_sampler_targets[MAX_PROGRAM_SAMPLERS];
  uint8_t *vs_direct_uniform_shadow;
  uint32_t vs_direct_uniform_shadow_size;
  uint8_t *ps_direct_uniform_shadow;
  uint32_t ps_direct_uniform_shadow_size;
  ProgramUniformValueShadow *uniform_value_shadows;
  uint32_t uniform_value_shadow_count;
  char **tf_pending_varyings;
  uint32_t tf_pending_count;
  GLenum tf_pending_buffer_mode;
  ProgramTransformFeedbackVarying *tf_linked_varyings;
  uint32_t tf_linked_count;
  GLenum tf_linked_buffer_mode;
  GLsizei tf_linked_max_name_length;
  GLuint tf_required_binding_count;
  bool tf_stream_out_ready;
  ProgramAttribReflection attrib_reflection[GL33_MAX_VERTEX_ATTRIBS];
  GX2FetchShader dynamic_fetch_shader;
  void *dynamic_fetch_shader_program;
  uint32_t dynamic_fetch_shader_program_size;
  uint32_t dynamic_fetch_shader_attrib_count;
  char *attrib_binding_names[GL33_MAX_VERTEX_ATTRIBS];
} GLProgram;

static GLShader g_shaders[MAX_SHADERS];
static GLProgram g_programs[MAX_PROGRAMS];
static bool g_shader_mode_valid = false;
static GX2ShaderMode g_shader_mode = GX2_SHADER_MODE_UNIFORM_REGISTER;

static bool is_valid_shader(GLuint s) { return s > 0 && s < MAX_SHADERS && g_shaders[s].in_use; }
static bool is_valid_program(GLuint p) { return p > 0 && p < MAX_PROGRAMS && g_programs[p].in_use; }
static GLint make_sampler_location(bool pixel_stage, uint32_t location);
static GLint make_direct_uniform_location(bool pixel_stage, uint32_t offset);
static bool location_is_sampler(GLint l);
static bool location_is_pixel(GLint l);
static bool location_is_direct(GLint l);
static bool stage_uses_virtual_uniform_block(const GLProgram *prog,
                                             bool pixel_stage);
static void free_program_uniform_blocks(GLProgram *prog);
static void free_program_attrib_reflection(GLProgram *prog);
static void free_program_transform_feedback_pending(GLProgram *prog);
static void free_program_transform_feedback_linked(GLProgram *prog);
static bool is_cafeglsl_implicit_uniform_block(const GX2UniformBlock *block);

static void invalidate_shader_program_memory(void *program, uint32_t size) {
  if (!program || size == 0) return;
  DCFlushRange(program, size);
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, program, size);
}

static void replace_owned_string(char **dst, const char *src) {
  size_t length;
  char *copy = NULL;

  if (*dst) {
    free(*dst);
    *dst = NULL;
  }
  if (!src) {
    return;
  }

  length = strlen(src);
  copy = (char *)malloc(length + 1u);
  if (!copy) {
    return;
  }

  memcpy(copy, src, length + 1u);
  *dst = copy;
}

static void copy_gl_string(const char *src, GLsizei max_length, GLsizei *out_length, GLchar *dst) {
  GLsizei written_length = 0;

  if (src && dst && max_length > 0) {
    written_length = (GLsizei)strlen(src);
    if (written_length >= max_length) {
      written_length = max_length - 1;
    }
    memcpy(dst, src, (size_t)written_length);
    dst[written_length] = '\0';
  } else if (dst && max_length > 0) {
    dst[0] = '\0';
  }

  if (out_length) {
    *out_length = written_length;
  }
}

static GLenum shader_var_type_to_gl_enum(GX2ShaderVarType type) {
  switch (type) {
  case GX2_SHADER_VAR_TYPE_BOOL: return GL_BOOL;
  case GX2_SHADER_VAR_TYPE_BOOL2: return GL_BOOL_VEC2;
  case GX2_SHADER_VAR_TYPE_BOOL3: return GL_BOOL_VEC3;
  case GX2_SHADER_VAR_TYPE_BOOL4: return GL_BOOL_VEC4;
  case GX2_SHADER_VAR_TYPE_INT: return GL_INT;
  case GX2_SHADER_VAR_TYPE_INT2: return GL_INT_VEC2;
  case GX2_SHADER_VAR_TYPE_INT3: return GL_INT_VEC3;
  case GX2_SHADER_VAR_TYPE_INT4: return GL_INT_VEC4;
  case GX2_SHADER_VAR_TYPE_UINT: return GL_UNSIGNED_INT;
  case GX2_SHADER_VAR_TYPE_UINT2: return GL_UNSIGNED_INT_VEC2;
  case GX2_SHADER_VAR_TYPE_UINT3: return GL_UNSIGNED_INT_VEC3;
  case GX2_SHADER_VAR_TYPE_UINT4: return GL_UNSIGNED_INT_VEC4;
  case GX2_SHADER_VAR_TYPE_FLOAT: return GL_FLOAT;
  case GX2_SHADER_VAR_TYPE_FLOAT2: return GL_FLOAT_VEC2;
  case GX2_SHADER_VAR_TYPE_FLOAT3: return GL_FLOAT_VEC3;
  case GX2_SHADER_VAR_TYPE_FLOAT4: return GL_FLOAT_VEC4;
  case GX2_SHADER_VAR_TYPE_FLOAT2X2: return GL_FLOAT_MAT2;
  case GX2_SHADER_VAR_TYPE_FLOAT2X3: return GL_FLOAT_MAT2x3;
  case GX2_SHADER_VAR_TYPE_FLOAT2X4: return GL_FLOAT_MAT2x4;
  case GX2_SHADER_VAR_TYPE_FLOAT3X2: return GL_FLOAT_MAT3x2;
  case GX2_SHADER_VAR_TYPE_FLOAT3X3: return GL_FLOAT_MAT3;
  case GX2_SHADER_VAR_TYPE_FLOAT3X4: return GL_FLOAT_MAT3x4;
  case GX2_SHADER_VAR_TYPE_FLOAT4X2: return GL_FLOAT_MAT4x2;
  case GX2_SHADER_VAR_TYPE_FLOAT4X3: return GL_FLOAT_MAT4x3;
  case GX2_SHADER_VAR_TYPE_FLOAT4X4: return GL_FLOAT_MAT4;
  default: return 0;
  }
}

static GLenum sampler_var_type_to_gl_enum(GX2SamplerVarType type) {
  switch (type) {
  case GX2_SAMPLER_VAR_TYPE_SAMPLER_1D: return GL_SAMPLER_1D;
  case GX2_SAMPLER_VAR_TYPE_SAMPLER_2D: return GL_SAMPLER_2D;
  case GX2_SAMPLER_VAR_TYPE_SAMPLER_3D: return GL_SAMPLER_3D;
  case GX2_SAMPLER_VAR_TYPE_SAMPLER_CUBE: return GL_SAMPLER_CUBE;
  case (GX2SamplerVarType)17: return GL_SAMPLER_2D_MULTISAMPLE;
  case (GX2SamplerVarType)18: return GL_SAMPLER_2D_MULTISAMPLE_ARRAY;
  case (GX2SamplerVarType)30: return GL_INT_SAMPLER_2D_MULTISAMPLE;
  case (GX2SamplerVarType)31: return GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY;
  case (GX2SamplerVarType)43: return GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE;
  case (GX2SamplerVarType)44: return GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY;
  default: return 0;
  }
}

static GLenum sampler_var_type_to_texture_target(GX2SamplerVarType type) {
  switch ((uint32_t)type) {
  case 0: return GL_TEXTURE_1D;
  case 1: return GL_TEXTURE_2D;
  case 3: return GL_TEXTURE_3D;
  case 4: return GL_TEXTURE_CUBE_MAP;
  case 17:
  case 30:
  case 43:
    return GL_TEXTURE_2D_MULTISAMPLE;
  case 18:
  case 31:
  case 44:
    return GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
  default:
    return GL_TEXTURE_2D;
  }
}

static uint32_t shader_var_type_word_count(GX2ShaderVarType type) {
  switch (type) {
  case GX2_SHADER_VAR_TYPE_BOOL:
  case GX2_SHADER_VAR_TYPE_INT:
  case GX2_SHADER_VAR_TYPE_UINT:
  case GX2_SHADER_VAR_TYPE_FLOAT:
    return 1;
  case GX2_SHADER_VAR_TYPE_BOOL2:
  case GX2_SHADER_VAR_TYPE_INT2:
  case GX2_SHADER_VAR_TYPE_UINT2:
  case GX2_SHADER_VAR_TYPE_FLOAT2:
    return 2;
  case GX2_SHADER_VAR_TYPE_BOOL3:
  case GX2_SHADER_VAR_TYPE_INT3:
  case GX2_SHADER_VAR_TYPE_UINT3:
  case GX2_SHADER_VAR_TYPE_FLOAT3:
    return 3;
  case GX2_SHADER_VAR_TYPE_BOOL4:
  case GX2_SHADER_VAR_TYPE_INT4:
  case GX2_SHADER_VAR_TYPE_UINT4:
  case GX2_SHADER_VAR_TYPE_FLOAT4:
  case GX2_SHADER_VAR_TYPE_FLOAT2X2:
    return 4;
  case GX2_SHADER_VAR_TYPE_FLOAT2X3:
  case GX2_SHADER_VAR_TYPE_FLOAT3X2:
    return 6;
  case GX2_SHADER_VAR_TYPE_FLOAT2X4:
  case GX2_SHADER_VAR_TYPE_FLOAT4X2:
    return 8;
  case GX2_SHADER_VAR_TYPE_FLOAT3X3:
    return 9;
  case GX2_SHADER_VAR_TYPE_FLOAT3X4:
  case GX2_SHADER_VAR_TYPE_FLOAT4X3:
    return 12;
  case GX2_SHADER_VAR_TYPE_FLOAT4X4:
    return 16;
  default:
    return 0;
  }
}

static uint32_t shader_var_element_count(uint32_t count) {
  return count > 0 ? count : 1u;
}

typedef struct {
  const char *name;
  GLint size;
  GLenum type;
  GLint block_index;
  GLint offset;
  GLint location;
  bool is_sampler;
} ActiveUniformInfo;

static uint32_t get_active_attribute_count(GLProgram *prog) {
  if (!prog || !prog->group || !prog->group->vertexShader) return 0;
  return prog->group->vertexShader->attribVarCount;
}

static GLsizei get_active_attribute_max_name_length(GLProgram *prog) {
  GLsizei max_length = 0;
  GX2VertexShader *vs;

  if (!prog || !prog->group || !prog->group->vertexShader) return 0;
  vs = prog->group->vertexShader;
  for (uint32_t i = 0; i < vs->attribVarCount; ++i) {
    GLsizei length = vs->attribVars[i].name ? (GLsizei)strlen(vs->attribVars[i].name) + 1 : 0;
    if (length > max_length) max_length = length;
  }
  return max_length;
}

static uint32_t get_active_uniform_count(GLProgram *prog) {
  GX2VertexShader *vs;
  GX2PixelShader *ps;
  uint32_t count = 0;

  if (!prog || !prog->group) return 0;
  vs = prog->group->vertexShader;
  ps = prog->group->pixelShader;

  if (vs) count += vs->uniformVarCount + vs->samplerVarCount;
  if (ps) count += ps->uniformVarCount + ps->samplerVarCount;
  return count;
}

static GLsizei get_active_uniform_max_name_length(GLProgram *prog) {
  GX2VertexShader *vs;
  GX2PixelShader *ps;
  GLsizei max_length = 0;

  if (!prog || !prog->group) return 0;
  vs = prog->group->vertexShader;
  ps = prog->group->pixelShader;

  if (vs) {
    for (uint32_t i = 0; i < vs->uniformVarCount; ++i) {
      GLsizei length = vs->uniformVars[i].name ? (GLsizei)strlen(vs->uniformVars[i].name) + 1 : 0;
      if (length > max_length) max_length = length;
    }
    for (uint32_t i = 0; i < vs->samplerVarCount; ++i) {
      GLsizei length = vs->samplerVars[i].name ? (GLsizei)strlen(vs->samplerVars[i].name) + 1 : 0;
      if (length > max_length) max_length = length;
    }
  }
  if (ps) {
    for (uint32_t i = 0; i < ps->uniformVarCount; ++i) {
      GLsizei length = ps->uniformVars[i].name ? (GLsizei)strlen(ps->uniformVars[i].name) + 1 : 0;
      if (length > max_length) max_length = length;
    }
    for (uint32_t i = 0; i < ps->samplerVarCount; ++i) {
      GLsizei length = ps->samplerVars[i].name ? (GLsizei)strlen(ps->samplerVars[i].name) + 1 : 0;
      if (length > max_length) max_length = length;
    }
  }

  return max_length;
}

static bool get_active_uniform_info(GLProgram *prog, GLuint index, ActiveUniformInfo *info) {
  GX2VertexShader *vs;
  GX2PixelShader *ps;
  uint32_t cursor = 0;

  if (!prog || !prog->group || !info) return false;
  memset(info, 0, sizeof(*info));
  info->block_index = -1;
  info->offset = -1;

  vs = prog->group->vertexShader;
  ps = prog->group->pixelShader;

  if (vs) {
    for (uint32_t i = 0; i < vs->uniformVarCount; ++i, ++cursor) {
      if (cursor != index) continue;
      info->name = vs->uniformVars[i].name;
      info->size = (GLint)shader_var_element_count(vs->uniformVars[i].count);
      info->type = shader_var_type_to_gl_enum(vs->uniformVars[i].type);
      info->block_index =
          (vs->uniformVars[i].block >= 0 &&
           (uint32_t)vs->uniformVars[i].block < vs->uniformBlockCount &&
           is_cafeglsl_implicit_uniform_block(&vs->uniformBlocks[vs->uniformVars[i].block]))
              ? -1
              : vs->uniformVars[i].block;
      info->offset = (GLint)vs->uniformVars[i].offset;
      info->location = vs->uniformVars[i].block < 0
                           ? make_direct_uniform_location(false, (uint32_t)vs->uniformVars[i].offset)
                           : (GLint)(((uint32_t)vs->uniformVars[i].block << 16) |
                                     ((uint32_t)vs->uniformVars[i].offset & 0xFFFFu));
      return info->type != 0;
    }
  }
  if (ps) {
    for (uint32_t i = 0; i < ps->uniformVarCount; ++i, ++cursor) {
      if (cursor != index) continue;
      info->name = ps->uniformVars[i].name;
      info->size = (GLint)shader_var_element_count(ps->uniformVars[i].count);
      info->type = shader_var_type_to_gl_enum(ps->uniformVars[i].type);
      info->block_index =
          (ps->uniformVars[i].block >= 0 &&
           (uint32_t)ps->uniformVars[i].block < ps->uniformBlockCount &&
           is_cafeglsl_implicit_uniform_block(&ps->uniformBlocks[ps->uniformVars[i].block]))
              ? -1
              : ps->uniformVars[i].block;
      info->offset = (GLint)ps->uniformVars[i].offset;
      info->location = ps->uniformVars[i].block < 0
                           ? make_direct_uniform_location(true, (uint32_t)ps->uniformVars[i].offset)
                           : (GLint)(GL_LOCATION_STAGE_PIXEL |
                                     ((uint32_t)ps->uniformVars[i].block << 16) |
                                     ((uint32_t)ps->uniformVars[i].offset & 0xFFFFu));
      return info->type != 0;
    }
  }
  if (vs) {
    for (uint32_t i = 0; i < vs->samplerVarCount; ++i, ++cursor) {
      if (cursor != index) continue;
      info->name = vs->samplerVars[i].name;
      info->size = 1;
      info->type = sampler_var_type_to_gl_enum(vs->samplerVars[i].type);
      info->location = make_sampler_location(false, (uint32_t)vs->samplerVars[i].location);
      info->is_sampler = true;
      return info->type != 0;
    }
  }
  if (ps) {
    for (uint32_t i = 0; i < ps->samplerVarCount; ++i, ++cursor) {
      if (cursor != index) continue;
      info->name = ps->samplerVars[i].name;
      info->size = 1;
      info->type = sampler_var_type_to_gl_enum(ps->samplerVars[i].type);
      info->location = make_sampler_location(true, (uint32_t)ps->samplerVars[i].location);
      info->is_sampler = true;
      return info->type != 0;
    }
  }

  return false;
}

static GLint resolve_uniform_location(GLProgram *prog, GLint location) {
  uint32_t count;
  ActiveUniformInfo info;

  if (!prog || location <= 0) {
    return location;
  }

  count = get_active_uniform_count(prog);
  if ((uint32_t)location > count) {
    return location;
  }

  if (!get_active_uniform_info(prog, (GLuint)(location - 1), &info)) {
    return location;
  }
  return info.location;
}

static GLint get_sampler_uniform_binding(GLProgram *prog, GLint location) {
  uint32_t location_index;
  bool pixel_stage;
  const GLint *units;
  const GLint *bindings;
  GLint sampler_slot;

  if (!prog || !location_is_sampler(location)) return 0;
  location_index = (uint32_t)location & 0xFFFFu;
  pixel_stage = location_is_pixel(location);
  units = pixel_stage ? prog->pixel_sampler_units : prog->vertex_sampler_units;
  bindings = pixel_stage ? prog->pixel_sampler_bindings : prog->vertex_sampler_bindings;
  if (location_index >= MAX_PROGRAM_SAMPLERS) return 0;
  sampler_slot = units[location_index];
  if (sampler_slot < 0 || sampler_slot >= MAX_PROGRAM_SAMPLERS) return 0;
  return bindings[sampler_slot];
}

static bool get_uniform_var_by_location(GLProgram *prog, GLint location,
                                        bool *pixel_stage,
                                        GX2ShaderVarType *type,
                                        uint32_t *block_index,
                                        uint32_t *offset,
                                        uint32_t *word_count) {
  GX2VertexShader *vs;
  GX2PixelShader *ps;

  if (!prog || !prog->group) return false;

  vs = prog->group->vertexShader;
  ps = prog->group->pixelShader;

  if (vs) {
    for (uint32_t i = 0; i < vs->uniformVarCount; ++i) {
      GLint candidate = vs->uniformVars[i].block < 0
                            ? make_direct_uniform_location(false, (uint32_t)vs->uniformVars[i].offset)
                            : (GLint)(((uint32_t)vs->uniformVars[i].block << 16) |
                                      ((uint32_t)vs->uniformVars[i].offset & 0xFFFFu));
      if (candidate != location) continue;
      if (pixel_stage) *pixel_stage = false;
      if (type) *type = vs->uniformVars[i].type;
      if (block_index) *block_index = vs->uniformVars[i].block >= 0 ? (uint32_t)vs->uniformVars[i].block : 0u;
      if (offset) *offset = (uint32_t)vs->uniformVars[i].offset;
      if (word_count) {
        *word_count = shader_var_type_word_count(vs->uniformVars[i].type) *
                      shader_var_element_count(vs->uniformVars[i].count);
      }
      return true;
    }
  }

  if (ps) {
    for (uint32_t i = 0; i < ps->uniformVarCount; ++i) {
      GLint candidate = ps->uniformVars[i].block < 0
                            ? make_direct_uniform_location(true, (uint32_t)ps->uniformVars[i].offset)
                            : (GLint)(GL_LOCATION_STAGE_PIXEL |
                                      ((uint32_t)ps->uniformVars[i].block << 16) |
                                      ((uint32_t)ps->uniformVars[i].offset & 0xFFFFu));
      if (candidate != location) continue;
      if (pixel_stage) *pixel_stage = true;
      if (type) *type = ps->uniformVars[i].type;
      if (block_index) *block_index = ps->uniformVars[i].block >= 0 ? (uint32_t)ps->uniformVars[i].block : 0u;
      if (offset) *offset = (uint32_t)ps->uniformVars[i].offset;
      if (word_count) {
        *word_count = shader_var_type_word_count(ps->uniformVars[i].type) *
                      shader_var_element_count(ps->uniformVars[i].count);
      }
      return true;
    }
  }

  return false;
}

static bool read_uniform_words(GLProgram *prog, GLint location, uint32_t *words,
                               uint32_t max_words, uint32_t *actual_words,
                               GX2ShaderVarType *type) {
  bool pixel_stage = false;
  GX2ShaderVarType var_type;
  uint32_t block_index = 0;
  uint32_t offset = 0;
  uint32_t word_count = 0;
  UniformBlockShadow *block = NULL;
  uint8_t *direct_shadow = NULL;
  uint32_t direct_shadow_size = 0;
  uint8_t *src = NULL;

  if (!prog || !words || max_words == 0 || location == -1 || location_is_sampler(location)) {
    return false;
  }
  if (!get_uniform_var_by_location(prog, location, &pixel_stage, &var_type,
                                   &block_index, &offset, &word_count)) {
    return false;
  }
  if (word_count == 0 || word_count > max_words) {
    return false;
  }

  if (location_is_direct(location)) {
    bool virtual_block = stage_uses_virtual_uniform_block(prog, pixel_stage);
    uint32_t shadow_offset =
        virtual_block ? offset : offset * sizeof(uint32_t);
    direct_shadow = pixel_stage ? prog->ps_direct_uniform_shadow : prog->vs_direct_uniform_shadow;
    direct_shadow_size = pixel_stage ? prog->ps_direct_uniform_shadow_size : prog->vs_direct_uniform_shadow_size;
    if (!direct_shadow || shadow_offset + word_count * sizeof(uint32_t) > direct_shadow_size) {
      return false;
    }
    src = direct_shadow + shadow_offset;
    for (uint32_t i = 0; i < word_count; ++i) {
      uint32_t word;
      memcpy(&word, src + i * sizeof(uint32_t), sizeof(uint32_t));
      words[i] = virtual_block ? GPU_TO_CPU_32(word) : word;
    }
    if (actual_words) *actual_words = word_count;
    if (type) *type = var_type;
    return true;
  }

  block = pixel_stage ? (block_index < prog->ps_block_count ? &prog->ps_blocks[block_index] : NULL)
                      : (block_index < prog->vs_block_count ? &prog->vs_blocks[block_index] : NULL);
  if (!block || !block->buffer || offset + word_count * sizeof(uint32_t) > block->size) {
    return false;
  }

  src = (uint8_t *)block->buffer + offset;
  for (uint32_t i = 0; i < word_count; ++i) {
    uint32_t word;
    memcpy(&word, src + i * sizeof(uint32_t), sizeof(word));
    words[i] = GPU_TO_CPU_32(word);
  }

  if (actual_words) *actual_words = word_count;
  if (type) *type = var_type;
  return true;
}

static bool has_sampler_binding_conflict(const GX2SamplerVar *samplers,
                                         uint32_t sampler_count,
                                         const GLint *bindings) {
  for (uint32_t i = 0; i < sampler_count; ++i) {
    for (uint32_t j = i + 1; j < sampler_count; ++j) {
      if (bindings[i] == bindings[j] && samplers[i].type != samplers[j].type) {
        return true;
      }
    }
  }
  return false;
}

static GLCompiledShader *create_compiled_shader(GLenum type, const char *source) {
  GLCompiledShader *binary = (GLCompiledShader *)calloc(1u, sizeof(*binary));
  if (!binary) return NULL;
  binary->source = source ? strdup(source) : NULL;
  if (!binary->source) {
    free(binary);
    return NULL;
  }
  binary->type = type;
  binary->reference_count = 1;
  return binary;
}

static void retain_compiled_shader(GLCompiledShader *binary) {
  if (binary) ++binary->reference_count;
}

static void release_compiled_shader(GLCompiledShader *binary) {
  if (!binary || binary->reference_count == 0) return;
  if (--binary->reference_count != 0) return;

  if (binary->vertex_shader) {
    gx2gl_cafeglsl_free_vertex_shader(binary->vertex_shader);
  }
  if (binary->pixel_shader) {
    gx2gl_cafeglsl_free_pixel_shader(binary->pixel_shader);
  }
  free(binary->source);
  free(binary);
}

static void free_shader_binary(GLShader *shader) {
  if (!shader) return;
  release_compiled_shader(shader->compiled_binary);
  shader->compiled_binary = NULL;
  shader->compiled_vertex_shader = NULL;
  shader->compiled_pixel_shader = NULL;
}

static bool shader_is_attached_to_any_program(GLuint shader_id) {
  for (uint32_t i = 1; i < MAX_PROGRAMS; ++i) {
    if (!g_programs[i].in_use) continue;
    if (g_programs[i].attached_vertex_shader == shader_id ||
        g_programs[i].attached_pixel_shader == shader_id ||
        g_programs[i].attached_geometry_shader == shader_id) {
      return true;
    }
  }
  return false;
}

static void destroy_shader_object(GLuint shader_id) {
  GLShader *shader;

  if (shader_id == 0 || shader_id >= MAX_SHADERS) return;
  shader = &g_shaders[shader_id];

  free_shader_binary(shader);
  free(shader->source);
  free(shader->info_log);
  memset(shader, 0, sizeof(*shader));
}

static void maybe_destroy_shader(GLuint shader_id) {
  if (!is_valid_shader(shader_id)) return;
  if (g_shaders[shader_id].delete_pending && !shader_is_attached_to_any_program(shader_id)) {
    destroy_shader_object(shader_id);
  }
}

static void free_program_block_shadows(GLProgram *prog) {
  if (!prog) return;

  if (prog->vs_blocks) {
    for (uint32_t i = 0; i < prog->vs_block_count; ++i) {
      if (prog->vs_blocks[i].buffer) {
        gl_mem_free(GL_MEM_TYPE_MEM2, prog->vs_blocks[i].buffer);
      }
    }
    free(prog->vs_blocks);
    prog->vs_blocks = NULL;
  }
  if (prog->ps_blocks) {
    for (uint32_t i = 0; i < prog->ps_block_count; ++i) {
      if (prog->ps_blocks[i].buffer) {
        gl_mem_free(GL_MEM_TYPE_MEM2, prog->ps_blocks[i].buffer);
      }
    }
    free(prog->ps_blocks);
    prog->ps_blocks = NULL;
  }

  prog->vs_block_count = 0;
  prog->ps_block_count = 0;
}

static void free_program_direct_uniform_shadows(GLProgram *prog) {
  if (!prog) return;

  if (prog->vs_direct_uniform_shadow) {
    gl_mem_free(GL_MEM_TYPE_MEM2, prog->vs_direct_uniform_shadow);
    prog->vs_direct_uniform_shadow = NULL;
  }
  if (prog->ps_direct_uniform_shadow) {
    gl_mem_free(GL_MEM_TYPE_MEM2, prog->ps_direct_uniform_shadow);
    prog->ps_direct_uniform_shadow = NULL;
  }
  prog->vs_direct_uniform_shadow_size = 0;
  prog->ps_direct_uniform_shadow_size = 0;
}

static void free_program_uniform_value_shadows(GLProgram *prog) {
  if (!prog || !prog->uniform_value_shadows) return;

  for (uint32_t i = 0; i < prog->uniform_value_shadow_count; ++i) {
    free(prog->uniform_value_shadows[i].words);
    prog->uniform_value_shadows[i].words = NULL;
    prog->uniform_value_shadows[i].word_count = 0;
  }
  free(prog->uniform_value_shadows);
  prog->uniform_value_shadows = NULL;
  prog->uniform_value_shadow_count = 0;
}

static bool set_program_uniform_value_shadow(GLProgram *prog, GLint location,
                                             const uint32_t *words, uint32_t word_count) {
  ProgramUniformValueShadow *shadow = NULL;

  if (!prog || location == -1 || !words || word_count == 0 || location_is_sampler(location)) {
    return false;
  }

  for (uint32_t i = 0; i < prog->uniform_value_shadow_count; ++i) {
    if (prog->uniform_value_shadows[i].location == location) {
      shadow = &prog->uniform_value_shadows[i];
      break;
    }
  }

  if (!shadow) {
    ProgramUniformValueShadow *expanded =
        (ProgramUniformValueShadow *)realloc(prog->uniform_value_shadows,
                                             (prog->uniform_value_shadow_count + 1u) * sizeof(ProgramUniformValueShadow));
    if (!expanded) return false;
    prog->uniform_value_shadows = expanded;
    shadow = &prog->uniform_value_shadows[prog->uniform_value_shadow_count];
    memset(shadow, 0, sizeof(*shadow));
    shadow->location = location;
    ++prog->uniform_value_shadow_count;
  }

  if (shadow->word_count != word_count) {
    uint32_t *resized = (uint32_t *)realloc(shadow->words, word_count * sizeof(uint32_t));
    if (!resized) return false;
    shadow->words = resized;
    shadow->word_count = word_count;
  }

  memcpy(shadow->words, words, word_count * sizeof(uint32_t));
  return true;
}

static bool get_program_uniform_value_shadow(GLProgram *prog, GLint location,
                                             uint32_t *words, uint32_t max_words,
                                             uint32_t *actual_words) {
  if (!prog || !words || max_words == 0 || location == -1 || location_is_sampler(location)) {
    return false;
  }

  for (uint32_t i = 0; i < prog->uniform_value_shadow_count; ++i) {
    ProgramUniformValueShadow *shadow = &prog->uniform_value_shadows[i];
    if (shadow->location != location || !shadow->words) continue;
    if (shadow->word_count == 0 || shadow->word_count > max_words) return false;
    memcpy(words, shadow->words, shadow->word_count * sizeof(uint32_t));
    if (actual_words) *actual_words = shadow->word_count;
    return true;
  }

  return false;
}

static bool initialize_program_direct_uniform_shadows(GLProgram *prog, GX2VertexShader *vs, GX2PixelShader *ps) {
  uint32_t vertex_shadow_size = 0;
  uint32_t pixel_shadow_size = 0;

  if (!prog) return false;

  free_program_direct_uniform_shadows(prog);

  if (vs) {
    for (uint32_t i = 0; i < vs->uniformVarCount; ++i) {
      uint32_t size_bytes;
      uint32_t end_offset;
      if (vs->uniformVars[i].block >= 0) continue;
      size_bytes = shader_var_type_word_count(vs->uniformVars[i].type) *
                   shader_var_element_count(vs->uniformVars[i].count) *
                   sizeof(uint32_t);
      // CafeGLSL exposes loose-uniform offsets as bytes in its virtual block.
      end_offset = vs->mode == GX2_SHADER_MODE_UNIFORM_BLOCK
                       ? (uint32_t)vs->uniformVars[i].offset + size_bytes
                       : (uint32_t)vs->uniformVars[i].offset *
                                 sizeof(uint32_t) +
                             size_bytes;
      if (end_offset > vertex_shadow_size) {
        vertex_shadow_size = end_offset;
      }
    }
  }
  if (ps) {
    for (uint32_t i = 0; i < ps->uniformVarCount; ++i) {
      uint32_t size_bytes;
      uint32_t end_offset;
      if (ps->uniformVars[i].block >= 0) continue;
      size_bytes = shader_var_type_word_count(ps->uniformVars[i].type) *
                   shader_var_element_count(ps->uniformVars[i].count) *
                   sizeof(uint32_t);
      // CafeGLSL exposes loose-uniform offsets as bytes in its virtual block.
      end_offset = ps->mode == GX2_SHADER_MODE_UNIFORM_BLOCK
                       ? (uint32_t)ps->uniformVars[i].offset + size_bytes
                       : (uint32_t)ps->uniformVars[i].offset *
                                 sizeof(uint32_t) +
                             size_bytes;
      if (end_offset > pixel_shadow_size) {
        pixel_shadow_size = end_offset;
      }
    }
  }

  vertex_shadow_size = (vertex_shadow_size + 15u) & ~15u;
  pixel_shadow_size = (pixel_shadow_size + 15u) & ~15u;

  if (vertex_shadow_size > 0) {
    prog->vs_direct_uniform_shadow = (uint8_t *)gl_mem_alloc(
        GL_MEM_TYPE_MEM2, vertex_shadow_size, 256);
    if (!prog->vs_direct_uniform_shadow) {
      free_program_direct_uniform_shadows(prog);
      return false;
    }
    memset(prog->vs_direct_uniform_shadow, 0, vertex_shadow_size);
    DCFlushRange(prog->vs_direct_uniform_shadow, vertex_shadow_size);
    prog->vs_direct_uniform_shadow_size = vertex_shadow_size;
  }

  if (pixel_shadow_size > 0) {
    prog->ps_direct_uniform_shadow = (uint8_t *)gl_mem_alloc(
        GL_MEM_TYPE_MEM2, pixel_shadow_size, 256);
    if (!prog->ps_direct_uniform_shadow) {
      free_program_direct_uniform_shadows(prog);
      return false;
    }
    memset(prog->ps_direct_uniform_shadow, 0, pixel_shadow_size);
    DCFlushRange(prog->ps_direct_uniform_shadow, pixel_shadow_size);
    prog->ps_direct_uniform_shadow_size = pixel_shadow_size;
  }

  return true;
}

static void clear_program_runtime_state(GLProgram *prog) {
  if (!prog) return;

  free_program_block_shadows(prog);
  free_program_direct_uniform_shadows(prog);
  free_program_uniform_value_shadows(prog);
  free_program_uniform_blocks(prog);
  free_program_attrib_reflection(prog);
  free_program_transform_feedback_linked(prog);

  if (prog->owns_whb_group) {
    WHBGfxFreeShaderGroup(&prog->owned_group);
  } else if (prog->owns_group && prog->owned_group.fetchShaderProgram) {
    gl_mem_free(GL_MEM_TYPE_MEM2, prog->owned_group.fetchShaderProgram);
  }
  if (prog->dynamic_fetch_shader_program) {
    gl_mem_free(GL_MEM_TYPE_MEM2, prog->dynamic_fetch_shader_program);
  }
  release_compiled_shader(prog->linked_vertex_binary);
  release_compiled_shader(prog->linked_pixel_binary);

  memset(&prog->owned_group, 0, sizeof(prog->owned_group));
  memset(&prog->dynamic_fetch_shader, 0, sizeof(prog->dynamic_fetch_shader));
  prog->dynamic_fetch_shader_program = NULL;
  prog->dynamic_fetch_shader_program_size = 0;
  prog->dynamic_fetch_shader_attrib_count = 0;
  prog->linked_vertex_binary = NULL;
  prog->linked_pixel_binary = NULL;
  prog->group = NULL;
  prog->owns_group = false;
  prog->owns_whb_group = false;
  prog->linked = false;
  prog->validated = false;
  prog->tf_linked_buffer_mode =
      prog->tf_pending_buffer_mode ? prog->tf_pending_buffer_mode
                                   : GL_INTERLEAVED_ATTRIBS;
  prog->tf_required_binding_count = 0;
  prog->tf_stream_out_ready = false;
  memset(prog->vertex_sampler_units, -1, sizeof(prog->vertex_sampler_units));
  memset(prog->pixel_sampler_units, -1, sizeof(prog->pixel_sampler_units));
  memset(prog->vertex_sampler_bindings, 0, sizeof(prog->vertex_sampler_bindings));
  memset(prog->pixel_sampler_bindings, 0, sizeof(prog->pixel_sampler_bindings));
}

static void destroy_program_object(GLProgram *prog) {
  if (!prog) return;

  clear_program_runtime_state(prog);
  free_program_transform_feedback_pending(prog);
  free(prog->info_log);
  prog->info_log = NULL;

  for (uint32_t i = 0; i < GL33_MAX_VERTEX_ATTRIBS; ++i) {
    free(prog->attrib_binding_names[i]);
    prog->attrib_binding_names[i] = NULL;
  }

  memset(prog, 0, sizeof(*prog));
}

static void free_program_uniform_blocks(GLProgram *prog) {
  if (!prog || !prog->uniform_blocks) return;
  for (uint32_t i = 0; i < prog->uniform_block_count; ++i) {
    free(prog->uniform_blocks[i].name);
  }
  free(prog->uniform_blocks);
  prog->uniform_blocks = NULL;
  prog->uniform_block_count = 0;
}

static void free_program_attrib_reflection(GLProgram *prog) {
  if (!prog) return;
  for (uint32_t i = 0; i < GL33_MAX_VERTEX_ATTRIBS; ++i) {
    free(prog->attrib_reflection[i].name);
    memset(&prog->attrib_reflection[i], 0, sizeof(prog->attrib_reflection[i]));
  }
}

static void free_program_transform_feedback_pending(GLProgram *prog) {
  if (!prog || !prog->tf_pending_varyings) return;
  for (uint32_t i = 0; i < prog->tf_pending_count; ++i) {
    free(prog->tf_pending_varyings[i]);
  }
  free(prog->tf_pending_varyings);
  prog->tf_pending_varyings = NULL;
  prog->tf_pending_count = 0;
}

static void free_program_transform_feedback_linked(GLProgram *prog) {
  if (!prog || !prog->tf_linked_varyings) {
    if (prog) {
      prog->tf_linked_count = 0;
      prog->tf_linked_max_name_length = 0;
      prog->tf_required_binding_count = 0;
      prog->tf_stream_out_ready = false;
    }
    return;
  }

  for (uint32_t i = 0; i < prog->tf_linked_count; ++i) {
    free(prog->tf_linked_varyings[i].name);
  }
  free(prog->tf_linked_varyings);
  prog->tf_linked_varyings = NULL;
  prog->tf_linked_count = 0;
  prog->tf_linked_max_name_length = 0;
  prog->tf_required_binding_count = 0;
  prog->tf_stream_out_ready = false;
}

static void free_transform_feedback_varying_array(
    ProgramTransformFeedbackVarying *varyings, uint32_t count) {
  if (!varyings) return;
  for (uint32_t i = 0; i < count; ++i) {
    free(varyings[i].name);
  }
  free(varyings);
}

static ProgramUniformBlock *find_program_uniform_block(GLProgram *prog, const char *name) {
  if (!prog || !name) return NULL;
  for (uint32_t i = 0; i < prog->uniform_block_count; ++i) {
    if (!prog->uniform_blocks[i].implicit &&
        prog->uniform_blocks[i].name &&
        strcmp(prog->uniform_blocks[i].name, name) == 0) {
      return &prog->uniform_blocks[i];
    }
  }
  return NULL;
}

static ProgramUniformBlock *find_program_implicit_uniform_block(GLProgram *prog) {
  if (!prog) return NULL;
  for (uint32_t i = 0; i < prog->uniform_block_count; ++i) {
    if (prog->uniform_blocks[i].implicit) {
      return &prog->uniform_blocks[i];
    }
  }
  return NULL;
}

static ProgramUniformBlock *append_program_uniform_block(GLProgram *prog, const char *name,
                                                         bool implicit) {
  ProgramUniformBlock *blocks;
  ProgramUniformBlock *block;

  if (!prog || (!name && !implicit)) return NULL;
  blocks = (ProgramUniformBlock *)realloc(
      prog->uniform_blocks, (prog->uniform_block_count + 1) * sizeof(ProgramUniformBlock));
  if (!blocks) return NULL;
  prog->uniform_blocks = blocks;
  block = &prog->uniform_blocks[prog->uniform_block_count];
  memset(block, 0, sizeof(*block));
  if (name) {
    block->name = strdup(name);
    if (!block->name) return NULL;
  }
  block->vertex_block_index = -1;
  block->pixel_block_index = -1;
  block->binding_point = prog->uniform_block_count;
  block->implicit = implicit;
  ++prog->uniform_block_count;
  return block;
}

static bool is_cafeglsl_implicit_uniform_block(const GX2UniformBlock *block) {
  if (!block) return false;
  return (!block->name || block->name[0] == '\0') &&
         block->offset == GX2GL_CAFEGLSL_VIRTUAL_UNIFORM_BLOCK_LOCATION;
}

static void initialize_program_uniform_block_binding(ProgramUniformBlock *block,
                                                     uint32_t gx2_location) {
  if (!block || block->binding_initialized) return;
  if (gx2_location < GL33_MAX_UNIFORM_BUFFER_BINDINGS) {
    block->binding_point = gx2_location;
  }
  block->binding_initialized = true;
}

static void build_program_uniform_blocks(GLProgram *prog) {
  GX2VertexShader *vs;
  GX2PixelShader *ps;

  if (!prog || !prog->group) return;

  free_program_uniform_blocks(prog);

  vs = prog->group->vertexShader;
  ps = prog->group->pixelShader;

  if (vs) {
    for (uint32_t i = 0; i < vs->uniformBlockCount; ++i) {
      const GX2UniformBlock *shader_block = &vs->uniformBlocks[i];
      bool implicit = is_cafeglsl_implicit_uniform_block(shader_block);
      ProgramUniformBlock *block = implicit ? find_program_implicit_uniform_block(prog)
                                            : find_program_uniform_block(prog, shader_block->name);
      if (!block) block = append_program_uniform_block(prog, shader_block->name, implicit);
      if (!block) continue;
      block->vertex_block_index = (int32_t)i;
      block->vertex_location = shader_block->offset;
      block->vertex_size = shader_block->size;
      if (implicit) {
        block->implicit = true;
      }
      initialize_program_uniform_block_binding(block, shader_block->offset);
    }
  }

  if (ps) {
    for (uint32_t i = 0; i < ps->uniformBlockCount; ++i) {
      const GX2UniformBlock *shader_block = &ps->uniformBlocks[i];
      bool implicit = is_cafeglsl_implicit_uniform_block(shader_block);
      ProgramUniformBlock *block = implicit ? find_program_implicit_uniform_block(prog)
                                            : find_program_uniform_block(prog, shader_block->name);
      if (!block) block = append_program_uniform_block(prog, shader_block->name, implicit);
      if (!block) continue;
      block->pixel_block_index = (int32_t)i;
      block->pixel_location = shader_block->offset;
      block->pixel_size = shader_block->size;
      if (implicit) {
        block->implicit = true;
      }
      initialize_program_uniform_block_binding(block, shader_block->offset);
    }
  }
}

static bool shader_var_type_to_reflection(GX2ShaderVarType type, GLenum *base_type, uint32_t *component_count) {
  if (!base_type || !component_count) return false;

  switch (type) {
  case GX2_SHADER_VAR_TYPE_FLOAT:
    *base_type = GL_FLOAT;
    *component_count = 1;
    return true;
  case GX2_SHADER_VAR_TYPE_FLOAT2:
    *base_type = GL_FLOAT;
    *component_count = 2;
    return true;
  case GX2_SHADER_VAR_TYPE_FLOAT3:
    *base_type = GL_FLOAT;
    *component_count = 3;
    return true;
  case GX2_SHADER_VAR_TYPE_FLOAT4:
    *base_type = GL_FLOAT;
    *component_count = 4;
    return true;
  case GX2_SHADER_VAR_TYPE_INT:
  case GX2_SHADER_VAR_TYPE_BOOL:
    *base_type = GL_INT;
    *component_count = 1;
    return true;
  case GX2_SHADER_VAR_TYPE_INT2:
  case GX2_SHADER_VAR_TYPE_BOOL2:
    *base_type = GL_INT;
    *component_count = 2;
    return true;
  case GX2_SHADER_VAR_TYPE_INT3:
  case GX2_SHADER_VAR_TYPE_BOOL3:
    *base_type = GL_INT;
    *component_count = 3;
    return true;
  case GX2_SHADER_VAR_TYPE_INT4:
  case GX2_SHADER_VAR_TYPE_BOOL4:
    *base_type = GL_INT;
    *component_count = 4;
    return true;
  case GX2_SHADER_VAR_TYPE_UINT:
    *base_type = GL_UNSIGNED_INT;
    *component_count = 1;
    return true;
  case GX2_SHADER_VAR_TYPE_UINT2:
    *base_type = GL_UNSIGNED_INT;
    *component_count = 2;
    return true;
  case GX2_SHADER_VAR_TYPE_UINT3:
    *base_type = GL_UNSIGNED_INT;
    *component_count = 3;
    return true;
  case GX2_SHADER_VAR_TYPE_UINT4:
    *base_type = GL_UNSIGNED_INT;
    *component_count = 4;
    return true;
  default:
    return false;
  }
}

static void build_program_attrib_reflection(GLProgram *prog, GX2VertexShader *vertex_shader) {
  if (!prog) return;

  free_program_attrib_reflection(prog);
  if (!vertex_shader) return;

  for (uint32_t i = 0; i < vertex_shader->attribVarCount; ++i) {
    uint32_t location = vertex_shader->attribVars[i].location;
    ProgramAttribReflection *reflection;

    if (location >= GL33_MAX_VERTEX_ATTRIBS) continue;
    reflection = &prog->attrib_reflection[location];
    if (!shader_var_type_to_reflection(vertex_shader->attribVars[i].type,
                                       &reflection->base_type,
                                       &reflection->component_count)) {
      continue;
    }

    reflection->active = true;
    replace_owned_string(&reflection->name,
                         vertex_shader->attribVars[i].name ? vertex_shader->attribVars[i].name : "");
  }
}

static GLuint program_stream_out_binding_count(const GLProgram *prog) {
  const GX2VertexShader *vs;
  GLuint count = 0;

  if (!prog || !prog->group) return 0;

  vs = prog->group->vertexShader;
  if (vs && vs->hasStreamOut) {
    for (GLuint i = 0; i < GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS; ++i) {
      if (vs->streamOutStride[i] != 0) count = i + 1u;
    }
    if (count == 0) count = 1;
  }

  return count;
}

static void initialize_program_prelinked_stream_out(GLProgram *prog) {
  GLuint binding_count;

  if (!prog) return;

  free_program_transform_feedback_linked(prog);
  prog->tf_linked_buffer_mode =
      prog->tf_pending_buffer_mode ? prog->tf_pending_buffer_mode
                                   : GL_INTERLEAVED_ATTRIBS;

  binding_count = program_stream_out_binding_count(prog);
  if (binding_count != 0) {
    prog->tf_stream_out_ready = true;
    prog->tf_required_binding_count = binding_count;
  }
}

static bool set_transform_feedback_link_error(char *message,
                                              size_t message_capacity,
                                              const char *text) {
  if (message && message_capacity > 0) {
    snprintf(message, message_capacity, "%s", text ? text : "");
  }
  return false;
}

static bool build_program_transform_feedback_link_state(GLProgram *prog,
                                                        const char *vertex_source,
                                                        char *message,
                                                        size_t message_capacity) {
  ProgramTransformFeedbackVarying *varyings = NULL;
  uint32_t total_components = 0;
  GLuint required_bindings;
  GLsizei max_name_length = 0;

  if (!prog) return false;

  free_program_transform_feedback_linked(prog);
  prog->tf_linked_buffer_mode =
      prog->tf_pending_buffer_mode ? prog->tf_pending_buffer_mode
                                   : GL_INTERLEAVED_ATTRIBS;

  if (prog->tf_pending_count == 0) {
    return true;
  }

  if (!vertex_source) {
    return set_transform_feedback_link_error(
        message, message_capacity,
        "Transform feedback varying validation requires vertex shader source.");
  }

  for (uint32_t i = 0; i < prog->tf_pending_count; ++i) {
    if (!prog->tf_pending_varyings[i]) {
      return set_transform_feedback_link_error(
          message, message_capacity,
          "Transform feedback varying names must not be null.");
    }
    for (uint32_t j = i + 1; j < prog->tf_pending_count; ++j) {
      if (prog->tf_pending_varyings[j] &&
          strcmp(prog->tf_pending_varyings[i],
                 prog->tf_pending_varyings[j]) == 0) {
        return set_transform_feedback_link_error(
            message, message_capacity,
            "Transform feedback varying names must be unique.");
      }
    }
  }

  varyings = (ProgramTransformFeedbackVarying *)calloc(
      prog->tf_pending_count, sizeof(ProgramTransformFeedbackVarying));
  if (!varyings) {
    _gl_set_error(GL_OUT_OF_MEMORY);
    return false;
  }

  for (uint32_t i = 0; i < prog->tf_pending_count; ++i) {
    GLenum type = 0;
    GLint size = 0;
    uint32_t components = 0;
    const char *name = prog->tf_pending_varyings[i];
    char error_message[256];

    if (!gx2gl_find_shader_output_info(vertex_source, name, &type,
                                       &size, &components)) {
      snprintf(error_message, sizeof(error_message),
               "Transform feedback varying '%s' is not a supported vertex shader output.",
               name);
      set_transform_feedback_link_error(message, message_capacity,
                                        error_message);
      free_transform_feedback_varying_array(varyings, i);
      return false;
    }

    if (prog->tf_linked_buffer_mode == GL_SEPARATE_ATTRIBS &&
        components > GX2GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS) {
      snprintf(error_message, sizeof(error_message),
               "Transform feedback varying '%s' exceeds the separate component limit.",
               name);
      set_transform_feedback_link_error(message, message_capacity,
                                        error_message);
      free_transform_feedback_varying_array(varyings, i);
      return false;
    }

    total_components += components;
    varyings[i].name = strdup(name);
    if (!varyings[i].name) {
      free_transform_feedback_varying_array(varyings, i);
      _gl_set_error(GL_OUT_OF_MEMORY);
      return false;
    }
    varyings[i].size = size;
    varyings[i].type = type;
    varyings[i].component_count = components;
    if ((GLsizei)strlen(name) + 1 > max_name_length) {
      max_name_length = (GLsizei)strlen(name) + 1;
    }
  }

  if (prog->tf_linked_buffer_mode == GL_INTERLEAVED_ATTRIBS &&
      total_components > GX2GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS) {
    free_transform_feedback_varying_array(varyings, prog->tf_pending_count);
    return set_transform_feedback_link_error(
        message, message_capacity,
        "Transform feedback varyings exceed the interleaved component limit.");
  }

  required_bindings =
      prog->tf_linked_buffer_mode == GL_INTERLEAVED_ATTRIBS
          ? 1u
          : (GLuint)prog->tf_pending_count;
  if (required_bindings > GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS) {
    free_transform_feedback_varying_array(varyings, prog->tf_pending_count);
    return set_transform_feedback_link_error(
        message, message_capacity,
        "Transform feedback varyings exceed the separate buffer limit.");
  }

  if (program_stream_out_binding_count(prog) == 0) {
    free_transform_feedback_varying_array(varyings, prog->tf_pending_count);
    return set_transform_feedback_link_error(
        message, message_capacity,
        "Transform feedback varyings are valid, but this shader backend did not emit stream-out metadata.");
  }

  prog->tf_linked_varyings = varyings;
  prog->tf_linked_count = prog->tf_pending_count;
  prog->tf_linked_max_name_length = max_name_length;
  prog->tf_required_binding_count = required_bindings;
  prog->tf_stream_out_ready = true;
  return true;
}

static bool initialize_program_block_shadows(GLProgram *prog, GX2VertexShader *vs, GX2PixelShader *ps) {
  if (!prog) return false;

  prog->vs_block_count = vs ? vs->uniformBlockCount : 0;
  prog->ps_block_count = ps ? ps->uniformBlockCount : 0;

  if (prog->vs_block_count) {
    prog->vs_blocks = (UniformBlockShadow *)calloc(prog->vs_block_count, sizeof(UniformBlockShadow));
    if (!prog->vs_blocks) {
      return false;
    }
    for (uint32_t i = 0; i < prog->vs_block_count; ++i) {
      prog->vs_blocks[i].size = vs->uniformBlocks[i].size;
      prog->vs_blocks[i].buffer = gl_mem_alloc(GL_MEM_TYPE_MEM2, vs->uniformBlocks[i].size, 256);
      if (!prog->vs_blocks[i].buffer) {
        return false;
      }
      memset(prog->vs_blocks[i].buffer, 0, vs->uniformBlocks[i].size);
      DCFlushRange(prog->vs_blocks[i].buffer, vs->uniformBlocks[i].size);
    }
  }

  if (prog->ps_block_count) {
    prog->ps_blocks = (UniformBlockShadow *)calloc(prog->ps_block_count, sizeof(UniformBlockShadow));
    if (!prog->ps_blocks) {
      return false;
    }
    for (uint32_t i = 0; i < prog->ps_block_count; ++i) {
      prog->ps_blocks[i].size = ps->uniformBlocks[i].size;
      prog->ps_blocks[i].buffer = gl_mem_alloc(GL_MEM_TYPE_MEM2, ps->uniformBlocks[i].size, 256);
      if (!prog->ps_blocks[i].buffer) {
        return false;
      }
      memset(prog->ps_blocks[i].buffer, 0, ps->uniformBlocks[i].size);
      DCFlushRange(prog->ps_blocks[i].buffer, ps->uniformBlocks[i].size);
    }
  }

  return true;
}

static void initialize_program_sampler_tables(GLProgram *prog, GX2VertexShader *vs, GX2PixelShader *ps) {
  memset(prog->vertex_sampler_units, -1, sizeof(prog->vertex_sampler_units));
  memset(prog->pixel_sampler_units, -1, sizeof(prog->pixel_sampler_units));
  memset(prog->vertex_sampler_bindings, 0, sizeof(prog->vertex_sampler_bindings));
  memset(prog->pixel_sampler_bindings, 0, sizeof(prog->pixel_sampler_bindings));
  for (uint32_t i = 0; i < MAX_PROGRAM_SAMPLERS; ++i) {
    prog->vertex_sampler_targets[i] = GL_TEXTURE_2D;
    prog->pixel_sampler_targets[i] = GL_TEXTURE_2D;
  }

  if (vs) {
    for (uint32_t i = 0; i < vs->samplerVarCount && i < MAX_PROGRAM_SAMPLERS; ++i) {
      uint32_t location = (uint32_t)vs->samplerVars[i].location;
      if (location < MAX_PROGRAM_SAMPLERS) {
        prog->vertex_sampler_units[location] = (GLint)i;
      }
      prog->vertex_sampler_bindings[i] = 0;
      prog->vertex_sampler_targets[i] =
          sampler_var_type_to_texture_target(vs->samplerVars[i].type);
    }
  }
  if (ps) {
    for (uint32_t i = 0; i < ps->samplerVarCount && i < MAX_PROGRAM_SAMPLERS; ++i) {
      uint32_t location = (uint32_t)ps->samplerVars[i].location;
      if (location < MAX_PROGRAM_SAMPLERS) {
        prog->pixel_sampler_units[location] = (GLint)i;
      }
      prog->pixel_sampler_bindings[i] = 0;
      prog->pixel_sampler_targets[i] =
          sampler_var_type_to_texture_target(ps->samplerVars[i].type);
    }
  }
}

static bool initialize_program_from_group(GLProgram *prog, const WHBGfxShaderGroup *group,
                                          bool owns_group, bool owns_whb_group) {
  WHBGfxShaderGroup owned_group_copy;
  GX2VertexShader *vs;
  GX2PixelShader *ps;

  if (!prog || !group) return false;

  if (owns_group) {
    owned_group_copy = *group;
    group = &owned_group_copy;
  }

  clear_program_runtime_state(prog);

  if (owns_group) {
    prog->owned_group = *group;
    prog->group = &prog->owned_group;
  } else {
    prog->group = group;
  }
  prog->owns_group = owns_group;
  prog->owns_whb_group = owns_whb_group;
  vs = group->vertexShader;
  ps = group->pixelShader;

  if (!initialize_program_block_shadows(prog, vs, ps)) {
    clear_program_runtime_state(prog);
    return false;
  }
  if (!initialize_program_direct_uniform_shadows(prog, vs, ps)) {
    clear_program_runtime_state(prog);
    return false;
  }

  initialize_program_sampler_tables(prog, vs, ps);
  build_program_uniform_blocks(prog);
  build_program_attrib_reflection(prog, vs);
  initialize_program_prelinked_stream_out(prog);
  prog->linked = true;
  prog->validated = false;
  return true;
}

static GX2ShaderMode choose_shader_mode(const GLProgram *prog) {
  bool has_uniform_blocks = false;
  GX2VertexShader *vs;
  GX2PixelShader *ps;

  if (!prog || !prog->group) {
    return GX2_SHADER_MODE_UNIFORM_REGISTER;
  }

  vs = prog->group->vertexShader;
  ps = prog->group->pixelShader;

  if (vs && ps && vs->mode == ps->mode) {
    return vs->mode;
  }
  if ((vs && vs->mode == GX2_SHADER_MODE_UNIFORM_BLOCK) ||
      (ps && ps->mode == GX2_SHADER_MODE_UNIFORM_BLOCK)) {
    return GX2_SHADER_MODE_UNIFORM_BLOCK;
  }

  if (vs && vs->uniformBlockCount > 0) {
    has_uniform_blocks = true;
  }

  if (ps && ps->uniformBlockCount > 0) {
    has_uniform_blocks = true;
  }

  return has_uniform_blocks ? GX2_SHADER_MODE_UNIFORM_BLOCK : GX2_SHADER_MODE_UNIFORM_REGISTER;
}

static bool location_is_sampler(GLint l) { return (((uint32_t)l & GL_LOCATION_KIND_SAMPLER) != 0u); }
static bool location_is_pixel(GLint l) { return (((uint32_t)l & GL_LOCATION_STAGE_PIXEL) != 0u); }
static bool location_is_direct(GLint l) { return (((uint32_t)l & GL_LOCATION_KIND_DIRECT) != 0u); }

static bool stage_uses_virtual_uniform_block(const GLProgram *prog,
                                             bool pixel_stage) {
  const GX2VertexShader *vs;
  const GX2PixelShader *ps;

  if (!prog || !prog->group) return false;
  vs = prog->group->vertexShader;
  ps = prog->group->pixelShader;
  return pixel_stage
             ? ps && ps->mode == GX2_SHADER_MODE_UNIFORM_BLOCK
             : vs && vs->mode == GX2_SHADER_MODE_UNIFORM_BLOCK;
}

static bool update_uniform_words(GLint location, const uint32_t *data, GLsizei count_words, uint32_t extra_offset_bytes) {
  if (location == -1 || count_words <= 0 || !data) return true;
  if (!g_gl_context) return false;
  GLuint prog_id = g_gl_context->bound_program;
  if (!is_valid_program(prog_id)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return false;
  }
  GLProgram *prog = &g_programs[prog_id];
  location = resolve_uniform_location(prog, location);
  if (location_is_sampler(location)) return false;
  bool is_pixel = location_is_pixel(location);
  uint32_t offset = ((uint32_t)location & 0xFFFFu) + extra_offset_bytes;
  uint32_t byte_count = (uint32_t)count_words * 4;
  set_program_uniform_value_shadow(prog, location, data, (uint32_t)count_words);
  if (location_is_direct(location)) {
    uint8_t *shadow = is_pixel ? prog->ps_direct_uniform_shadow : prog->vs_direct_uniform_shadow;
    uint32_t shadow_size = is_pixel ? prog->ps_direct_uniform_shadow_size : prog->vs_direct_uniform_shadow_size;
    if (stage_uses_virtual_uniform_block(prog, is_pixel)) {
      uint32_t shadow_offset = offset;
      if (!shadow || shadow_offset + byte_count > shadow_size) {
        return false;
      }
      for (GLsizei i = 0; i < count_words; ++i) {
        uint32_t swapped = CPU_TO_GPU_32(data[i]);
        memcpy(shadow + shadow_offset + (uint32_t)i * sizeof(uint32_t),
               &swapped, sizeof(swapped));
      }
      DCFlushRange(shadow + shadow_offset, byte_count);
      return true;
    }

    uint32_t shadow_offset = offset * sizeof(uint32_t);
    uint32_t upload_offset = offset & ~3u;
    uint32_t upload_end = (offset + (uint32_t)count_words + 3u) & ~3u;
    uint32_t upload_words = upload_end - upload_offset;
    uint32_t upload_bytes = upload_words * sizeof(uint32_t);
    uint32_t swapped_stack[16];
    uint32_t *swapped = upload_words <= (uint32_t)(sizeof(swapped_stack) / sizeof(swapped_stack[0]))
                            ? swapped_stack
                            : (uint32_t *)malloc(upload_bytes);
    if (!swapped || !shadow || shadow_offset + byte_count > shadow_size ||
        upload_offset * sizeof(uint32_t) + upload_bytes > shadow_size) {
      if (swapped && swapped != swapped_stack) free(swapped);
      return false;
    }
    memcpy(shadow + shadow_offset, data, byte_count);
    for (uint32_t i = 0; i < upload_words; ++i) {
      uint32_t word;
      memcpy(&word, shadow + (upload_offset + i) * sizeof(uint32_t), sizeof(word));
      swapped[i] = CPU_TO_GPU_32(word);
    }
    if (is_pixel) GX2SetPixelUniformReg(upload_offset, upload_words, swapped);
    else GX2SetVertexUniformReg(upload_offset, upload_words, swapped);
    if (swapped != swapped_stack) free(swapped);
    return true;
  }
  uint32_t block_idx = ((uint32_t)location >> 16) & GL_LOCATION_BLOCK_MASK;
  UniformBlockShadow *block = is_pixel ? (block_idx < prog->ps_block_count ? &prog->ps_blocks[block_idx] : NULL) : (block_idx < prog->vs_block_count ? &prog->vs_blocks[block_idx] : NULL);
  if (!block || !block->buffer || offset + byte_count > block->size) return false;
  uint32_t *dst = (uint32_t *)((uint8_t *)block->buffer + offset);
  for (GLsizei i = 0; i < count_words; i++) dst[i] = CPU_TO_GPU_32(data[i]);
  DCFlushRange(dst, byte_count);
  return true;
}

static bool update_sampler_bindings(GLint location, GLsizei count, const GLint *values) {
  if (location == -1 || count <= 0 || !values) return true;
  if (!g_gl_context) return false;

  GLuint prog_id = g_gl_context->bound_program;
  if (!is_valid_program(prog_id)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return true;
  }

  GLProgram *prog = &g_programs[prog_id];
  location = resolve_uniform_location(prog, location);
  if (!location_is_sampler(location)) return false;
  GLint *sampler_units = location_is_pixel(location) ? prog->pixel_sampler_units : prog->vertex_sampler_units;
  GLint *sampler_bindings =
      location_is_pixel(location) ? prog->pixel_sampler_bindings : prog->vertex_sampler_bindings;
  uint32_t base_location = (uint32_t)location & 0xFFFFu;

  for (GLsizei i = 0; i < count; ++i) {
    uint32_t location_index = base_location + (uint32_t)i;
    GLint sampler_slot;

    if (location_index >= MAX_PROGRAM_SAMPLERS) return false;
    sampler_slot = sampler_units[location_index];
    if (sampler_slot < 0 || sampler_slot >= MAX_PROGRAM_SAMPLERS) return false;

    sampler_bindings[sampler_slot] = values[i];
  }

  g_gl_context->dirty_flags |= GL_DIRTY_TEXTURE_BINDINGS;
  return true;
}

static GLint make_sampler_location(bool pixel_stage, uint32_t location) {
  return (GLint)((pixel_stage ? GL_LOCATION_STAGE_PIXEL : 0u) | GL_LOCATION_KIND_SAMPLER | (location & 0xFFFFu));
}

static GLint make_direct_uniform_location(bool pixel_stage, uint32_t offset) {
  return (GLint)((pixel_stage ? GL_LOCATION_STAGE_PIXEL : 0u) | GL_LOCATION_KIND_DIRECT | (offset & 0xFFFFu));
}

static GLuint get_bound_texture_for_unit(GLuint unit, GLenum target) {
  if (!g_gl_context || unit >= 32) return 0;
  switch (target) {
  case GL_TEXTURE_1D: return g_gl_context->bound_texture_1d[unit];
  case GL_TEXTURE_2D: return g_gl_context->bound_texture_2d[unit];
  case GL_TEXTURE_3D: return g_gl_context->bound_texture_3d[unit];
  case GL_TEXTURE_CUBE_MAP: return g_gl_context->bound_texture_cube[unit];
  case GL_TEXTURE_1D_ARRAY: return g_gl_context->bound_texture_1d_array[unit];
  case GL_TEXTURE_2D_ARRAY: return g_gl_context->bound_texture_2d_array[unit];
  case GL_TEXTURE_2D_MULTISAMPLE:
    return g_gl_context->bound_texture_2d_multisample[unit];
  case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
    return g_gl_context->bound_texture_2d_multisample_array[unit];
  default: return 0;
  }
}

static void bind_stage_samplers(const GLint *sampler_units,
                                const GLint *sampler_bindings,
                                const GLenum *sampler_targets,
                                bool pixel_stage) {
  for (uint32_t location = 0; location < MAX_PROGRAM_SAMPLERS; ++location) {
    GLint shader_slot = sampler_units[location];
    GLint texture_unit;
    GLuint tex_id;
    GX2Texture *tex;
    GLuint sampler_id;
    GX2Sampler *sampler;

    if (shader_slot < 0 || shader_slot >= MAX_PROGRAM_SAMPLERS) continue;

    texture_unit = sampler_bindings[shader_slot];
    if (texture_unit < 0 || texture_unit >= 32) continue;

    tex_id = get_bound_texture_for_unit((GLuint)texture_unit,
                                        sampler_targets[shader_slot]);
    if (!tex_id) continue;

    gl_framebuffer_sync_texture_for_sampling(tex_id);

    tex = gl_get_gx2_texture(tex_id);
    if (!tex) continue;

    sampler_id = g_gl_context->bound_sampler[texture_unit];
    sampler = gl_get_gx2_sampler(sampler_id ? sampler_id : tex_id, sampler_id != 0);

    if (pixel_stage) {
      GX2SetPixelTexture(tex, (uint32_t)shader_slot);
      if (sampler) GX2SetPixelSampler(sampler, (uint32_t)shader_slot);
    } else {
      GX2SetVertexTexture(tex, (uint32_t)shader_slot);
      if (sampler) GX2SetVertexSampler(sampler, (uint32_t)shader_slot);
    }
  }
}

static bool bind_uniform_block_buffer(GLuint buffer_id, uint32_t offset, uint32_t available_size,
                                      uint32_t required_size, bool pixel_stage, uint32_t slot) {
  uint8_t *data;
  GLsizeiptr size;

  size = gl_buffer_get_size(buffer_id);
  if (offset > (uint32_t)size || available_size > (uint32_t)size - offset || required_size > available_size) {
    return false;
  }

  data = (uint8_t *)gl_buffer_get_uniform_block_data(buffer_id, offset, required_size);
  if (!data) return false;

  GX2Invalidate(GX2_INVALIDATE_MODE_UNIFORM_BLOCK, data, required_size);
  if (pixel_stage) GX2SetPixelUniformBlock(slot, required_size, data);
  else GX2SetVertexUniformBlock(slot, required_size, data);
  return true;
}

static void bind_program_uniform_blocks(GLProgram *prog) {
  if (!g_gl_context || !prog) return;

  for (uint32_t i = 0; i < prog->uniform_block_count; ++i) {
    ProgramUniformBlock *block = &prog->uniform_blocks[i];
    gl_uniform_buffer_binding_t *binding = NULL;

    if (!block->implicit && block->binding_point < GL33_MAX_UNIFORM_BUFFER_BINDINGS) {
      binding = &g_gl_context->uniform_buffer_bindings[block->binding_point];
    }

    if (block->vertex_block_index >= 0) {
      uint32_t gx2_location = block->vertex_location;
      if (binding && binding->buffer != 0) {
        GLsizeiptr total_size = gl_buffer_get_size(binding->buffer);
        uint32_t bind_offset = binding->whole_buffer ? 0u : (uint32_t)binding->offset;
        uint32_t bind_size = total_size > (GLsizeiptr)bind_offset
                                 ? (binding->whole_buffer
                                        ? (uint32_t)(total_size - (GLsizeiptr)bind_offset)
                                        : (uint32_t)binding->size)
                                 : 0u;
        bind_uniform_block_buffer(binding->buffer, bind_offset, bind_size, block->vertex_size,
                                  false, gx2_location);
      } else if (prog->vs_blocks && (uint32_t)block->vertex_block_index < prog->vs_block_count &&
                 prog->vs_blocks[block->vertex_block_index].buffer) {
        GX2SetVertexUniformBlock(gx2_location, prog->vs_blocks[block->vertex_block_index].size,
                                 prog->vs_blocks[block->vertex_block_index].buffer);
      }
    }

    if (block->pixel_block_index >= 0) {
      uint32_t gx2_location = block->pixel_location;
      if (binding && binding->buffer != 0) {
        GLsizeiptr total_size = gl_buffer_get_size(binding->buffer);
        uint32_t bind_offset = binding->whole_buffer ? 0u : (uint32_t)binding->offset;
        uint32_t bind_size = total_size > (GLsizeiptr)bind_offset
                                 ? (binding->whole_buffer
                                        ? (uint32_t)(total_size - (GLsizeiptr)bind_offset)
                                        : (uint32_t)binding->size)
                                 : 0u;
        bind_uniform_block_buffer(binding->buffer, bind_offset, bind_size, block->pixel_size,
                                  true, gx2_location);
      } else if (prog->ps_blocks && (uint32_t)block->pixel_block_index < prog->ps_block_count &&
                 prog->ps_blocks[block->pixel_block_index].buffer) {
        GX2SetPixelUniformBlock(gx2_location, prog->ps_blocks[block->pixel_block_index].size,
                                prog->ps_blocks[block->pixel_block_index].buffer);
      }
    }
  }

  if (stage_uses_virtual_uniform_block(prog, false) &&
      prog->vs_direct_uniform_shadow &&
      prog->vs_direct_uniform_shadow_size > 0) {
    DCFlushRange(prog->vs_direct_uniform_shadow,
                 prog->vs_direct_uniform_shadow_size);
    GX2Invalidate(GX2_INVALIDATE_MODE_UNIFORM_BLOCK,
                  prog->vs_direct_uniform_shadow,
                  prog->vs_direct_uniform_shadow_size);
    GX2SetVertexUniformBlock(GX2GL_CAFEGLSL_VIRTUAL_UNIFORM_BLOCK_LOCATION,
                             prog->vs_direct_uniform_shadow_size,
                             prog->vs_direct_uniform_shadow);
  }
  if (stage_uses_virtual_uniform_block(prog, true) &&
      prog->ps_direct_uniform_shadow &&
      prog->ps_direct_uniform_shadow_size > 0) {
    DCFlushRange(prog->ps_direct_uniform_shadow,
                 prog->ps_direct_uniform_shadow_size);
    GX2Invalidate(GX2_INVALIDATE_MODE_UNIFORM_BLOCK,
                  prog->ps_direct_uniform_shadow,
                  prog->ps_direct_uniform_shadow_size);
    GX2SetPixelUniformBlock(GX2GL_CAFEGLSL_VIRTUAL_UNIFORM_BLOCK_LOCATION,
                            prog->ps_direct_uniform_shadow_size,
                            prog->ps_direct_uniform_shadow);
  }
}

static void bind_direct_uniform_registers(const uint8_t *shadow,
                                          uint32_t shadow_size,
                                          bool pixel_stage) {
  uint32_t stack_words[64];
  uint32_t *words;
  uint32_t word_count;

  if (!shadow || shadow_size == 0) return;
  word_count = shadow_size / sizeof(uint32_t);
  words = word_count <= (uint32_t)(sizeof(stack_words) / sizeof(stack_words[0]))
              ? stack_words
              : (uint32_t *)malloc(shadow_size);
  if (!words) return;

  for (uint32_t i = 0; i < word_count; ++i) {
    uint32_t word;
    memcpy(&word, shadow + i * sizeof(uint32_t), sizeof(word));
    words[i] = CPU_TO_GPU_32(word);
  }

  if (pixel_stage) {
    GX2SetPixelUniformReg(0, word_count, words);
  } else {
    GX2SetVertexUniformReg(0, word_count, words);
  }
  if (words != stack_words) free(words);
}

static uint32_t attrib_mask_from_size(GLint size) {
  switch (size) {
  case 1:
    return GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
  case 2:
    return GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
  case 3:
    return GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
  case 4:
    return GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
  default:
    return 0;
  }
}

static uint32_t gl_vertex_type_size(GLenum type) {
  switch (type) {
  case GL_FLOAT:
  case GL_UNSIGNED_INT:
  case GL_INT:
    return 4;
  case GL_HALF_FLOAT:
  case GL_UNSIGNED_SHORT:
  case GL_SHORT:
    return 2;
  case GL_UNSIGNED_BYTE:
  case GL_BYTE:
    return 1;
  default:
    return 0;
  }
}

static bool map_vertex_attrib_format(GLenum gl_type, GLboolean normalized,
                                     GLboolean integer_input, GLint size,
                                     GX2AttribFormat *format, GX2EndianSwapMode *endian_swap,
                                     uint32_t *mask) {
  uint32_t flags = 0;

  if (!format || !endian_swap || !mask) return false;

  *mask = attrib_mask_from_size(size);
  if (*mask == 0) return false;

  switch (gl_type) {
  case GL_FLOAT:
    *endian_swap = GX2_ENDIAN_SWAP_8_IN_32;
    switch (size) {
    case 1: *format = GX2_ATTRIB_FORMAT_FLOAT_32; return true;
    case 2: *format = GX2_ATTRIB_FORMAT_FLOAT_32_32; return true;
    case 3: *format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32; return true;
    case 4: *format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32; return true;
    default: return false;
    }
  case GL_HALF_FLOAT:
    *endian_swap = GX2_ENDIAN_SWAP_8_IN_16;
    switch (size) {
    case 1: *format = (GX2AttribFormat)(GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16_FLOAT); return true;
    case 2: *format = (GX2AttribFormat)(GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16_16_FLOAT); return true;
    case 4: *format = (GX2AttribFormat)(GX2_ATTRIB_FLAG_SCALED | GX2_ATTRIB_TYPE_16_16_16_16_FLOAT); return true;
    default: return false;
    }
  case GL_UNSIGNED_BYTE:
    *endian_swap = GX2_ENDIAN_SWAP_NONE;
    flags = integer_input ? GX2_ATTRIB_FLAG_INTEGER
                          : (normalized ? 0u : GX2_ATTRIB_FLAG_SCALED);
    switch (size) {
    case 1: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_8); return true;
    case 2: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_8_8); return true;
    case 4: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_8_8_8_8); return true;
    default: return false;
    }
  case GL_BYTE:
    *endian_swap = GX2_ENDIAN_SWAP_NONE;
    flags = GX2_ATTRIB_FLAG_SIGNED |
            (integer_input ? GX2_ATTRIB_FLAG_INTEGER
                           : (normalized ? 0u : GX2_ATTRIB_FLAG_SCALED));
    switch (size) {
    case 1: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_8); return true;
    case 2: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_8_8); return true;
    case 4: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_8_8_8_8); return true;
    default: return false;
    }
  case GL_UNSIGNED_SHORT:
    *endian_swap = GX2_ENDIAN_SWAP_8_IN_16;
    flags = integer_input ? GX2_ATTRIB_FLAG_INTEGER
                          : (normalized ? 0u : GX2_ATTRIB_FLAG_SCALED);
    switch (size) {
    case 1: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_16); return true;
    case 2: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_16_16); return true;
    case 4: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_16_16_16_16); return true;
    default:
      return false;
    }
  case GL_SHORT:
    *endian_swap = GX2_ENDIAN_SWAP_8_IN_16;
    flags = GX2_ATTRIB_FLAG_SIGNED |
            (integer_input ? GX2_ATTRIB_FLAG_INTEGER
                           : (normalized ? 0u : GX2_ATTRIB_FLAG_SCALED));
    switch (size) {
    case 1: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_16); return true;
    case 2: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_16_16); return true;
    case 4: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_16_16_16_16); return true;
    default:
      return false;
    }
  case GL_UNSIGNED_INT:
    if (size < 1 || size > 4) return false;
    *endian_swap = GX2_ENDIAN_SWAP_8_IN_32;
    flags = integer_input ? GX2_ATTRIB_FLAG_INTEGER
                          : (normalized ? 0u : GX2_ATTRIB_FLAG_SCALED);
    switch (size) {
    case 1: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_32); return true;
    case 2: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_32_32); return true;
    case 3: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_32_32_32); return true;
    case 4: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_32_32_32_32); return true;
    default: return false;
    }
  case GL_INT:
    if (size < 1 || size > 4) return false;
    *endian_swap = GX2_ENDIAN_SWAP_8_IN_32;
    flags = GX2_ATTRIB_FLAG_SIGNED |
            (integer_input ? GX2_ATTRIB_FLAG_INTEGER
                           : (normalized ? 0u : GX2_ATTRIB_FLAG_SCALED));
    switch (size) {
    case 1: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_32); return true;
    case 2: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_32_32); return true;
    case 3: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_32_32_32); return true;
    case 4: *format = (GX2AttribFormat)(flags | GX2_ATTRIB_TYPE_32_32_32_32); return true;
    default: return false;
    }
  default:
    return false;
  }
}

typedef struct {
  bool in_use;
  GLuint buffer_id;
  GX2RBuffer *gx2r;
  uint32_t gx2_index;
  uint32_t stride;
  uint32_t size;
  const uint8_t *data_ptr;
} ProgramFetchBufferBinding;

static bool ensure_dynamic_fetch_shader_storage(GLProgram *prog, uint32_t attrib_count) {
  uint32_t required_size;

  if (!prog) return false;

  required_size = GX2CalcFetchShaderSizeEx(attrib_count,
                                           GX2_FETCH_SHADER_TESSELLATION_NONE,
                                           GX2_TESSELLATION_MODE_DISCRETE);
  if (required_size == 0) return false;

  if (!prog->dynamic_fetch_shader_program || prog->dynamic_fetch_shader_program_size != required_size) {
    if (prog->dynamic_fetch_shader_program) {
      gl_mem_free(GL_MEM_TYPE_MEM2, prog->dynamic_fetch_shader_program);
      prog->dynamic_fetch_shader_program = NULL;
    }
    prog->dynamic_fetch_shader_program =
        gl_mem_alloc(GL_MEM_TYPE_MEM2, required_size, GX2_SHADER_PROGRAM_ALIGNMENT);
    if (!prog->dynamic_fetch_shader_program) {
      prog->dynamic_fetch_shader_program_size = 0;
      return false;
    }
    prog->dynamic_fetch_shader_program_size = required_size;
  }

  prog->dynamic_fetch_shader_attrib_count = attrib_count;
  return true;
}

void gl_bind_program_fetch_shader(void) {
  GLuint prog_id;
  GLProgram *prog;
  GX2AttribStream streams[GL33_MAX_VERTEX_ATTRIBS];
  ProgramFetchBufferBinding buffers[GL33_MAX_VERTEX_ATTRIBS];
  uint32_t attrib_count = 0;
  uint32_t buffer_count = 0;

  if (!g_gl_context) return;

  prog_id = g_gl_context->bound_program;
  if (!is_valid_program(prog_id)) return;

  prog = &g_programs[prog_id];
  if (!prog->group || !prog->linked || !prog->group->vertexShader) return;

  memset(streams, 0, sizeof(streams));
  memset(buffers, 0, sizeof(buffers));

  for (uint32_t slot = 0; slot < GL33_MAX_VERTEX_ATTRIBS; ++slot) {
    gl_vao_attrib_state_t state;
    ProgramAttribReflection *reflection;
    GX2AttribFormat format;
    GX2EndianSwapMode endian_swap;
    uint32_t mask;
    GX2RBuffer *gx2r;
    GLsizeiptr buffer_size;
    uint32_t component_size;
    uint32_t offset;
    uint32_t stride;
    uint32_t buffer_index = UINT32_MAX;

    if (!gl_vao_get_attrib_state(slot, &state) || !state.enabled) continue;

    reflection = &prog->attrib_reflection[slot];
    if (!reflection->active) continue;

    if (!state.buffer) {
      WiiU_Log("gx2gl: attribute %u (%s) has no bound buffer; skipping.", slot,
               reflection->name ? reflection->name : "<unnamed>");
      continue;
    }

    component_size = gl_vertex_type_size(state.type);
    if (component_size == 0 ||
        !map_vertex_attrib_format(state.type, state.normalized, state.integer_input,
                                  state.size, &format, &endian_swap, &mask)) {
      WiiU_Log("gx2gl: unsupported vertex format for attribute %u (%s): type=0x%04x normalized=%u size=%d.",
               slot, reflection->name ? reflection->name : "<unnamed>",
               (unsigned int)state.type, (unsigned int)state.normalized, (int)state.size);
      continue;
    }

    gx2r = gl_buffer_get_gx2r_buffer(state.buffer);
    if (!gx2r || !gx2r->buffer) {
      WiiU_Log("gx2gl: attribute %u (%s) references invalid buffer %u; skipping.", slot,
               reflection->name ? reflection->name : "<unnamed>", state.buffer);
      continue;
    }

    buffer_size = gl_buffer_get_size(state.buffer);
    offset = (uint32_t)(uintptr_t)state.pointer;
    stride = state.stride > 0 ? (uint32_t)state.stride : component_size * (uint32_t)state.size;
    if ((uint64_t)offset >= (uint64_t)buffer_size) {
      WiiU_Log("gx2gl: attribute %u (%s) offset %u exceeds buffer %u size %u; skipping.", slot,
               reflection->name ? reflection->name : "<unnamed>", offset, state.buffer, (unsigned int)buffer_size);
      continue;
    }

    for (uint32_t i = 0; i < buffer_count; ++i) {
      if (!buffers[i].in_use || buffers[i].buffer_id != state.buffer) continue;
      if (buffers[i].stride != stride) continue;
      buffer_index = buffers[i].gx2_index;
      break;
    }

    if (buffer_index == UINT32_MAX) {
      if (buffer_count >= GL33_MAX_VERTEX_ATTRIBS) {
        WiiU_Log("gx2gl: exhausted GX2 attribute buffer slots while binding attribute %u (%s).", slot,
                 reflection->name ? reflection->name : "<unnamed>");
        continue;
      }
      buffers[buffer_count].in_use = true;
      buffers[buffer_count].buffer_id = state.buffer;
      buffers[buffer_count].gx2r = gx2r;
      buffers[buffer_count].gx2_index = buffer_count;
      buffers[buffer_count].stride = stride;
      buffers[buffer_count].size = (uint32_t)buffer_size;
      buffers[buffer_count].data_ptr = (const uint8_t *)gx2r->buffer;
      buffer_index = buffers[buffer_count].gx2_index;
      ++buffer_count;
    }

    streams[attrib_count].location = slot;
    streams[attrib_count].buffer = buffer_index;
    streams[attrib_count].offset = offset;
    streams[attrib_count].format = format;
    streams[attrib_count].type = state.divisor > 0 ? GX2_ATTRIB_INDEX_PER_INSTANCE : GX2_ATTRIB_INDEX_PER_VERTEX;
    streams[attrib_count].aluDivisor = state.divisor;
    streams[attrib_count].mask = mask;
    streams[attrib_count].endianSwap = endian_swap;
    ++attrib_count;
  }

  if (!ensure_dynamic_fetch_shader_storage(prog, attrib_count)) {
    WiiU_Log("gx2gl: failed to allocate dynamic fetch shader storage for program %u.", prog_id);
    return;
  }

  memset(prog->dynamic_fetch_shader_program, 0, prog->dynamic_fetch_shader_program_size);
  GX2InitFetchShaderEx(&prog->dynamic_fetch_shader,
                       (uint8_t *)prog->dynamic_fetch_shader_program,
                       attrib_count,
                       streams,
                       GX2_FETCH_SHADER_TESSELLATION_NONE,
                       GX2_TESSELLATION_MODE_DISCRETE);
  DCFlushRange(prog->dynamic_fetch_shader_program, prog->dynamic_fetch_shader_program_size);
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER,
                prog->dynamic_fetch_shader_program,
                prog->dynamic_fetch_shader_program_size);
  GX2SetFetchShader(&prog->dynamic_fetch_shader);

  for (uint32_t i = 0; i < buffer_count; ++i) {
    if (!buffers[i].in_use) continue;
    DCFlushRange((void *)buffers[i].data_ptr, buffers[i].size);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,
                  (void *)buffers[i].data_ptr,
                  buffers[i].size);
    GX2SetAttribBuffer(buffers[i].gx2_index,
                       buffers[i].size,
                       buffers[i].stride,
                       buffers[i].data_ptr);
  }
}

static bool is_supported_shader_type(GLenum type) {
  return type == GL_VERTEX_SHADER || type == GL_FRAGMENT_SHADER;
}

static char *concatenate_shader_source(GLsizei count, const GLchar *const *strings,
                                       const GLint *lengths, uint32_t *out_length) {
  size_t total_length = 0;
  char *combined;
  size_t cursor = 0;

  if (!strings || count <= 0) return NULL;

  for (GLsizei i = 0; i < count; ++i) {
    size_t part_length;
    if (!strings[i]) return NULL;
    part_length = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
    total_length += part_length;
  }

  combined = (char *)malloc(total_length + 1u);
  if (!combined) return NULL;

  for (GLsizei i = 0; i < count; ++i) {
    size_t part_length = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
    memcpy(combined + cursor, strings[i], part_length);
    cursor += part_length;
  }
  combined[cursor] = '\0';

  if (out_length) {
    *out_length = (uint32_t)cursor;
  }
  return combined;
}

void gl_shader_init(void) {
  memset(g_shaders, 0, sizeof(g_shaders));
  memset(g_programs, 0, sizeof(g_programs));
  g_shader_mode_valid = false;
  g_shader_mode = GX2_SHADER_MODE_UNIFORM_REGISTER;
}

void gl_shader_shutdown(void) {
  for (uint32_t i = 1; i < MAX_PROGRAMS; ++i) {
    if (g_programs[i].in_use) {
      destroy_program_object(&g_programs[i]);
    }
  }
  for (uint32_t i = 1; i < MAX_SHADERS; ++i) {
    if (g_shaders[i].in_use) {
      destroy_shader_object(i);
    }
  }
  memset(g_programs, 0, sizeof(g_programs));
  memset(g_shaders, 0, sizeof(g_shaders));
  g_shader_mode_valid = false;
  g_shader_mode = GX2_SHADER_MODE_UNIFORM_REGISTER;
  gx2gl_cafeglsl_shutdown();
}
GLuint _gl_CreateShader(GLenum type) {
    if (!is_supported_shader_type(type)) {
      _gl_set_error(GL_INVALID_ENUM);
      return 0;
    }
    for (int i = 1; i < MAX_SHADERS; i++) {
      if (!g_shaders[i].in_use) {
        memset(&g_shaders[i], 0, sizeof(g_shaders[i]));
        g_shaders[i].in_use = true;
        g_shaders[i].type = type;
        return i;
      }
    }
    return 0;
}
void _gl_DeleteShader(GLuint s) {
  if (s == 0) return;
  if (!is_valid_shader(s)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  if (shader_is_attached_to_any_program(s)) {
    g_shaders[s].delete_pending = true;
    return;
  }

  destroy_shader_object(s);
}
GLboolean _gl_IsShader(GLuint s) { return is_valid_shader(s) ? GL_TRUE : GL_FALSE; }
void _gl_ShaderSource(GLuint s, GLsizei c, const GLchar* const* str, const GLint* l) {
  GLShader *shader;
  char *combined;
  uint32_t source_length = 0;

  if (!is_valid_shader(s)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (c < 0 || !str) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  shader = &g_shaders[s];
  combined = concatenate_shader_source(c, str, l, &source_length);
  if (!combined) {
    replace_owned_string(&shader->info_log, "Failed to store shader source.");
    return;
  }

  free(shader->source);
  shader->source = combined;
  shader->source_length = source_length;
  shader->source_present = true;
}
void _gl_CompileShader(GLuint s) {
  GLShader *shader;
  GLCompiledShader *new_binary;
  char info_log_buffer[GX2GL_INFO_LOG_CAPACITY];
  char *prepared_source = NULL;

  if (!is_valid_shader(s)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  shader = &g_shaders[s];
  shader->compile_requested = true;
  shader->compile_succeeded = false;
  free_shader_binary(shader);
  memset(info_log_buffer, 0, sizeof(info_log_buffer));

  if (!shader->source_present || !shader->source) {
    replace_owned_string(&shader->info_log, "No shader source was provided.");
    return;
  }

  prepared_source = gx2gl_prepare_shader_source_for_cafeglsl_ex(shader->source,
                                                                shader->type,
                                                                info_log_buffer,
                                                                (int)sizeof(info_log_buffer));
  if (!prepared_source) {
    if (info_log_buffer[0] == '\0') {
      snprintf(info_log_buffer, sizeof(info_log_buffer),
               "Failed to prepare shader source for CafeGLSL.");
    }
    replace_owned_string(&shader->info_log, info_log_buffer);
    return;
  }
  new_binary = create_compiled_shader(shader->type, shader->source);
  if (!new_binary) {
    replace_owned_string(&shader->info_log,
                         "Failed to allocate the compiled shader record.");
    free(prepared_source);
    return;
  }

  if (shader->type == GL_VERTEX_SHADER) {
    new_binary->vertex_shader =
        gx2gl_cafeglsl_compile_vertex_shader(prepared_source,
                                             info_log_buffer,
                                             (int)sizeof(info_log_buffer),
                                             GX2GL_CAFEGLSL_FLAG_NONE);
    shader->compile_succeeded = new_binary->vertex_shader != NULL;
    if (shader->compile_succeeded) {
      invalidate_shader_program_memory(new_binary->vertex_shader->program,
                                       new_binary->vertex_shader->size);
    }
  } else if (shader->type == GL_FRAGMENT_SHADER) {
    new_binary->pixel_shader =
        gx2gl_cafeglsl_compile_pixel_shader(prepared_source,
                                            info_log_buffer,
                                            (int)sizeof(info_log_buffer),
                                            GX2GL_CAFEGLSL_FLAG_NONE);
    shader->compile_succeeded = new_binary->pixel_shader != NULL;
    if (shader->compile_succeeded) {
      invalidate_shader_program_memory(new_binary->pixel_shader->program,
                                       new_binary->pixel_shader->size);
    }
  } else {
    snprintf(info_log_buffer, sizeof(info_log_buffer),
             "Geometry shaders are not supported by gx2gl yet.");
    shader->compile_succeeded = false;
  }

  if (shader->compile_succeeded) {
    shader->compiled_binary = new_binary;
    shader->compiled_vertex_shader = new_binary->vertex_shader;
    shader->compiled_pixel_shader = new_binary->pixel_shader;
  } else {
    release_compiled_shader(new_binary);
  }

  if (!shader->compile_succeeded && info_log_buffer[0] == '\0') {
    snprintf(info_log_buffer, sizeof(info_log_buffer),
             "CafeGLSL failed to compile the shader.");
  }

  replace_owned_string(&shader->info_log, info_log_buffer);
  free(prepared_source);
}
GLuint _gl_CreateProgram(void) {
    for (int i = 1; i < MAX_PROGRAMS; i++) {
      if (!g_programs[i].in_use) {
        memset(&g_programs[i], 0, sizeof(g_programs[i]));
        g_programs[i].in_use = true;
        g_programs[i].tf_pending_buffer_mode = GL_INTERLEAVED_ATTRIBS;
        g_programs[i].tf_linked_buffer_mode = GL_INTERLEAVED_ATTRIBS;
        memset(g_programs[i].vertex_sampler_units, -1, sizeof(g_programs[i].vertex_sampler_units));
        memset(g_programs[i].pixel_sampler_units, -1, sizeof(g_programs[i].pixel_sampler_units));
        return i;
      }
    }
    return 0;
}
void _gl_DeleteProgram(GLuint p) {
  GLuint attached_vertex_shader = 0;
  GLuint attached_pixel_shader = 0;
  GLuint attached_geometry_shader = 0;

  if (p == 0) return;
  if (!is_valid_program(p)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  attached_vertex_shader = g_programs[p].attached_vertex_shader;
  attached_pixel_shader = g_programs[p].attached_pixel_shader;
  attached_geometry_shader = g_programs[p].attached_geometry_shader;

  if (g_gl_context && g_gl_context->bound_program == p) {
    g_gl_context->bound_program = 0;
    g_gl_context->dirty_flags |= GL_DIRTY_PROGRAM | GL_DIRTY_TEXTURE_BINDINGS | GL_DIRTY_UNIFORM_BINDINGS;
  }

  destroy_program_object(&g_programs[p]);
  maybe_destroy_shader(attached_vertex_shader);
  maybe_destroy_shader(attached_pixel_shader);
  maybe_destroy_shader(attached_geometry_shader);
}
GLboolean _gl_IsProgram(GLuint p) { return is_valid_program(p) ? GL_TRUE : GL_FALSE; }
void _gl_AttachShader(GLuint p, GLuint s) {
  GLProgram *prog;
  GLShader *shader;

  if (!is_valid_program(p) || !is_valid_shader(s)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  prog = &g_programs[p];
  shader = &g_shaders[s];
  if (shader->type == GL_VERTEX_SHADER) {
    prog->attached_vertex_shader = s;
  } else if (shader->type == GL_FRAGMENT_SHADER) {
    prog->attached_pixel_shader = s;
  } else if (shader->type == GL_GEOMETRY_SHADER) {
    prog->attached_geometry_shader = s;
  } else {
    _gl_set_error(GL_INVALID_OPERATION);
  }
}
void _gl_DetachShader(GLuint p, GLuint s) {
  if (!is_valid_program(p) || !is_valid_shader(s)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  if (g_programs[p].attached_vertex_shader == s) {
    g_programs[p].attached_vertex_shader = 0;
  } else if (g_programs[p].attached_pixel_shader == s) {
    g_programs[p].attached_pixel_shader = 0;
  } else if (g_programs[p].attached_geometry_shader == s) {
    g_programs[p].attached_geometry_shader = 0;
  } else {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  maybe_destroy_shader(s);
}
void _gl_LinkProgram(GLuint p) {
  GLProgram *prog;
  GLShader *vertex_shader;
  GLShader *pixel_shader;
  char interface_error[512];
  char transform_feedback_error[512];

  if (!is_valid_program(p)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (gl_transform_feedback_program_active(p)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  prog = &g_programs[p];
  prog->link_attempted = true;
  replace_owned_string(&prog->info_log, "");
  prog->linked = false;
  prog->validated = false;

  if (!prog->attached_vertex_shader || !prog->attached_pixel_shader) {
    replace_owned_string(&prog->info_log,
                         "Program link requires one compiled vertex shader and one compiled fragment shader.");
    clear_program_runtime_state(prog);
    return;
  }

  if (prog->attached_geometry_shader) {
    replace_owned_string(&prog->info_log, "Geometry shaders are not supported by gx2gl yet.");
    clear_program_runtime_state(prog);
    return;
  }

  vertex_shader = &g_shaders[prog->attached_vertex_shader];
  pixel_shader = &g_shaders[prog->attached_pixel_shader];
  if (!vertex_shader->compile_succeeded || !vertex_shader->compiled_vertex_shader ||
      !pixel_shader->compile_succeeded || !pixel_shader->compiled_pixel_shader) {
    replace_owned_string(&prog->info_log,
                         "All attached shaders must compile successfully before linking.");
    clear_program_runtime_state(prog);
    return;
  }

  interface_error[0] = '\0';
  if (!gx2gl_validate_program_shader_interfaces(
                                                vertex_shader->compiled_binary->source,
                                                pixel_shader->compiled_binary->source,
                                                interface_error,
                                                sizeof(interface_error))) {
    replace_owned_string(
        &prog->info_log,
        interface_error[0]
            ? interface_error
            : "Shader interface validation failed for this program.");
    clear_program_runtime_state(prog);
    return;
  }

  memset(&prog->owned_group, 0, sizeof(prog->owned_group));
  prog->owned_group.vertexShader = vertex_shader->compiled_vertex_shader;
  prog->owned_group.pixelShader = pixel_shader->compiled_pixel_shader;

  if (!initialize_program_from_group(prog, &prog->owned_group, true, false)) {
    replace_owned_string(&prog->info_log,
                         "Failed to build the GX2 shader group or fetch shader for this program.");
    clear_program_runtime_state(prog);
    return;
  }

  retain_compiled_shader(vertex_shader->compiled_binary);
  retain_compiled_shader(pixel_shader->compiled_binary);
  prog->linked_vertex_binary = vertex_shader->compiled_binary;
  prog->linked_pixel_binary = pixel_shader->compiled_binary;

  transform_feedback_error[0] = '\0';
  if (!build_program_transform_feedback_link_state(
          prog, vertex_shader->compiled_binary->source, transform_feedback_error,
          sizeof(transform_feedback_error))) {
    replace_owned_string(
        &prog->info_log,
        transform_feedback_error[0]
            ? transform_feedback_error
            : "Failed to link transform feedback state for this program.");
    clear_program_runtime_state(prog);
    return;
  }

  replace_owned_string(&prog->info_log, "");
}
void _gl_UseProgram(GLuint p) {
  if (!g_gl_context) return;
  if (gl_transform_feedback_current_active_unpaused()) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (p != 0 && !is_valid_program(p)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (p != 0 && (!g_programs[p].linked || !g_programs[p].group)) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  g_gl_context->bound_program = p;
  g_gl_context->dirty_flags |= GL_DIRTY_PROGRAM | GL_DIRTY_TEXTURE_BINDINGS | GL_DIRTY_UNIFORM_BINDINGS;
}
void _gl_ValidateProgram(GLuint p) {
  GLProgram *prog;
  GX2VertexShader *vs;
  GX2PixelShader *ps;

  if (!is_valid_program(p)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  prog = &g_programs[p];
  prog->validated = prog->linked && prog->group != NULL;
  if (!prog->validated) {
    replace_owned_string(&prog->info_log, "Program validation failed because the program is not linked.");
    return;
  }

  vs = prog->group->vertexShader;
  ps = prog->group->pixelShader;
  if ((vs && has_sampler_binding_conflict(vs->samplerVars, vs->samplerVarCount,
                                          prog->vertex_sampler_bindings)) ||
      (ps && has_sampler_binding_conflict(ps->samplerVars, ps->samplerVarCount,
                                          prog->pixel_sampler_bindings))) {
    prog->validated = GL_FALSE;
    replace_owned_string(&prog->info_log,
                         "Program validation failed because samplers of different types share the same texture unit.");
  } else {
    replace_owned_string(&prog->info_log, "");
  }
}
void _gl_BindAttribLocation(GLuint p, GLuint i, const GLchar* n) {
  GLProgram *prog;

  if (!is_valid_program(p)) {
    _gl_set_error(is_valid_shader(p) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
    return;
  }
  if (i >= GL33_MAX_VERTEX_ATTRIBS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (!n) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (strncmp(n, "gl_", 3) == 0) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }

  prog = &g_programs[p];
  replace_owned_string(&prog->attrib_binding_names[i], n);
}
void _gl_GetAttachedShaders(GLuint p, GLsizei m, GLsizei* c, GLuint* s) {
  GLuint attached[3];
  GLsizei total = 0;
  GLsizei written = 0;

  if (!is_valid_program(p)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  if (g_programs[p].attached_vertex_shader) {
    attached[total++] = g_programs[p].attached_vertex_shader;
  }
  if (g_programs[p].attached_pixel_shader) {
    attached[total++] = g_programs[p].attached_pixel_shader;
  }
  if (g_programs[p].attached_geometry_shader) {
    attached[total++] = g_programs[p].attached_geometry_shader;
  }

  if (s && m > 0) {
    for (written = 0; written < total && written < m; ++written) {
      s[written] = attached[written];
    }
  }
  if (c) {
    *c = total;
  }
}
void _gl_GetActiveAttrib(GLuint p, GLuint i, GLsizei b, GLsizei* l, GLint* s, GLenum* t, GLchar* n) {
  GLProgram *prog;
  GX2VertexShader *vs;
  const GX2AttribVar *attrib;

  if (!is_valid_program(p)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  prog = &g_programs[p];
  if (!prog->group || !prog->group->vertexShader) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (i >= get_active_attribute_count(prog)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  vs = prog->group->vertexShader;
  attrib = &vs->attribVars[i];
  if (s) *s = (GLint)attrib->count;
  if (t) *t = shader_var_type_to_gl_enum(attrib->type);
  copy_gl_string(attrib->name, b, l, n);
}

void _gl_GetActiveUniform(GLuint p, GLuint i, GLsizei b, GLsizei* l, GLint* s, GLenum* t, GLchar* n) {
  GLProgram *prog;
  ActiveUniformInfo info;

  if (!is_valid_program(p)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  prog = &g_programs[p];
  if (!prog->group) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (!get_active_uniform_info(prog, i, &info)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  if (s) *s = info.size;
  if (t) *t = info.type;
  copy_gl_string(info.name, b, l, n);
}
void _gl_GetShaderiv(GLuint s, GLenum p, GLint *v) {
  if (!is_valid_shader(s) || !v) return;
  switch (p) {
  case GL_COMPILE_STATUS:  *v = g_shaders[s].compile_succeeded ? GL_TRUE : GL_FALSE; break;
  case GL_SHADER_TYPE:     *v = (GLint)g_shaders[s].type; break;
  case GL_DELETE_STATUS:   *v = g_shaders[s].delete_pending ? GL_TRUE : GL_FALSE; break;
  case GL_INFO_LOG_LENGTH: *v = g_shaders[s].info_log ? (GLint)strlen(g_shaders[s].info_log) + 1 : 0; break;
  case GL_SHADER_SOURCE_LENGTH: *v = g_shaders[s].source_present ? (GLint)g_shaders[s].source_length + 1 : 0; break;
  default: _gl_set_error(GL_INVALID_ENUM); break;
  }
}
void _gl_GetProgramiv(GLuint p, GLenum n, GLint *v) {
  if (!is_valid_program(p) || !v) return;
  GLProgram *prog = &g_programs[p];
  switch (n) {
  case GL_LINK_STATUS:          *v = prog->linked ? GL_TRUE : GL_FALSE; break;
  case GL_VALIDATE_STATUS:      *v = prog->validated ? GL_TRUE : GL_FALSE; break;
  case GL_DELETE_STATUS:        *v = prog->delete_pending ? GL_TRUE : GL_FALSE; break;
  case GL_INFO_LOG_LENGTH:      *v = prog->info_log ? (GLint)strlen(prog->info_log) + 1 : 0; break;
  case GL_ATTACHED_SHADERS:     *v = (prog->attached_vertex_shader ? 1 : 0) + (prog->attached_pixel_shader ? 1 : 0) + (prog->attached_geometry_shader ? 1 : 0); break;
  case GL_ACTIVE_UNIFORMS:      *v = (GLint)get_active_uniform_count(prog); break;
  case GL_ACTIVE_ATTRIBUTES:    *v = (GLint)get_active_attribute_count(prog); break;
  case GL_ACTIVE_UNIFORM_MAX_LENGTH: *v = (GLint)get_active_uniform_max_name_length(prog); break;
  case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH: *v = (GLint)get_active_attribute_max_name_length(prog); break;
  case GL_TRANSFORM_FEEDBACK_BUFFER_MODE: *v = (GLint)prog->tf_linked_buffer_mode; break;
  case GL_TRANSFORM_FEEDBACK_VARYINGS: *v = (GLint)prog->tf_linked_count; break;
  case GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH: *v = (GLint)prog->tf_linked_max_name_length; break;
  default: _gl_set_error(GL_INVALID_ENUM); break;
  }
}
void _gl_GetShaderInfoLog(GLuint s, GLsizei m, GLsizei *l, GLchar *i) {
  if (!is_valid_shader(s)) return;
  copy_gl_string(g_shaders[s].info_log, m, l, i);
}
void _gl_GetProgramInfoLog(GLuint p, GLsizei m, GLsizei *l, GLchar *i) {
  if (!is_valid_program(p)) return;
  copy_gl_string(g_programs[p].info_log, m, l, i);
}
void _gl_GetShaderSource(GLuint s, GLsizei b, GLsizei *l, GLchar *src) {
  if (!is_valid_shader(s)) return;
  copy_gl_string(g_shaders[s].source, b, l, src);
}
void _gl_GetUniformfv(GLuint p, GLint l, GLfloat *v) {
  uint32_t words[16];
  uint32_t actual_words = 0;

  if (!is_valid_program(p) || !v) return;
  if (l == -1) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  l = resolve_uniform_location(&g_programs[p], l);
  if (location_is_sampler(l)) {
    *v = (GLfloat)get_sampler_uniform_binding(&g_programs[p], l);
    return;
  }
  if (get_program_uniform_value_shadow(&g_programs[p], l, words,
                                       (uint32_t)(sizeof(words) / sizeof(words[0])),
                                       &actual_words)) {
    for (uint32_t i = 0; i < actual_words; ++i) {
      memcpy(&v[i], &words[i], sizeof(GLfloat));
    }
    return;
  }
  if (read_uniform_words(&g_programs[p], l, words,
                         (uint32_t)(sizeof(words) / sizeof(words[0])),
                         &actual_words, NULL)) {
    for (uint32_t i = 0; i < actual_words; ++i) {
      memcpy(&v[i], &words[i], sizeof(GLfloat));
    }
    return;
  }
  _gl_set_error(GL_INVALID_OPERATION);
}

void _gl_GetUniformiv(GLuint p, GLint l, GLint *v) {
  uint32_t words[16];
  uint32_t actual_words = 0;

  if (!is_valid_program(p) || !v) return;
  if (l == -1) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  l = resolve_uniform_location(&g_programs[p], l);
  if (location_is_sampler(l)) {
    *v = get_sampler_uniform_binding(&g_programs[p], l);
    return;
  }
  if (get_program_uniform_value_shadow(&g_programs[p], l, words,
                                       (uint32_t)(sizeof(words) / sizeof(words[0])),
                                       &actual_words)) {
    for (uint32_t i = 0; i < actual_words; ++i) {
      v[i] = (GLint)words[i];
    }
    return;
  }
  if (read_uniform_words(&g_programs[p], l, words,
                         (uint32_t)(sizeof(words) / sizeof(words[0])),
                         &actual_words, NULL)) {
    for (uint32_t i = 0; i < actual_words; ++i) {
      v[i] = (GLint)words[i];
    }
    return;
  }
  _gl_set_error(GL_INVALID_OPERATION);
}

void _gl_GetUniformuiv(GLuint program, GLint location, GLuint *params) {
  uint32_t words[16];
  uint32_t actual_words = 0;

  if (!is_valid_program(program) || !params) return;
  if (location == -1) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  location = resolve_uniform_location(&g_programs[program], location);
  if (location_is_sampler(location)) {
    *params = (GLuint)get_sampler_uniform_binding(&g_programs[program], location);
    return;
  }
  if (get_program_uniform_value_shadow(&g_programs[program], location, words,
                                       (uint32_t)(sizeof(words) / sizeof(words[0])),
                                       &actual_words)) {
    for (uint32_t i = 0; i < actual_words; ++i) {
      params[i] = words[i];
    }
    return;
  }
  if (read_uniform_words(&g_programs[program], location, words,
                         (uint32_t)(sizeof(words) / sizeof(words[0])),
                         &actual_words, NULL)) {
    for (uint32_t i = 0; i < actual_words; ++i) {
      params[i] = words[i];
    }
    return;
  }
  _gl_set_error(GL_INVALID_OPERATION);
}
GLuint _gl_GetUniformBlockIndex(GLuint p, const GLchar *n) {
  GLProgram *prog;
  ProgramUniformBlock *block;

  if (!is_valid_program(p) || !n) return GL_INVALID_INDEX;
  prog = &g_programs[p];
  block = find_program_uniform_block(prog, n);
  if (!block) return GL_INVALID_INDEX;
  return (GLuint)(block - prog->uniform_blocks);
}

void _gl_UniformBlockBinding(GLuint p, GLuint i, GLuint b) {
  GLProgram *prog;

  if (!is_valid_program(p)) return;
  prog = &g_programs[p];
  if (i >= prog->uniform_block_count) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (b >= GL33_MAX_UNIFORM_BUFFER_BINDINGS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  prog->uniform_blocks[i].binding_point = b;
  if (g_gl_context && g_gl_context->bound_program == p) {
    g_gl_context->dirty_flags |= GL_DIRTY_UNIFORM_BINDINGS;
  }
}
void _gl_WiiULoadShaderGroup(GLuint p, const void *g) {
  if (!is_valid_program(p) || !g) return;
  g_programs[p].link_attempted = true;
  if (!initialize_program_from_group(&g_programs[p], (const WHBGfxShaderGroup *)g, false, false)) {
    replace_owned_string(&g_programs[p].info_log, "Failed to initialize the supplied Wii U shader group.");
  } else {
    replace_owned_string(&g_programs[p].info_log, "");
  }
}
void _gl_WiiULoadShaderGroupGFD(GLuint p, GLuint idx, const void *d) {
  WHBGfxShaderGroup loaded_group;

  if (!is_valid_program(p) || !d) return;

  g_programs[p].link_attempted = true;
  memset(&loaded_group, 0, sizeof(loaded_group));
  if (!WHBGfxLoadGFDShaderGroup(&loaded_group, idx, d)) {
    replace_owned_string(&g_programs[p].info_log,
                         "Failed to load CafeGLSL .gsh shader group with WHBGfxLoadGFDShaderGroup.");
    clear_program_runtime_state(&g_programs[p]);
    return;
  }

  if (!initialize_program_from_group(&g_programs[p], &loaded_group, true, true)) {
    replace_owned_string(&g_programs[p].info_log,
                         "Failed to initialize the loaded CafeGLSL .gsh shader group.");
    clear_program_runtime_state(&g_programs[p]);
    return;
  }

  replace_owned_string(&g_programs[p].info_log, "");
}

GLint _gl_GetUniformLocation(GLuint p, const GLchar *name) {
  uint32_t count;

  if (!is_valid_program(p) || !name) return -1;
  GLProgram *prog = &g_programs[p];
  if (!prog->group) return -1;

  count = get_active_uniform_count(prog);
  for (uint32_t i = 0; i < count; ++i) {
    ActiveUniformInfo info;
    if (!get_active_uniform_info(prog, i, &info) || !info.name) {
      continue;
    }
    if (strcmp(info.name, name) == 0) {
      return (GLint)i + 1;
    }
    size_t info_name_length = strlen(info.name);
    size_t query_name_length = strlen(name);
    if (query_name_length == info_name_length + 3u &&
        strncmp(info.name, name, info_name_length) == 0 &&
        strcmp(name + info_name_length, "[0]") == 0) {
      return (GLint)i + 1;
    }
  }
  return -1;
}
GLint _gl_GetAttribLocation(GLuint p, const GLchar *name) {
  if (!is_valid_program(p) || !name) return -1;
  GLProgram *prog = &g_programs[p];
  if (!prog->group || !prog->group->vertexShader) return -1;
  GX2VertexShader *vs = prog->group->vertexShader;
  for (uint32_t i = 0; i < vs->attribVarCount; i++)
    if (vs->attribVars[i].name && strcmp(vs->attribVars[i].name, name) == 0)
      return (GLint)vs->attribVars[i].location;
  return -1;
}
void _gl_ReleaseShaderCompiler(void) { gx2gl_cafeglsl_shutdown(); }
void _gl_ShaderBinary(GLsizei c, const GLuint* s, GLenum f, const GLvoid* b, GLsizei l) {
  (void)c;
  (void)s;
  (void)b;
  (void)l;
  if (f != 0) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  _gl_set_error(GL_INVALID_OPERATION);
}
void _gl_GetShaderPrecisionFormat(GLenum s, GLenum p, GLint* r, GLint* pr) {
  bool valid_shader_type = s == GL_VERTEX_SHADER || s == GL_FRAGMENT_SHADER;
  bool valid_precision_type =
      p == GL_LOW_FLOAT || p == GL_MEDIUM_FLOAT || p == GL_HIGH_FLOAT ||
      p == GL_LOW_INT || p == GL_MEDIUM_INT || p == GL_HIGH_INT;

  if (!valid_shader_type || !valid_precision_type) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }

  if (r) {
    if (p == GL_LOW_INT || p == GL_MEDIUM_INT || p == GL_HIGH_INT) {
      r[0] = 31;
      r[1] = 30;
    } else {
      r[0] = 127;
      r[1] = 127;
    }
  }
  if (pr) {
    *pr = (p == GL_LOW_INT || p == GL_MEDIUM_INT || p == GL_HIGH_INT) ? 0 : 23;
  }
}
void _gl_Uniform1f(GLint l, GLfloat v) { update_uniform_words(l, (uint32_t*)&v, 1, 0); }
void _gl_Uniform1fv(GLint l, GLsizei c, const GLfloat *v) { update_uniform_words(l, (uint32_t*)v, c, 0); }
void _gl_Uniform1i(GLint l, GLint v) {
  if (update_sampler_bindings(l, 1, &v)) return;
  update_uniform_words(l, (uint32_t*)&v, 1, 0);
}
void _gl_Uniform1iv(GLint l, GLsizei c, const GLint *v) {
  if (update_sampler_bindings(l, c, v)) return;
  update_uniform_words(l, (uint32_t*)v, c, 0);
}
void _gl_Uniform2f(GLint l, GLfloat v0, GLfloat v1) { GLfloat d[2]={v0,v1}; update_uniform_words(l, (uint32_t*)d, 2, 0); }
void _gl_Uniform2fv(GLint l, GLsizei c, const GLfloat *v) { update_uniform_words(l, (uint32_t*)v, c*2, 0); }
void _gl_Uniform2i(GLint l, GLint v0, GLint v1) { GLint d[2]={v0,v1}; update_uniform_words(l, (uint32_t*)d, 2, 0); }
void _gl_Uniform2iv(GLint l, GLsizei c, const GLint *v) { update_uniform_words(l, (uint32_t*)v, c*2, 0); }
void _gl_Uniform3f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2) { GLfloat d[3]={v0,v1,v2}; update_uniform_words(l, (uint32_t*)d, 3, 0); }
void _gl_Uniform3fv(GLint l, GLsizei c, const GLfloat *v) { update_uniform_words(l, (uint32_t*)v, c*3, 0); }
void _gl_Uniform3i(GLint l, GLint v0, GLint v1, GLint v2) { GLint d[3]={v0,v1,v2}; update_uniform_words(l, (uint32_t*)d, 3, 0); }
void _gl_Uniform3iv(GLint l, GLsizei c, const GLint *v) { update_uniform_words(l, (uint32_t*)v, c*3, 0); }
void _gl_Uniform4f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { GLfloat d[4]={v0,v1,v2,v3}; update_uniform_words(l, (uint32_t*)d, 4, 0); }
void _gl_Uniform4fv(GLint l, GLsizei c, const GLfloat *v) { update_uniform_words(l, (uint32_t*)v, c*4, 0); }
void _gl_Uniform4i(GLint l, GLint v0, GLint v1, GLint v2, GLint v3) { GLint d[4]={v0,v1,v2,v3}; update_uniform_words(l, (uint32_t*)d, 4, 0); }
void _gl_Uniform4iv(GLint l, GLsizei c, const GLint *v) { update_uniform_words(l, (uint32_t*)v, c*4, 0); }
void _gl_Uniform1ui(GLint l, GLuint v) { update_uniform_words(l, &v, 1, 0); }
void _gl_Uniform2ui(GLint l, GLuint v0, GLuint v1) { GLuint d[2]={v0,v1}; update_uniform_words(l, d, 2, 0); }
void _gl_Uniform3ui(GLint l, GLuint v0, GLuint v1, GLuint v2) { GLuint d[3]={v0,v1,v2}; update_uniform_words(l, d, 3, 0); }
void _gl_Uniform4ui(GLint l, GLuint v0, GLuint v1, GLuint v2, GLuint v3) { GLuint d[4]={v0,v1,v2,v3}; update_uniform_words(l, d, 4, 0); }
void _gl_Uniform1uiv(GLint l, GLsizei c, const GLuint *v) { update_uniform_words(l, v, c, 0); }
void _gl_Uniform2uiv(GLint l, GLsizei c, const GLuint *v) { update_uniform_words(l, v, c*2, 0); }
void _gl_Uniform3uiv(GLint l, GLsizei c, const GLuint *v) { update_uniform_words(l, v, c*3, 0); }
void _gl_Uniform4uiv(GLint l, GLsizei c, const GLuint *v) {
    update_uniform_words(l, v, c * 4, 0);
}

void _gl_UniformMatrix2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*4, 0); }
void _gl_UniformMatrix3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*9, 0); }
void _gl_UniformMatrix4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*16, 0); }
void _gl_UniformMatrix2x3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*6, 0); }
void _gl_UniformMatrix3x2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*6, 0); }
void _gl_UniformMatrix2x4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*8, 0); }
void _gl_UniformMatrix4x2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*8, 0); }
void _gl_UniformMatrix3x4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*12, 0); }
void _gl_UniformMatrix4x3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)t; update_uniform_words(l, (uint32_t*)v, c*12, 0); }

void _gl_GetActiveUniformBlockiv(GLuint p, GLuint i, GLenum n, GLint *v) {
  GLProgram *prog;
  ProgramUniformBlock *block;

  if (!v) return;
  if (!is_valid_program(p)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  prog = &g_programs[p];
  if (i >= prog->uniform_block_count) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  block = &prog->uniform_blocks[i];
  switch (n) {
  case GL_UNIFORM_BLOCK_BINDING:
    *v = (GLint)block->binding_point;
    break;
  case GL_UNIFORM_BLOCK_DATA_SIZE:
    *v = (GLint)((block->vertex_size > block->pixel_size) ? block->vertex_size : block->pixel_size);
    break;
  case GL_UNIFORM_BLOCK_NAME_LENGTH:
    *v = block->name ? (GLint)strlen(block->name) + 1 : 0;
    break;
  case GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER:
    *v = block->vertex_block_index >= 0 ? GL_TRUE : GL_FALSE;
    break;
  case GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER:
    *v = block->pixel_block_index >= 0 ? GL_TRUE : GL_FALSE;
    break;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    break;
  }
}
void _gl_GetActiveUniformBlockName(GLuint p, GLuint i, GLsizei s, GLsizei *l, GLchar *n) {
  GLProgram *prog;
  const char *name;
  GLsizei len;

  if (!is_valid_program(p) || s < 0 || (s > 0 && !n)) return;
  prog = &g_programs[p];
  if (i >= prog->uniform_block_count) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  name = prog->uniform_blocks[i].name ? prog->uniform_blocks[i].name : "";
  len = (GLsizei)strlen(name);
  if (l) *l = len;
  if (s > 0) {
    GLsizei copy = len < (s - 1) ? len : (s - 1);
    memcpy(n, name, (size_t)copy);
    n[copy] = '\0';
  }
}
void _gl_GetActiveUniformsiv(GLuint p, GLsizei c, const GLuint *i, GLenum n, GLint *v) {
  GLProgram *prog;

  if (!v || !i) return;
  if (!is_valid_program(p) || c < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  prog = &g_programs[p];
  for (GLsizei j = 0; j < c; ++j) {
    ActiveUniformInfo info;
    if (!get_active_uniform_info(prog, i[j], &info)) {
      _gl_set_error(GL_INVALID_VALUE);
      return;
    }
    switch (n) {
    case GL_UNIFORM_TYPE:
      v[j] = (GLint)info.type;
      break;
    case GL_UNIFORM_SIZE:
      v[j] = info.size;
      break;
    case GL_UNIFORM_NAME_LENGTH:
      v[j] = info.name ? (GLint)strlen(info.name) + 1 : 0;
      break;
    case GL_UNIFORM_BLOCK_INDEX:
      v[j] = info.block_index;
      break;
    case GL_UNIFORM_OFFSET:
      v[j] = info.offset;
      break;
    default:
      _gl_set_error(GL_INVALID_ENUM);
      return;
    }
  }
}

void _gl_GetActiveUniformName(GLuint p, GLuint i, GLsizei s, GLsizei *l, GLchar *n) {
  GLProgram *prog;
  ActiveUniformInfo info;

  if (!is_valid_program(p) || s < 0 || (s > 0 && !n)) {
    if (is_valid_program(p)) _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  prog = &g_programs[p];
  if (!get_active_uniform_info(prog, i, &info)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  copy_gl_string(info.name, s, l, n);
}

void _gl_TransformFeedbackVaryings(GLuint program, GLsizei count,
                                    const GLchar *const *varyings,
                                    GLenum bufferMode) {
  GLProgram *prog;
  char **names = NULL;

  if (!is_valid_program(program)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (count < 0 || (count > 0 && !varyings)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (bufferMode != GL_INTERLEAVED_ATTRIBS &&
      bufferMode != GL_SEPARATE_ATTRIBS) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (bufferMode == GL_SEPARATE_ATTRIBS &&
      count > GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  if (count > 0) {
    names = (char **)calloc((size_t)count, sizeof(char *));
    if (!names) {
      _gl_set_error(GL_OUT_OF_MEMORY);
      return;
    }
    for (GLsizei i = 0; i < count; ++i) {
      if (!varyings[i]) {
        for (GLsizei j = 0; j < i; ++j) free(names[j]);
        free(names);
        _gl_set_error(GL_INVALID_VALUE);
        return;
      }
      names[i] = strdup(varyings[i]);
      if (!names[i]) {
        for (GLsizei j = 0; j < i; ++j) free(names[j]);
        free(names);
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
      }
    }
  }

  prog = &g_programs[program];
  free_program_transform_feedback_pending(prog);
  prog->tf_pending_varyings = names;
  prog->tf_pending_count = (uint32_t)count;
  prog->tf_pending_buffer_mode = bufferMode;
}

void _gl_GetTransformFeedbackVarying(GLuint program, GLuint index,
                                     GLsizei bufSize, GLsizei *length,
                                     GLsizei *size, GLenum *type,
                                     GLchar *name) {
  GLProgram *prog;
  ProgramTransformFeedbackVarying *varying;

  if (!is_valid_program(program)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (bufSize < 0 || (bufSize > 0 && !name)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  prog = &g_programs[program];
  if (!prog->link_attempted) {
    _gl_set_error(GL_INVALID_OPERATION);
    return;
  }
  if (index >= prog->tf_linked_count) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }

  varying = &prog->tf_linked_varyings[index];
  if (size) *size = varying->size;
  if (type) *type = varying->type;
  copy_gl_string(varying->name, bufSize, length, name);
}

GLboolean gl_program_transform_feedback_ready(GLuint program) {
  GLProgram *prog;

  if (!is_valid_program(program)) return GL_FALSE;
  prog = &g_programs[program];
  return (prog->linked && prog->tf_stream_out_ready &&
          prog->tf_required_binding_count > 0)
             ? GL_TRUE
             : GL_FALSE;
}

GLuint gl_program_transform_feedback_binding_count(GLuint program) {
  if (!is_valid_program(program) || !g_programs[program].linked) return 0;
  return g_programs[program].tf_required_binding_count;
}

GLenum gl_program_transform_feedback_buffer_mode(GLuint program) {
  if (!is_valid_program(program)) return GL_INTERLEAVED_ATTRIBS;
  return g_programs[program].tf_linked_buffer_mode;
}

void gl_bind_shaders(void) {
  if (!g_gl_context) return;
  GLuint prog_id = g_gl_context->bound_program;
  if (!is_valid_program(prog_id)) return;
  GLProgram *prog = &g_programs[prog_id];
  if (!prog->group || !prog->linked) return;
  GX2ShaderMode shader_mode = choose_shader_mode(prog);
  if (!g_shader_mode_valid || g_shader_mode != shader_mode) {
    GX2SetShaderMode(shader_mode);
    g_shader_mode = shader_mode;
    g_shader_mode_valid = true;
  }
  if (prog->group->vertexShader) GX2SetVertexShader(prog->group->vertexShader);
  if (prog->group->pixelShader)  GX2SetPixelShader(prog->group->pixelShader);
  if (shader_mode == GX2_SHADER_MODE_UNIFORM_BLOCK ||
      shader_mode == GX2_SHADER_MODE_GEOMETRY_SHADER) {
    GX2Invalidate(GX2_INVALIDATE_MODE_SHADER, NULL, 0xFFFFFFFFu);
  }
  if (!stage_uses_virtual_uniform_block(prog, false)) {
    bind_direct_uniform_registers(prog->vs_direct_uniform_shadow,
                                  prog->vs_direct_uniform_shadow_size, false);
  }
  if (!stage_uses_virtual_uniform_block(prog, true)) {
    bind_direct_uniform_registers(prog->ps_direct_uniform_shadow,
                                  prog->ps_direct_uniform_shadow_size, true);
  }
  bind_program_uniform_blocks(prog);
}

void gl_bind_program_textures(void) {
  GLuint prog_id;
  GLProgram *prog;

  if (!g_gl_context) return;
  prog_id = g_gl_context->bound_program;
  if (!is_valid_program(prog_id)) return;

  prog = &g_programs[prog_id];
  if (!prog->group || !prog->linked) return;

  bind_stage_samplers(prog->pixel_sampler_units, prog->pixel_sampler_bindings,
                      prog->pixel_sampler_targets, true);
  bind_stage_samplers(prog->vertex_sampler_units, prog->vertex_sampler_bindings,
                      prog->vertex_sampler_targets, false);
}

#ifdef __cplusplus
}
#endif
