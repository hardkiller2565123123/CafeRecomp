#include "gl_context.h"
#include "gl_framebuffer.h"
#include "gl_vao.h"

#include <stdint.h>
#include <string.h>

#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_MAX_VERTEX_UNIFORM_BLOCKS
#define GL_MAX_VERTEX_UNIFORM_BLOCKS 0x8A2B
#endif
#ifndef GL_MAX_FRAGMENT_UNIFORM_BLOCKS
#define GL_MAX_FRAGMENT_UNIFORM_BLOCKS 0x8A2D
#endif
#ifndef GL_MAX_COMBINED_UNIFORM_BLOCKS
#define GL_MAX_COMBINED_UNIFORM_BLOCKS 0x8A2E
#endif
#ifndef GL_MAX_UNIFORM_BLOCK_SIZE
#define GL_MAX_UNIFORM_BLOCK_SIZE 0x8A30
#endif
#ifndef GL_MAX_VERTEX_UNIFORM_COMPONENTS
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS 0x8B4A
#endif
#ifndef GL_MAX_FRAGMENT_UNIFORM_COMPONENTS
#define GL_MAX_FRAGMENT_UNIFORM_COMPONENTS 0x8B49
#endif
#ifndef GL_MAX_VARYING_COMPONENTS
#define GL_MAX_VARYING_COMPONENTS 0x8B4B
#endif
#ifndef GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS 0x8B4C
#endif
#ifndef GL_MAX_3D_TEXTURE_SIZE
#define GL_MAX_3D_TEXTURE_SIZE 0x8073
#endif
#ifndef GL_MAX_CUBE_MAP_TEXTURE_SIZE
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE 0x851C
#endif
#ifndef GL_MAX_TEXTURE_LOD_BIAS
#define GL_MAX_TEXTURE_LOD_BIAS 0x84FD
#endif
#ifndef GL_MAX_VIEWPORT_DIMS
#define GL_MAX_VIEWPORT_DIMS 0x0D3A
#endif
#ifndef GL_ALIASED_POINT_SIZE_RANGE
#define GL_ALIASED_POINT_SIZE_RANGE 0x846D
#endif
#ifndef GL_ALIASED_LINE_WIDTH_RANGE
#define GL_ALIASED_LINE_WIDTH_RANGE 0x846E
#endif
#ifndef GL_POINT_SIZE
#define GL_POINT_SIZE 0x0B11
#endif
#ifndef GL_POINT_SIZE_RANGE
#define GL_POINT_SIZE_RANGE 0x0B12
#endif
#ifndef GL_POLYGON_OFFSET_FACTOR
#define GL_POLYGON_OFFSET_FACTOR 0x8038
#endif
#ifndef GL_POLYGON_OFFSET_UNITS
#define GL_POLYGON_OFFSET_UNITS 0x2A00
#endif
#ifndef GL_DEPTH_FUNC
#define GL_DEPTH_FUNC 0x0B74
#endif
#ifndef GL_BLEND_SRC
#define GL_BLEND_SRC 0x0BE1
#endif
#ifndef GL_BLEND_DST
#define GL_BLEND_DST 0x0BE0
#endif
#ifndef GL_BLEND_SRC_RGB
#define GL_BLEND_SRC_RGB 0x80C9
#endif
#ifndef GL_BLEND_DST_RGB
#define GL_BLEND_DST_RGB 0x80C8
#endif
#ifndef GL_BLEND_SRC_ALPHA
#define GL_BLEND_SRC_ALPHA 0x80CB
#endif
#ifndef GL_BLEND_DST_ALPHA
#define GL_BLEND_DST_ALPHA 0x80CA
#endif
#ifndef GL_BLEND_EQUATION_ALPHA
#define GL_BLEND_EQUATION_ALPHA 0x883D
#endif
#ifndef GL_CULL_FACE_MODE
#define GL_CULL_FACE_MODE 0x0B45
#endif
#ifndef GL_FRONT_FACE
#define GL_FRONT_FACE 0x0B46
#endif
#ifndef GL_POLYGON_MODE
#define GL_POLYGON_MODE 0x0B40
#endif
#ifndef GL_LOGIC_OP_MODE
#define GL_LOGIC_OP_MODE 0x0BF0
#endif
#ifndef GL_STENCIL_FUNC
#define GL_STENCIL_FUNC 0x0B92
#endif
#ifndef GL_STENCIL_VALUE_MASK
#define GL_STENCIL_VALUE_MASK 0x0B93
#endif
#ifndef GL_STENCIL_REF
#define GL_STENCIL_REF 0x0B97
#endif
#ifndef GL_STENCIL_FAIL
#define GL_STENCIL_FAIL 0x0B94
#endif
#ifndef GL_STENCIL_PASS_DEPTH_FAIL
#define GL_STENCIL_PASS_DEPTH_FAIL 0x0B95
#endif
#ifndef GL_STENCIL_PASS_DEPTH_PASS
#define GL_STENCIL_PASS_DEPTH_PASS 0x0B96
#endif
#ifndef GL_STENCIL_BACK_FUNC
#define GL_STENCIL_BACK_FUNC 0x8800
#endif
#ifndef GL_STENCIL_BACK_VALUE_MASK
#define GL_STENCIL_BACK_VALUE_MASK 0x8CA4
#endif
#ifndef GL_STENCIL_BACK_REF
#define GL_STENCIL_BACK_REF 0x8CA3
#endif
#ifndef GL_STENCIL_BACK_FAIL
#define GL_STENCIL_BACK_FAIL 0x8801
#endif
#ifndef GL_STENCIL_BACK_PASS_DEPTH_FAIL
#define GL_STENCIL_BACK_PASS_DEPTH_FAIL 0x8802
#endif
#ifndef GL_STENCIL_BACK_PASS_DEPTH_PASS
#define GL_STENCIL_BACK_PASS_DEPTH_PASS 0x8803
#endif
#ifndef GL_STENCIL_BACK_WRITEMASK
#define GL_STENCIL_BACK_WRITEMASK 0x8CA5
#endif
#ifndef GL_SAMPLER_BINDING
#define GL_SAMPLER_BINDING 0x8919
#endif
#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION 0x821B
#endif
#ifndef GL_MINOR_VERSION
#define GL_MINOR_VERSION 0x821C
#endif
#ifndef GL_CONTEXT_FLAGS
#define GL_CONTEXT_FLAGS 0x821E
#endif
#ifndef GL_CONTEXT_PROFILE_MASK
#define GL_CONTEXT_PROFILE_MASK 0x9126
#endif
#ifndef GL_CONTEXT_CORE_PROFILE_BIT
#define GL_CONTEXT_CORE_PROFILE_BIT 0x00000001
#endif
#ifndef GL_SUBPIXEL_BITS
#define GL_SUBPIXEL_BITS 0x0D50
#endif
#ifndef GL_DOUBLEBUFFER
#define GL_DOUBLEBUFFER 0x0C32
#endif
#ifndef GL_STEREO
#define GL_STEREO 0x0C33
#endif
#ifndef GL_CLAMP_READ_COLOR
#define GL_CLAMP_READ_COLOR 0x891C
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define GX2GL_MAX_TEXTURE_SIZE 8192
#define GX2GL_MAX_3D_TEXTURE_SIZE 2048
#define GX2GL_MAX_ARRAY_TEXTURE_LAYERS 2048
#define GX2GL_MAX_TEXTURE_IMAGE_UNITS GL33_MAX_TEXTURE_IMAGE_UNITS
#define GX2GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS GL33_MAX_COMBINED_TEXTURE_IMAGE_UNITS
#define GX2GL_MAX_UNIFORM_BLOCKS_PER_STAGE 12
#define GX2GL_MAX_COMBINED_UNIFORM_BLOCKS 36
#define GX2GL_MAX_UNIFORM_BLOCK_SIZE 65536
#define GX2GL_UNIFORM_BUFFER_ALIGNMENT 256
#define GX2GL_MAX_VARYING_COMPONENTS 64
#define GX2GL_MAX_SHADER_UNIFORM_COMPONENTS 4096
#define GX2GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS 64
#define GX2GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS 4
#define GX2GL_MAX_POINT_SIZE 64.0f
#define GX2GL_MAX_TEXTURE_LOD_BIAS 15.0f

typedef enum {
  GET_VALUE_BOOL,
  GET_VALUE_INT,
  GET_VALUE_FLOAT
} GetValueType;

typedef struct {
  GetValueType type;
  uint32_t count;
  union {
    GLboolean b[4];
    GLint i[4];
    GLfloat f[4];
  } data;
} GetValue;

static const char *const g_extensions[] = {
    "GL_ARB_vertex_array_object",
    "GL_ARB_framebuffer_object",
    "GL_ARB_uniform_buffer_object",
    "GL_ARB_instanced_arrays",
    "GL_EXT_texture_compression_s3tc",
};

static const uint32_t g_extension_count =
    (uint32_t)(sizeof(g_extensions) / sizeof(g_extensions[0]));

static const char g_vendor[] = "Wii U Homebrew";
static const char g_renderer[] = "gx2gl on AMD Latte";
static const char g_version[] =
    "3.3 gx2gl 0.1.0 (Core Profile; not Khronos-conformant)";
static const char g_sl_version[] =
    "3.30 gx2gl-CafeGLSL (vertex/fragment runtime path)";

static unsigned active_texture_unit(void) {
  if (!g_gl_context) return 0;
  return g_gl_context->active_texture < GL33_MAX_TEXTURE_UNITS
             ? (unsigned)g_gl_context->active_texture
             : 0;
}

static GLboolean bool_value(GLboolean value) {
  return value ? GL_TRUE : GL_FALSE;
}

static GLint float_to_int(GLfloat value) {
  if (value >= 0.0f) return (GLint)(value + 0.5f);
  return (GLint)(value - 0.5f);
}

static bool set_bool1(GetValue *value, GLboolean x) {
  value->type = GET_VALUE_BOOL;
  value->count = 1;
  value->data.b[0] = bool_value(x);
  return true;
}

static bool set_bool4(GetValue *value, const GLboolean *x) {
  value->type = GET_VALUE_BOOL;
  value->count = 4;
  for (uint32_t i = 0; i < 4; ++i) value->data.b[i] = bool_value(x[i]);
  return true;
}

static bool set_int0(GetValue *value) {
  value->type = GET_VALUE_INT;
  value->count = 0;
  return true;
}

static bool set_int1(GetValue *value, GLint x) {
  value->type = GET_VALUE_INT;
  value->count = 1;
  value->data.i[0] = x;
  return true;
}

static bool set_int2(GetValue *value, GLint x, GLint y) {
  value->type = GET_VALUE_INT;
  value->count = 2;
  value->data.i[0] = x;
  value->data.i[1] = y;
  return true;
}

static bool set_int4(GetValue *value, GLint x, GLint y, GLint z, GLint w) {
  value->type = GET_VALUE_INT;
  value->count = 4;
  value->data.i[0] = x;
  value->data.i[1] = y;
  value->data.i[2] = z;
  value->data.i[3] = w;
  return true;
}

static bool set_float1(GetValue *value, GLfloat x) {
  value->type = GET_VALUE_FLOAT;
  value->count = 1;
  value->data.f[0] = x;
  return true;
}

static bool set_float2(GetValue *value, GLfloat x, GLfloat y) {
  value->type = GET_VALUE_FLOAT;
  value->count = 2;
  value->data.f[0] = x;
  value->data.f[1] = y;
  return true;
}

static bool set_float4(GetValue *value, GLfloat x, GLfloat y, GLfloat z,
                       GLfloat w) {
  value->type = GET_VALUE_FLOAT;
  value->count = 4;
  value->data.f[0] = x;
  value->data.f[1] = y;
  value->data.f[2] = z;
  value->data.f[3] = w;
  return true;
}

static bool get_context_value(GLenum pname, GetValue *value) {
  unsigned unit;

  if (!g_gl_context || !value) return false;

  memset(value, 0, sizeof(*value));
  unit = active_texture_unit();

  switch (pname) {
  case GL_MAJOR_VERSION:
    return set_int1(value, 3);
  case GL_MINOR_VERSION:
    return set_int1(value, 3);
  case GL_CONTEXT_FLAGS:
    return set_int1(value, 0);
  case GL_CONTEXT_PROFILE_MASK:
    return set_int1(value, GL_CONTEXT_CORE_PROFILE_BIT);
  case GL_NUM_EXTENSIONS:
    return set_int1(value, (GLint)g_extension_count);
  case GL_VENDOR:
  case GL_RENDERER:
  case GL_VERSION:
  case GL_SHADING_LANGUAGE_VERSION:
  case GL_EXTENSIONS:
    break;

  case GL_MAX_TEXTURE_SIZE:
    return set_int1(value, GX2GL_MAX_TEXTURE_SIZE);
  case GL_MAX_3D_TEXTURE_SIZE:
    return set_int1(value, GX2GL_MAX_3D_TEXTURE_SIZE);
  case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
    return set_int1(value, GX2GL_MAX_TEXTURE_SIZE);
  case GL_MAX_TEXTURE_IMAGE_UNITS:
    return set_int1(value, GX2GL_MAX_TEXTURE_IMAGE_UNITS);
  case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
    return set_int1(value, GX2GL_MAX_TEXTURE_IMAGE_UNITS);
  case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
    return set_int1(value, GX2GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS);
  case GL_MAX_ARRAY_TEXTURE_LAYERS:
    return set_int1(value, GX2GL_MAX_ARRAY_TEXTURE_LAYERS);
  case GL_MAX_VERTEX_ATTRIBS:
    return set_int1(value, GL33_MAX_VERTEX_ATTRIBS);
  case GL_MAX_COLOR_ATTACHMENTS:
    return set_int1(value, 8);
  case GL_MAX_DRAW_BUFFERS:
    return set_int1(value, 8);
  case GL_MAX_RENDERBUFFER_SIZE:
    return set_int1(value, GX2GL_MAX_TEXTURE_SIZE);
  case GL_MAX_SAMPLES:
  case GL_MAX_COLOR_TEXTURE_SAMPLES:
  case GL_MAX_DEPTH_TEXTURE_SAMPLES:
    return set_int1(value, GL33_MAX_SAMPLES);
  case GL_MAX_INTEGER_SAMPLES:
  case GL_MAX_SAMPLE_MASK_WORDS:
    return set_int1(value, 1);
  case GL_MAX_VERTEX_UNIFORM_BLOCKS:
  case GL_MAX_FRAGMENT_UNIFORM_BLOCKS:
    return set_int1(value, GX2GL_MAX_UNIFORM_BLOCKS_PER_STAGE);
  case GL_MAX_COMBINED_UNIFORM_BLOCKS:
    return set_int1(value, GX2GL_MAX_COMBINED_UNIFORM_BLOCKS);
  case GL_MAX_UNIFORM_BUFFER_BINDINGS:
    return set_int1(value, GL33_MAX_UNIFORM_BUFFER_BINDINGS);
  case GL_MAX_UNIFORM_BLOCK_SIZE:
    return set_int1(value, GX2GL_MAX_UNIFORM_BLOCK_SIZE);
  case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:
    return set_int1(value, GX2GL_UNIFORM_BUFFER_ALIGNMENT);
  case GL_MAX_VERTEX_UNIFORM_COMPONENTS:
  case GL_MAX_FRAGMENT_UNIFORM_COMPONENTS:
    return set_int1(value, GX2GL_MAX_SHADER_UNIFORM_COMPONENTS);
  case GL_MAX_VARYING_COMPONENTS:
    return set_int1(value, GX2GL_MAX_VARYING_COMPONENTS);
  case GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS:
    return set_int1(value, GX2GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS);
  case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS:
    return set_int1(value, GL33_MAX_TRANSFORM_FEEDBACK_BUFFER_BINDINGS);
  case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS:
    return set_int1(value, GX2GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS);
  case GL_MAX_VIEWPORT_DIMS:
    return set_int2(value, GX2GL_MAX_TEXTURE_SIZE, GX2GL_MAX_TEXTURE_SIZE);
  case GL_SUBPIXEL_BITS:
    return set_int1(value, 4);
  case GL_MAX_TEXTURE_LOD_BIAS:
    return set_float1(value, GX2GL_MAX_TEXTURE_LOD_BIAS);
  case GL_ALIASED_LINE_WIDTH_RANGE:
    return set_float2(value, 1.0f, 1.0f);
  case GL_ALIASED_POINT_SIZE_RANGE:
  case GL_POINT_SIZE_RANGE:
    return set_float2(value, 1.0f, GX2GL_MAX_POINT_SIZE);

  case GL_SHADER_COMPILER:
    return set_bool1(value, GL_TRUE);
  case GL_NUM_SHADER_BINARY_FORMATS:
    return set_int1(value, 0);
  case GL_NUM_COMPRESSED_TEXTURE_FORMATS:
    return set_int1(value, 4);
  case GL_SHADER_BINARY_FORMATS:
    return set_int0(value);
  case GL_COMPRESSED_TEXTURE_FORMATS:
    value->type = GET_VALUE_INT;
    value->count = 4;
    value->data.i[0] = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    value->data.i[1] = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    value->data.i[2] = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    value->data.i[3] = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    return true;
  case GL_IMPLEMENTATION_COLOR_READ_FORMAT:
    return set_int1(value, GL_RGBA);
  case GL_IMPLEMENTATION_COLOR_READ_TYPE:
    return set_int1(value, GL_UNSIGNED_BYTE);
  case GL_DOUBLEBUFFER:
    return set_bool1(value, GL_TRUE);
  case GL_STEREO:
    return set_bool1(value, GL_FALSE);

  case GL_DEPTH_TEST:
    return set_bool1(value, g_gl_context->depth_test_enabled);
  case GL_STENCIL_TEST:
    return set_bool1(value, g_gl_context->stencil_test_enabled);
  case GL_BLEND:
    return set_bool1(value, g_gl_context->blend_enabled);
  case GL_COLOR_LOGIC_OP:
    return set_bool1(value, g_gl_context->color_logic_op_enabled);
  case GL_CULL_FACE:
    return set_bool1(value, g_gl_context->cull_face_enabled);
  case GL_SCISSOR_TEST:
    return set_bool1(value, g_gl_context->scissor_test_enabled);
  case GL_SAMPLE_ALPHA_TO_COVERAGE:
    return set_bool1(value, g_gl_context->sample_alpha_to_coverage_enabled);
  case GL_SAMPLE_ALPHA_TO_ONE:
    return set_bool1(value, g_gl_context->sample_alpha_to_one_enabled);
  case GL_SAMPLE_COVERAGE:
    return set_bool1(value, g_gl_context->sample_coverage_enabled);
  case GL_SAMPLE_MASK:
    return set_bool1(value, g_gl_context->sample_mask_enabled);
  case GL_MULTISAMPLE:
    return set_bool1(value, g_gl_context->multisample_enabled);
  case GL_SAMPLE_COVERAGE_INVERT:
    return set_bool1(value, g_gl_context->sample_coverage_invert);
  case GL_PRIMITIVE_RESTART:
    return set_bool1(value, g_gl_context->primitive_restart_enabled);
  case GL_RASTERIZER_DISCARD:
    return set_bool1(value, g_gl_context->rasterizer_discard_enabled);
  case GL_POLYGON_OFFSET_POINT:
    return set_bool1(value, g_gl_context->polygon_offset_point_enabled);
  case GL_POLYGON_OFFSET_LINE:
    return set_bool1(value, g_gl_context->polygon_offset_line_enabled);
  case GL_POLYGON_OFFSET_FILL:
    return set_bool1(value, g_gl_context->polygon_offset_fill_enabled);

  case GL_CURRENT_PROGRAM:
    return set_int1(value, (GLint)g_gl_context->bound_program);
  case GL_ACTIVE_TEXTURE:
    return set_int1(value, (GLint)(GL_TEXTURE0 + g_gl_context->active_texture));
  case GL_ARRAY_BUFFER_BINDING:
    return set_int1(value, (GLint)g_gl_context->bound_array_buffer);
  case GL_ELEMENT_ARRAY_BUFFER_BINDING:
    return set_int1(value, (GLint)gl_vao_get_element_array_buffer());
  case GL_VERTEX_ARRAY_BINDING:
    return set_int1(value, (GLint)gl_vao_get_bound_vertex_array());
  case GL_UNIFORM_BUFFER_BINDING:
    return set_int1(value, (GLint)g_gl_context->bound_uniform_buffer);
  case GL_COPY_READ_BUFFER_BINDING:
    return set_int1(value, (GLint)g_gl_context->bound_copy_read_buffer);
  case GL_COPY_WRITE_BUFFER_BINDING:
    return set_int1(value, (GLint)g_gl_context->bound_copy_write_buffer);
  case GL_PIXEL_PACK_BUFFER_BINDING:
    return set_int1(value, (GLint)g_gl_context->bound_pixel_pack_buffer);
  case GL_PIXEL_UNPACK_BUFFER_BINDING:
    return set_int1(value, (GLint)g_gl_context->bound_pixel_unpack_buffer);
  case GL_TEXTURE_BINDING_1D:
    return set_int1(value, (GLint)g_gl_context->bound_texture_1d[unit]);
  case GL_TEXTURE_BINDING_2D:
    return set_int1(value, (GLint)g_gl_context->bound_texture_2d[unit]);
  case GL_TEXTURE_BINDING_3D:
    return set_int1(value, (GLint)g_gl_context->bound_texture_3d[unit]);
  case GL_TEXTURE_BINDING_CUBE_MAP:
    return set_int1(value, (GLint)g_gl_context->bound_texture_cube[unit]);
  case GL_TEXTURE_BINDING_1D_ARRAY:
    return set_int1(value, (GLint)g_gl_context->bound_texture_1d_array[unit]);
  case GL_TEXTURE_BINDING_2D_ARRAY:
    return set_int1(value, (GLint)g_gl_context->bound_texture_2d_array[unit]);
  case GL_TEXTURE_BINDING_2D_MULTISAMPLE:
    return set_int1(value,
                    (GLint)g_gl_context->bound_texture_2d_multisample[unit]);
  case GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY:
    return set_int1(
        value,
        (GLint)g_gl_context->bound_texture_2d_multisample_array[unit]);
  case GL_TEXTURE_BINDING_BUFFER:
    return set_int1(value, (GLint)g_gl_context->bound_texture_buffer);
  case GL_SAMPLER_BINDING:
    return set_int1(value, (GLint)g_gl_context->bound_sampler[unit]);
  case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
    return set_int1(value,
                    (GLint)g_gl_context->bound_transform_feedback_buffer);
  case GL_TRANSFORM_FEEDBACK_BINDING:
    return set_int1(value, (GLint)g_gl_context->bound_transform_feedback);
  case GL_TRANSFORM_FEEDBACK_ACTIVE:
    return set_bool1(value, gl_transform_feedback_current_active());
  case GL_TRANSFORM_FEEDBACK_PAUSED:
    return set_bool1(value, gl_transform_feedback_current_active_paused());
  case GL_FRAMEBUFFER_BINDING:
#if GL_DRAW_FRAMEBUFFER_BINDING != GL_FRAMEBUFFER_BINDING
  case GL_DRAW_FRAMEBUFFER_BINDING:
#endif
    return set_int1(value, (GLint)g_gl_context->bound_framebuffer);
  case GL_READ_FRAMEBUFFER_BINDING:
    return set_int1(value, (GLint)g_gl_context->bound_read_framebuffer);
  case GL_RENDERBUFFER_BINDING:
    return set_int1(value, (GLint)g_gl_context->bound_renderbuffer);

  case GL_PACK_ALIGNMENT:
    return set_int1(value, g_gl_context->pack_alignment);
  case GL_PACK_ROW_LENGTH:
    return set_int1(value, g_gl_context->pack_row_length);
  case GL_PACK_SKIP_ROWS:
    return set_int1(value, g_gl_context->pack_skip_rows);
  case GL_PACK_SKIP_PIXELS:
    return set_int1(value, g_gl_context->pack_skip_pixels);
  case GL_PACK_IMAGE_HEIGHT:
    return set_int1(value, g_gl_context->pack_image_height);
  case GL_PACK_SKIP_IMAGES:
    return set_int1(value, g_gl_context->pack_skip_images);
  case GL_UNPACK_ALIGNMENT:
    return set_int1(value, g_gl_context->unpack_alignment);
  case GL_UNPACK_ROW_LENGTH:
    return set_int1(value, g_gl_context->unpack_row_length);
  case GL_UNPACK_SKIP_ROWS:
    return set_int1(value, g_gl_context->unpack_skip_rows);
  case GL_UNPACK_SKIP_PIXELS:
    return set_int1(value, g_gl_context->unpack_skip_pixels);
  case GL_UNPACK_IMAGE_HEIGHT:
    return set_int1(value, g_gl_context->unpack_image_height);
  case GL_UNPACK_SKIP_IMAGES:
    return set_int1(value, g_gl_context->unpack_skip_images);

  case GL_VIEWPORT:
    return set_int4(value, g_gl_context->viewport.x, g_gl_context->viewport.y,
                    (GLint)g_gl_context->viewport.width,
                    (GLint)g_gl_context->viewport.height);
  case GL_SCISSOR_BOX:
    return set_int4(value, g_gl_context->scissor.x, g_gl_context->scissor.y,
                    (GLint)g_gl_context->scissor.width,
                    (GLint)g_gl_context->scissor.height);
  case GL_DEPTH_RANGE:
    return set_float2(value, g_gl_context->viewport.near_z,
                      g_gl_context->viewport.far_z);

  case GL_COLOR_CLEAR_VALUE:
    return set_float4(value, g_gl_context->clear_color[0],
                      g_gl_context->clear_color[1],
                      g_gl_context->clear_color[2],
                      g_gl_context->clear_color[3]);
  case GL_DEPTH_CLEAR_VALUE:
    return set_float1(value, g_gl_context->clear_depth);
  case GL_STENCIL_CLEAR_VALUE:
    return set_int1(value, g_gl_context->clear_stencil);
  case GL_BLEND_COLOR:
    return set_float4(value, g_gl_context->blend_color[0],
                      g_gl_context->blend_color[1],
                      g_gl_context->blend_color[2],
                      g_gl_context->blend_color[3]);
  case GL_LINE_WIDTH:
    return set_float1(value, g_gl_context->line_width);
  case GL_POINT_SIZE:
    return set_float1(value, g_gl_context->point_size);
  case GL_POLYGON_OFFSET_FACTOR:
    return set_float1(value, g_gl_context->polygon_offset_factor);
  case GL_POLYGON_OFFSET_UNITS:
    return set_float1(value, g_gl_context->polygon_offset_units);
  case GL_SAMPLE_COVERAGE_VALUE:
    return set_float1(value, g_gl_context->sample_coverage_value);
  case GL_SAMPLE_BUFFERS: {
    GLsizei samples = gl_get_draw_sample_count();
    return set_int1(value, samples > 0 ? 1 : 0);
  }
  case GL_SAMPLES:
    return set_int1(value, gl_get_draw_sample_count());

  case GL_DEPTH_WRITEMASK:
    return set_bool1(value, g_gl_context->depth_mask);
  case GL_COLOR_WRITEMASK:
    return set_bool4(value, g_gl_context->color_mask);
  case GL_DEPTH_FUNC:
    return set_int1(value, (GLint)g_gl_context->depth_func);
  case GL_BLEND_SRC:
  case GL_BLEND_SRC_RGB:
    return set_int1(value, (GLint)g_gl_context->blend_src_rgb);
  case GL_BLEND_DST:
  case GL_BLEND_DST_RGB:
    return set_int1(value, (GLint)g_gl_context->blend_dst_rgb);
  case GL_BLEND_SRC_ALPHA:
    return set_int1(value, (GLint)g_gl_context->blend_src_alpha);
  case GL_BLEND_DST_ALPHA:
    return set_int1(value, (GLint)g_gl_context->blend_dst_alpha);
  case GL_BLEND_EQUATION:
    return set_int1(value, (GLint)g_gl_context->blend_eq_rgb);
  case GL_BLEND_EQUATION_ALPHA:
    return set_int1(value, (GLint)g_gl_context->blend_eq_alpha);
  case GL_CULL_FACE_MODE:
    return set_int1(value, (GLint)g_gl_context->cull_face_mode);
  case GL_FRONT_FACE:
    return set_int1(value, (GLint)g_gl_context->front_face);
  case GL_POLYGON_MODE:
    return set_int2(value, (GLint)g_gl_context->polygon_mode,
                    (GLint)g_gl_context->polygon_mode);
  case GL_LOGIC_OP_MODE:
    return set_int1(value, (GLint)g_gl_context->logic_op);
  case GL_PRIMITIVE_RESTART_INDEX:
    return set_int1(value, (GLint)g_gl_context->primitive_restart_index);
  case GL_PROVOKING_VERTEX:
    return set_int1(value, (GLint)g_gl_context->provoking_vertex);
  case GL_GENERATE_MIPMAP_HINT:
    return set_int1(value, (GLint)g_gl_context->generate_mipmap_hint);
  case GL_CLAMP_READ_COLOR:
    return set_int1(value, (GLint)g_gl_context->clamp_read_color);

  case GL_STENCIL_WRITEMASK:
    return set_int1(value, (GLint)g_gl_context->stencil_write_mask[0]);
  case GL_STENCIL_BACK_WRITEMASK:
    return set_int1(value, (GLint)g_gl_context->stencil_write_mask[1]);
  case GL_STENCIL_FUNC:
    return set_int1(value, (GLint)g_gl_context->stencil_func[0]);
  case GL_STENCIL_BACK_FUNC:
    return set_int1(value, (GLint)g_gl_context->stencil_func[1]);
  case GL_STENCIL_REF:
    return set_int1(value, g_gl_context->stencil_ref[0]);
  case GL_STENCIL_BACK_REF:
    return set_int1(value, g_gl_context->stencil_ref[1]);
  case GL_STENCIL_VALUE_MASK:
    return set_int1(value, (GLint)g_gl_context->stencil_compare_mask[0]);
  case GL_STENCIL_BACK_VALUE_MASK:
    return set_int1(value, (GLint)g_gl_context->stencil_compare_mask[1]);
  case GL_STENCIL_FAIL:
    return set_int1(value, (GLint)g_gl_context->stencil_fail[0]);
  case GL_STENCIL_BACK_FAIL:
    return set_int1(value, (GLint)g_gl_context->stencil_fail[1]);
  case GL_STENCIL_PASS_DEPTH_FAIL:
    return set_int1(value, (GLint)g_gl_context->stencil_zfail[0]);
  case GL_STENCIL_BACK_PASS_DEPTH_FAIL:
    return set_int1(value, (GLint)g_gl_context->stencil_zfail[1]);
  case GL_STENCIL_PASS_DEPTH_PASS:
    return set_int1(value, (GLint)g_gl_context->stencil_zpass[0]);
  case GL_STENCIL_BACK_PASS_DEPTH_PASS:
    return set_int1(value, (GLint)g_gl_context->stencil_zpass[1]);
  default:
    break;
  }

  _gl_set_error(GL_INVALID_ENUM);
  return false;
}

static bool read_value(GLenum pname, GetValue *value) {
  return get_context_value(pname, value);
}

static void write_boolean_values(const GetValue *value, GLboolean *data) {
  for (uint32_t i = 0; i < value->count; ++i) {
    switch (value->type) {
    case GET_VALUE_BOOL:
      data[i] = bool_value(value->data.b[i]);
      break;
    case GET_VALUE_INT:
      data[i] = value->data.i[i] != 0 ? GL_TRUE : GL_FALSE;
      break;
    case GET_VALUE_FLOAT:
      data[i] = value->data.f[i] != 0.0f ? GL_TRUE : GL_FALSE;
      break;
    }
  }
}

static void write_integer_values(const GetValue *value, GLint *data) {
  for (uint32_t i = 0; i < value->count; ++i) {
    switch (value->type) {
    case GET_VALUE_BOOL:
      data[i] = value->data.b[i] ? GL_TRUE : GL_FALSE;
      break;
    case GET_VALUE_INT:
      data[i] = value->data.i[i];
      break;
    case GET_VALUE_FLOAT:
      data[i] = float_to_int(value->data.f[i]);
      break;
    }
  }
}

static void write_integer64_values(const GetValue *value, GLint64 *data) {
  for (uint32_t i = 0; i < value->count; ++i) {
    switch (value->type) {
    case GET_VALUE_BOOL:
      data[i] = value->data.b[i] ? GL_TRUE : GL_FALSE;
      break;
    case GET_VALUE_INT:
      data[i] = (GLint64)value->data.i[i];
      break;
    case GET_VALUE_FLOAT:
      data[i] = (GLint64)float_to_int(value->data.f[i]);
      break;
    }
  }
}

static void write_float_values(const GetValue *value, GLfloat *data) {
  for (uint32_t i = 0; i < value->count; ++i) {
    switch (value->type) {
    case GET_VALUE_BOOL:
      data[i] = value->data.b[i] ? 1.0f : 0.0f;
      break;
    case GET_VALUE_INT:
      data[i] = (GLfloat)value->data.i[i];
      break;
    case GET_VALUE_FLOAT:
      data[i] = value->data.f[i];
      break;
    }
  }
}

static void write_double_values(const GetValue *value, GLdouble *data) {
  for (uint32_t i = 0; i < value->count; ++i) {
    switch (value->type) {
    case GET_VALUE_BOOL:
      data[i] = value->data.b[i] ? 1.0 : 0.0;
      break;
    case GET_VALUE_INT:
      data[i] = (GLdouble)value->data.i[i];
      break;
    case GET_VALUE_FLOAT:
      data[i] = (GLdouble)value->data.f[i];
      break;
    }
  }
}

const GLubyte *_gl_GetString(GLenum name) {
  switch (name) {
  case GL_VENDOR:
    return (const GLubyte *)g_vendor;
  case GL_RENDERER:
    return (const GLubyte *)g_renderer;
  case GL_VERSION:
    return (const GLubyte *)g_version;
  case GL_SHADING_LANGUAGE_VERSION:
    return (const GLubyte *)g_sl_version;
  case GL_EXTENSIONS:
    _gl_set_error(GL_INVALID_ENUM);
    return NULL;
  default:
    _gl_set_error(GL_INVALID_ENUM);
    return NULL;
  }
}

const GLubyte *_gl_GetStringi(GLenum name, GLuint index) {
  if (name != GL_EXTENSIONS) {
    _gl_set_error(GL_INVALID_ENUM);
    return NULL;
  }
  if (index >= g_extension_count) {
    _gl_set_error(GL_INVALID_VALUE);
    return NULL;
  }

  return (const GLubyte *)g_extensions[index];
}

void _gl_GetBooleanv(GLenum pname, GLboolean *data) {
  GetValue value;

  if (!data) return;
  if (!read_value(pname, &value)) return;
  write_boolean_values(&value, data);
}

void _gl_GetIntegerv(GLenum pname, GLint *data) {
  GetValue value;

  if (!data) return;
  if (!read_value(pname, &value)) return;
  write_integer_values(&value, data);
}

void _gl_GetInteger64v(GLenum pname, GLint64 *data) {
  GetValue value;

  if (!data) return;
  if (!read_value(pname, &value)) return;
  write_integer64_values(&value, data);
}

void _gl_GetFloatv(GLenum pname, GLfloat *data) {
  GetValue value;

  if (!data) return;
  if (!read_value(pname, &value)) return;
  write_float_values(&value, data);
}

void _gl_GetDoublev(GLenum pname, GLdouble *data) {
  GetValue value;

  if (!data) return;
  if (!read_value(pname, &value)) return;
  write_double_values(&value, data);
}

#ifdef __cplusplus
}
#endif
