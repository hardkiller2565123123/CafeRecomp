#ifndef GL_H
#define GL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef signed char GLbyte;
typedef short GLshort;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned long long GLuint64;
typedef long long GLint64;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;
typedef char GLchar;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1

#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_POINTS 0x0000
#define GL_LINES_ADJACENCY 0x000A
#define GL_LINE_STRIP_ADJACENCY 0x000B
#define GL_TRIANGLES_ADJACENCY 0x000C
#define GL_TRIANGLE_STRIP_ADJACENCY 0x000D

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400

#define GL_FLOAT 0x1406
#define GL_HALF_FLOAT 0x140B
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_INT 0x1404
#define GL_BYTE 0x1400
#define GL_SHORT 0x1402
#define GL_UNSIGNED_INT_24_8 0x84FA

#define GL_FLOAT_VEC2 0x8B50
#define GL_FLOAT_VEC3 0x8B51
#define GL_FLOAT_VEC4 0x8B52
#define GL_INT_VEC2 0x8B53
#define GL_INT_VEC3 0x8B54
#define GL_INT_VEC4 0x8B55
#define GL_BOOL 0x8B56
#define GL_BOOL_VEC2 0x8B57
#define GL_BOOL_VEC3 0x8B58
#define GL_BOOL_VEC4 0x8B59
#define GL_FLOAT_MAT2 0x8B5A
#define GL_FLOAT_MAT3 0x8B5B
#define GL_FLOAT_MAT4 0x8B5C
#define GL_UNSIGNED_INT_VEC2 0x8DC6
#define GL_UNSIGNED_INT_VEC3 0x8DC7
#define GL_UNSIGNED_INT_VEC4 0x8DC8
#define GL_FLOAT_MAT2x3 0x8B65
#define GL_FLOAT_MAT2x4 0x8B66
#define GL_FLOAT_MAT3x2 0x8B67
#define GL_FLOAT_MAT3x4 0x8B68
#define GL_FLOAT_MAT4x2 0x8B69
#define GL_FLOAT_MAT4x3 0x8B6A
#define GL_SAMPLER_2D 0x8B5E
#define GL_SAMPLER_CUBE 0x8B60

#define GL_RGBA 0x1908
#define GL_RGB 0x1907
#define GL_RGBA8 0x8058
#define GL_RGB8 0x8051
#define GL_RG 0x8227
#define GL_RG8 0x822B
#define GL_RED 0x1903
#define GL_GREEN 0x1904
#define GL_BLUE 0x1905
#define GL_R8 0x8229
#define GL_RGBA16F 0x881A
#define GL_RGBA32F 0x8814
#define GL_ALPHA 0x1906
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A

#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH_COMPONENT32 0x81A7
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_DEPTH_STENCIL 0x84F9
#define GL_DEPTH24_STENCIL8 0x88F0

#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#define GL_PROXY_TEXTURE_2D_MULTISAMPLE 0x9101
#define GL_TEXTURE_2D_MULTISAMPLE_ARRAY 0x9102
#define GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY 0x9103
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X 0x8516
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y 0x8517
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y 0x8518
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z 0x8519
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A
#define GL_TEXTURE_1D 0x0DE0
#define GL_TEXTURE0 0x84C0
#define GL_ACTIVE_TEXTURE 0x84E0

#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703

#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_TEXTURE_MIN_LOD 0x813A
#define GL_TEXTURE_MAX_LOD 0x813B
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_TEXTURE_LOD_BIAS 0x8501
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_TEXTURE_SWIZZLE_R 0x8E42
#define GL_TEXTURE_SWIZZLE_G 0x8E43
#define GL_TEXTURE_SWIZZLE_B 0x8E44
#define GL_TEXTURE_SWIZZLE_A 0x8E45
#define GL_TEXTURE_SWIZZLE_RGBA 0x8E46
#define GL_TEXTURE_BORDER_COLOR 0x1004

#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_BORDER 0x812D
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370
#define GL_COMPARE_REF_TO_TEXTURE 0x884E

#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_UNIFORM_BUFFER_BINDING 0x8A28
#define GL_UNIFORM_BUFFER_START   0x8A29
#define GL_UNIFORM_BUFFER_SIZE    0x8A2A
#define GL_MAX_UNIFORM_BUFFER_BINDINGS 0x8A2F
#define GL_COPY_READ_BUFFER  0x8F36
#define GL_COPY_WRITE_BUFFER 0x8F37
#define GL_COPY_READ_BUFFER_BINDING  0x8F36
#define GL_COPY_WRITE_BUFFER_BINDING 0x8F37
#define GL_PIXEL_PACK_BUFFER 0x88EB
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#define GL_PIXEL_PACK_BUFFER_BINDING 0x88ED
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#define GL_TEXTURE_BUFFER 0x8C2A
#define GL_TEXTURE_1D_ARRAY 0x8C18
#define GL_PROXY_TEXTURE_1D_ARRAY 0x8C19
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_PROXY_TEXTURE_2D_ARRAY 0x8C1B
#define GL_TEXTURE_BINDING_1D 0x8068
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_TEXTURE_BINDING_3D 0x806A
#define GL_TEXTURE_BINDING_CUBE_MAP 0x8514
#define GL_TEXTURE_BINDING_BUFFER 0x8C2C
#define GL_TEXTURE_BINDING_1D_ARRAY 0x8C1C
#define GL_TEXTURE_BINDING_2D_ARRAY 0x8C1D
#define GL_TEXTURE_BINDING_2D_MULTISAMPLE 0x9104
#define GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY 0x9105
#define GL_MAX_ARRAY_TEXTURE_LAYERS 0x88FF
#define GL_TEXTURE_SAMPLES 0x9106
#define GL_TEXTURE_FIXED_SAMPLE_LOCATIONS 0x9107
#define GL_SAMPLER_1D_ARRAY 0x8DC0
#define GL_SAMPLER_2D_ARRAY 0x8DC1
#define GL_SAMPLER_1D_ARRAY_SHADOW 0x8DC3
#define GL_SAMPLER_2D_ARRAY_SHADOW 0x8DC4
#define GL_INT_SAMPLER_1D_ARRAY 0x8DCE
#define GL_INT_SAMPLER_2D_ARRAY 0x8DCF
#define GL_UNSIGNED_INT_SAMPLER_1D_ARRAY 0x8DD6
#define GL_UNSIGNED_INT_SAMPLER_2D_ARRAY 0x8DD7
#define GL_SAMPLER_2D_MULTISAMPLE 0x9108
#define GL_INT_SAMPLER_2D_MULTISAMPLE 0x9109
#define GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE 0x910A
#define GL_SAMPLER_2D_MULTISAMPLE_ARRAY 0x910B
#define GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY 0x910C
#define GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY 0x910D
#define GL_SAMPLER_BINDING 0x8919
#define GL_TRANSFORM_FEEDBACK 0x8E22
#define GL_TRANSFORM_FEEDBACK_BINDING 0x8E25
#define GL_TRANSFORM_FEEDBACK_BUFFER 0x8C8E
#define GL_TRANSFORM_FEEDBACK_BUFFER_BINDING 0x8C8F
#define GL_TRANSFORM_FEEDBACK_BUFFER_START 0x8C84
#define GL_TRANSFORM_FEEDBACK_BUFFER_SIZE 0x8C85
#define GL_TRANSFORM_FEEDBACK_BUFFER_MODE 0x8C7F
#define GL_TRANSFORM_FEEDBACK_VARYINGS 0x8C83
#define GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH 0x8C76
#define GL_TRANSFORM_FEEDBACK_ACTIVE 0x8E24
#define GL_TRANSFORM_FEEDBACK_PAUSED 0x8E23
#define GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN 0x8C88
#define GL_INTERLEAVED_ATTRIBS 0x8C8C
#define GL_SEPARATE_ATTRIBS 0x8C8D
#define GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS 0x8C8A
#define GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS 0x8C8B
#define GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS 0x8C80
#define GL_PRIMITIVES_GENERATED 0x8C87
#define GL_CURRENT_QUERY 0x8865
#define GL_QUERY_COUNTER_BITS 0x8864
#define GL_QUERY_RESULT 0x8866
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#define GL_SAMPLES_PASSED 0x8914
#define GL_ANY_SAMPLES_PASSED 0x8C2F
#define GL_ANY_SAMPLES_PASSED_CONSERVATIVE 0x8D6A
#define GL_QUERY_WAIT 0x8E13
#define GL_QUERY_NO_WAIT 0x8E14
#define GL_QUERY_BY_REGION_WAIT 0x8E15
#define GL_QUERY_BY_REGION_NO_WAIT 0x8E16
#define GL_STATIC_DRAW 0x88E4
#define GL_STATIC_READ 0x88E5
#define GL_STATIC_COPY 0x88E6
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_DYNAMIC_READ 0x88E9
#define GL_DYNAMIC_COPY 0x88EA
#define GL_STREAM_DRAW 0x88E0
#define GL_STREAM_READ 0x88E1
#define GL_STREAM_COPY 0x88E2
#define GL_MAP_READ_BIT  0x0001
#define GL_MAP_WRITE_BIT 0x0002

#define GL_BUFFER_SIZE 0x8764
#define GL_BUFFER_USAGE 0x8765
#define GL_BUFFER_ACCESS 0x88BB
#define GL_BUFFER_MAPPED 0x88BC
#define GL_BUFFER_MAP_POINTER 0x88BD
#define GL_BUFFER_ACCESS_FLAGS 0x911F
#define GL_BUFFER_MAP_LENGTH 0x9120
#define GL_BUFFER_MAP_OFFSET 0x9121

#define GL_READ_ONLY 0x88B8
#define GL_WRITE_ONLY 0x88B9
#define GL_READ_WRITE 0x88BA

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_GEOMETRY_SHADER 0x8DD9

#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VALIDATE_STATUS 0x8B83
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_ATTACHED_SHADERS 0x8B85
#define GL_ACTIVE_UNIFORMS 0x8B86
#define GL_ACTIVE_UNIFORM_MAX_LENGTH 0x8B87
#define GL_ACTIVE_ATTRIBUTES 0x8B89
#define GL_ACTIVE_ATTRIBUTE_MAX_LENGTH 0x8B8A
#define GL_SHADER_SOURCE_LENGTH 0x8B88
#define GL_SHADER_TYPE 0x8B4F
#define GL_DELETE_STATUS 0x8B80
#define GL_CURRENT_PROGRAM 0x8B8D

#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE 0x0308
#define GL_CONSTANT_COLOR 0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#define GL_CONSTANT_ALPHA 0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004

#define GL_FUNC_ADD 0x8006
#define GL_BLEND_COLOR 0x8005
#define GL_BLEND_EQUATION 0x8009
#define GL_BLEND_EQUATION_RGB 0x8009
#define GL_BLEND_EQUATION_ALPHA 0x883D
#define GL_BLEND_DST_RGB 0x80C8
#define GL_BLEND_SRC_RGB 0x80C9
#define GL_BLEND_DST_ALPHA 0x80CA
#define GL_BLEND_SRC_ALPHA 0x80CB
#define GL_BLEND_DST 0x0BE0
#define GL_BLEND_SRC 0x0BE1
#define GL_FUNC_SUBTRACT 0x800A
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_MIN 0x8007
#define GL_MAX 0x8008

#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207

#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_INCR 0x1E02
#define GL_DECR 0x1E03
#define GL_INVERT 0x150A
#define GL_INCR_WRAP 0x8507
#define GL_DECR_WRAP 0x8508

#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_NUM_EXTENSIONS 0x821D
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C
#define GL_CONTEXT_FLAGS 0x821E
#define GL_CONTEXT_PROFILE_MASK 0x9126
#define GL_CONTEXT_CORE_PROFILE_BIT 0x00000001
#define GL_SUBPIXEL_BITS 0x0D50
#define GL_DOUBLEBUFFER 0x0C32
#define GL_STEREO 0x0C33
#define GL_CLAMP_VERTEX_COLOR 0x891A
#define GL_CLAMP_FRAGMENT_COLOR 0x891B
#define GL_CLAMP_READ_COLOR 0x891C
#define GL_FIXED_ONLY 0x891D
#define GL_RASTERIZER_DISCARD 0x8C89

#define GL_CULL_FACE 0x0B44
#define GL_CULL_FACE_MODE 0x0B45
#define GL_FRONT_FACE 0x0B46
#define GL_DEPTH_TEST 0x0B71
#define GL_DEPTH_FUNC 0x0B74
#define GL_STENCIL_TEST 0x0B90
#define GL_STENCIL_FUNC 0x0B92
#define GL_STENCIL_VALUE_MASK 0x0B93
#define GL_STENCIL_REF 0x0B97
#define GL_STENCIL_FAIL 0x0B94
#define GL_STENCIL_PASS_DEPTH_FAIL 0x0B95
#define GL_STENCIL_PASS_DEPTH_PASS 0x0B96
#define GL_STENCIL_BACK_FUNC 0x8800
#define GL_STENCIL_BACK_FAIL 0x8801
#define GL_STENCIL_BACK_PASS_DEPTH_FAIL 0x8802
#define GL_STENCIL_BACK_PASS_DEPTH_PASS 0x8803
#define GL_STENCIL_BACK_REF 0x8CA3
#define GL_STENCIL_BACK_VALUE_MASK 0x8CA4
#define GL_STENCIL_BACK_WRITEMASK 0x8CA5
#define GL_BLEND 0x0BE2
#define GL_COLOR_LOGIC_OP 0x0BF2
#define GL_SCISSOR_TEST 0x0C11
#define GL_POINT_SIZE 0x0B11
#define GL_POINT_SIZE_RANGE 0x0B12
#define GL_POLYGON_OFFSET_UNITS 0x2A00
#define GL_POLYGON_OFFSET_POINT 0x2A01
#define GL_POLYGON_OFFSET_LINE 0x2A02
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_POLYGON_OFFSET_FACTOR 0x8038
#define GL_POLYGON_MODE 0x0B40
#define GL_MULTISAMPLE 0x809D
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#define GL_SAMPLE_ALPHA_TO_ONE 0x809F
#define GL_SAMPLE_COVERAGE 0x80A0
#define GL_SAMPLE_BUFFERS 0x80A8
#define GL_SAMPLES 0x80A9

#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408

#define GL_CW 0x0900
#define GL_CCW 0x0901

#define GL_POINT 0x1B00
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02

#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_ATTACHMENT1 0x8CE1
#define GL_COLOR_ATTACHMENT2 0x8CE2
#define GL_COLOR_ATTACHMENT3 0x8CE3
#define GL_COLOR_ATTACHMENT4 0x8CE4
#define GL_COLOR_ATTACHMENT5 0x8CE5
#define GL_COLOR_ATTACHMENT6 0x8CE6
#define GL_COLOR_ATTACHMENT7 0x8CE7
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A

#define GL_MAX_COLOR_ATTACHMENTS 0x8CDF
#define GL_MAX_DRAW_BUFFERS 0x8824
#define GL_MAX_RENDERBUFFER_SIZE 0x84E8
#define GL_MAX_SAMPLES 0x8D57
#define GL_SAMPLE_POSITION 0x8E50
#define GL_SAMPLE_MASK 0x8E51
#define GL_SAMPLE_MASK_VALUE 0x8E52
#define GL_MAX_SAMPLE_MASK_WORDS 0x8E59
#define GL_MAX_COLOR_TEXTURE_SAMPLES 0x910E
#define GL_MAX_DEPTH_TEXTURE_SAMPLES 0x910F
#define GL_MAX_INTEGER_SAMPLES 0x9110

#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_FRAMEBUFFER_UNDEFINED 0x8219
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER 0x8CDB
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER 0x8CDC
#define GL_FRAMEBUFFER_UNSUPPORTED 0x8CDD
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE 0x8D56
#define GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS 0x8DA8

#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05

#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001
#define GL_TEXTURE_DEPTH 0x8071
#define GL_TEXTURE_INTERNAL_FORMAT 0x1003
#define GL_TEXTURE_RED_SIZE 0x805C
#define GL_TEXTURE_GREEN_SIZE 0x805D
#define GL_TEXTURE_BLUE_SIZE 0x805E
#define GL_TEXTURE_ALPHA_SIZE 0x805F
#define GL_TEXTURE_DEPTH_SIZE 0x884A
#define GL_TEXTURE_STENCIL_SIZE 0x88F1
#define GL_TEXTURE_RED_TYPE 0x8C10
#define GL_TEXTURE_GREEN_TYPE 0x8C11
#define GL_TEXTURE_BLUE_TYPE 0x8C12
#define GL_TEXTURE_ALPHA_TYPE 0x8C13
#define GL_TEXTURE_DEPTH_TYPE 0x8C16
#define GL_TEXTURE_SHARED_SIZE 0x8C3F
#define GL_TEXTURE_COMPRESSED 0x86A1
#define GL_TEXTURE_COMPRESSED_IMAGE_SIZE 0x86A0
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_UNSIGNED_NORMALIZED 0x8C17
#define GL_SIGNED_NORMALIZED 0x8F9C
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_3D_TEXTURE_SIZE 0x8073
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE 0x851C
#define GL_MAX_TEXTURE_LOD_BIAS 0x84FD
#define GL_MAX_VIEWPORT_DIMS 0x0D3A
#define GL_ALIASED_POINT_SIZE_RANGE 0x846D
#define GL_ALIASED_LINE_WIDTH_RANGE 0x846E

#define GL_MAX_VERTEX_ATTRIBS 0x8869
#define GL_MAX_TEXTURE_IMAGE_UNITS 0x8872
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS 0x8B4C
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#define GL_MAX_ARRAY_TEXTURE_LAYERS 0x88FF
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS 0x8B4A
#define GL_MAX_FRAGMENT_UNIFORM_COMPONENTS 0x8B49
#define GL_MAX_VARYING_COMPONENTS 0x8B4B
#define GL_MAX_VERTEX_UNIFORM_BLOCKS 0x8A2B
#define GL_MAX_FRAGMENT_UNIFORM_BLOCKS 0x8A2D
#define GL_MAX_COMBINED_UNIFORM_BLOCKS 0x8A2E
#define GL_MAX_UNIFORM_BUFFER_BINDINGS 0x8A2F
#define GL_MAX_UNIFORM_BLOCK_SIZE 0x8A30
#define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 0x8A34

#define GL_LOW_FLOAT 0x8DF0
#define GL_MEDIUM_FLOAT 0x8DF1
#define GL_HIGH_FLOAT 0x8DF2
#define GL_LOW_INT 0x8DF3
#define GL_MEDIUM_INT 0x8DF4
#define GL_HIGH_INT 0x8DF5
#define GL_SHADER_COMPILER 0x8DFA
#define GL_NUM_SHADER_BINARY_FORMATS 0x8DF9
#define GL_SHADER_BINARY_FORMATS 0x8DF8
#define GL_NUM_COMPRESSED_TEXTURE_FORMATS 0x86A2
#define GL_COMPRESSED_TEXTURE_FORMATS 0x86A3
#define GL_IMPLEMENTATION_COLOR_READ_FORMAT 0x8B9B
#define GL_IMPLEMENTATION_COLOR_READ_TYPE 0x8B9A

#define GL_MAP_READ_BIT 0x0001
#define GL_MAP_WRITE_BIT 0x0002
#define GL_MAP_INVALIDATE_RANGE_BIT 0x0004
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#define GL_MAP_FLUSH_EXPLICIT_BIT 0x0010
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020

#define GL_CLEAR 0x1500
#define GL_AND 0x1501
#define GL_AND_REVERSE 0x1502
#define GL_COPY 0x1503
#define GL_AND_INVERTED 0x1504
#define GL_NOOP 0x1505
#define GL_XOR 0x1506
#define GL_OR 0x1507
#define GL_NOR 0x1508
#define GL_EQUIV 0x1509
#define GL_OR_REVERSE 0x150B
#define GL_COPY_INVERTED 0x150C
#define GL_OR_INVERTED 0x150D
#define GL_NAND 0x150E
#define GL_SET 0x150F
#define GL_LOGIC_OP_MODE 0x0BF0

#define GL_DONT_CARE 0x1100
#define GL_FASTEST 0x1101
#define GL_NICEST 0x1102
#define GL_GENERATE_MIPMAP_HINT 0x8192

#define GL_DEPTH_WRITEMASK 0x0B72
#define GL_COLOR_WRITEMASK 0x0C23
#define GL_STENCIL_WRITEMASK 0x0B98
#define GL_VIEWPORT 0x0BA2
#define GL_SCISSOR_BOX 0x0C10
#define GL_PACK_ROW_LENGTH 0x0D02
#define GL_PACK_SKIP_ROWS 0x0D03
#define GL_PACK_SKIP_PIXELS 0x0D04
#define GL_PACK_IMAGE_HEIGHT 0x806C
#define GL_PACK_SKIP_IMAGES 0x806B
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#define GL_UNPACK_IMAGE_HEIGHT 0x806E
#define GL_UNPACK_SKIP_IMAGES 0x806D
#define GL_STENCIL_CLEAR_VALUE 0x0B91
#define GL_DEPTH_RANGE 0x0B70
#define GL_COLOR_CLEAR_VALUE 0x0C22
#define GL_DEPTH_CLEAR_VALUE 0x0B73
#define GL_LINE_WIDTH 0x0B21
#define GL_SAMPLE_COVERAGE_VALUE 0x80AA
#define GL_SAMPLE_COVERAGE_INVERT 0x80AB
#define GL_PRIMITIVE_RESTART 0x8F9D
#define GL_PRIMITIVE_RESTART_INDEX 0x8F9E
#define GL_FIRST_VERTEX_CONVENTION 0x8E4D
#define GL_LAST_VERTEX_CONVENTION 0x8E4E
#define GL_PROVOKING_VERTEX 0x8E4F
#define GL_CURRENT_VERTEX_ATTRIB 0x8626
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED 0x8622
#define GL_VERTEX_ATTRIB_ARRAY_SIZE 0x8623
#define GL_VERTEX_ATTRIB_ARRAY_STRIDE 0x8624
#define GL_VERTEX_ATTRIB_ARRAY_TYPE 0x8625
#define GL_VERTEX_ATTRIB_ARRAY_NORMALIZED 0x886A
#define GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING 0x889F
#define GL_VERTEX_ATTRIB_ARRAY_DIVISOR 0x88FE
#define GL_VERTEX_ATTRIB_ARRAY_INTEGER 0x88FD
#define GL_VERTEX_ATTRIB_ARRAY_POINTER 0x8645
#define GL_VERTEX_ARRAY_BINDING 0x85B5

#define GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING 0x8210
#define GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE 0x8211
#define GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE 0x8212
#define GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE 0x8213
#define GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE 0x8214
#define GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE 0x8215
#define GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE 0x8216
#define GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE 0x8217
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE 0x8CD0
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME 0x8CD1
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL 0x8CD2
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE 0x8CD3
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER 0x8CD4
#define GL_FRAMEBUFFER_ATTACHMENT_LAYERED 0x8DA7
#define GL_RENDERBUFFER_BINDING 0x8CA7
#define GL_TEXTURE 0x1702
#define GL_SRGB 0x8C40
#define GL_RENDERBUFFER_WIDTH           0x8D42
#define GL_RENDERBUFFER_HEIGHT          0x8D43
#define GL_RENDERBUFFER_INTERNAL_FORMAT 0x8D44
#define GL_RENDERBUFFER_SAMPLES         0x8CAB
#define GL_RENDERBUFFER_RED_SIZE        0x8D50
#define GL_RENDERBUFFER_GREEN_SIZE      0x8D51
#define GL_RENDERBUFFER_BLUE_SIZE       0x8D52
#define GL_RENDERBUFFER_ALPHA_SIZE      0x8D53
#define GL_RENDERBUFFER_DEPTH_SIZE      0x8D54
#define GL_RENDERBUFFER_STENCIL_SIZE    0x8D55
#define GL_FRAMEBUFFER_DEFAULT          0x8218
#define GL_NONE 0
#define GL_INVALID_INDEX 0xFFFFFFFFu
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY 0x0505
#define GL_INVALID_FRAMEBUFFER_OPERATION 0x0506

#define GL_COLOR   0x1800
#define GL_DEPTH   0x1801
#define GL_STENCIL 0x1802

#define GL_SIGNALED    0x9119
#define GL_UNSIGNALED  0x9118

void glGenBuffers(GLsizei n, GLuint *buffers);
void glDeleteBuffers(GLsizei n, const GLuint *buffers);
GLboolean glIsBuffer(GLuint buffer);
void glBindBuffer(GLenum target, GLuint buffer);
void glBindBufferBase(GLenum target, GLuint index, GLuint buffer);
void glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data);
void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params);
void glGetBufferPointerv(GLenum target, GLenum pname, GLvoid **params);
void* glMapBuffer(GLenum target, GLenum access);
void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
GLboolean glUnmapBuffer(GLenum target);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
GLboolean glIsEnabled(GLenum cap);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glBlendEquation(GLenum mode);
void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
void glBlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void glDepthFunc(GLenum func);
void glDepthMask(GLboolean flag);
void glDepthRange(GLclampd nearVal, GLclampd farVal);
void glDepthRangef(GLclampf nearVal, GLclampf farVal);
void glStencilFunc(GLenum func, GLint ref, GLuint mask);
void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass);
void glStencilMask(GLuint mask);
void glStencilMaskSeparate(GLenum face, GLuint mask);
void glCullFace(GLenum mode);
void glFrontFace(GLenum mode);
void glPolygonMode(GLenum face, GLenum mode);
void glPolygonOffset(GLfloat factor, GLfloat units);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void glLineWidth(GLfloat width);
void glHint(GLenum target, GLenum mode);
void glSampleCoverage(GLclampf value, GLboolean invert);
void glPixelStorei(GLenum pname, GLint param);
void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void glClearDepth(GLclampd depth);
void glClearDepthf(GLclampf depth);
void glClearStencil(GLint s);
void glClear(GLbitfield mask);
void glGenTextures(GLsizei n, GLuint *textures);
void glDeleteTextures(GLsizei n, const GLuint *textures);
GLboolean glIsTexture(GLuint texture);
void glGenSamplers(GLsizei n, GLuint *samplers);
void glDeleteSamplers(GLsizei n, const GLuint *samplers);
GLboolean glIsSampler(GLuint sampler);
void glBindTexture(GLenum target, GLuint texture);
void glBindSampler(GLuint unit, GLuint sampler);
void glActiveTexture(GLenum texture);
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *pixels);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glTexParameterf(GLenum target, GLenum pname, GLfloat param);
void glTexParameteriv(GLenum target, GLenum pname, const GLint *params);
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params);
void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params);
void glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint *param);
void glSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat *param);
void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param);
void glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param);
void glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params);
void glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params);
void glGenerateMipmap(GLenum target);
GLuint glCreateShader(GLenum type);
void glDeleteShader(GLuint shader);
GLboolean glIsShader(GLuint shader);
void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source);
void glCompileShader(GLuint shader);
GLuint glCreateProgram(void);
void glDeleteProgram(GLuint program);
GLboolean glIsProgram(GLuint program);
void glAttachShader(GLuint program, GLuint shader);
void glDetachShader(GLuint program, GLuint shader);
void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name);
void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders);
void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
void glLinkProgram(GLuint program);
void glValidateProgram(GLuint program);
void glUseProgram(GLuint program);
void glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
void glGetProgramiv(GLuint program, GLenum pname, GLint *params);
void glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
void glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
void glGetUniformfv(GLuint program, GLint location, GLfloat *params);
void glGetUniformiv(GLuint program, GLint location, GLint *params);
void glUniform1f(GLint location, GLfloat v0);
void glUniform1fv(GLint location, GLsizei count, const GLfloat *value);
void glUniform1i(GLint location, GLint v0);
void glUniform1iv(GLint location, GLsizei count, const GLint *value);
void glUniform2f(GLint location, GLfloat v0, GLfloat v1);
void glUniform2fv(GLint location, GLsizei count, const GLfloat *value);
void glUniform2i(GLint location, GLint v0, GLint v1);
void glUniform2iv(GLint location, GLsizei count, const GLint *value);
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
void glUniform3fv(GLint location, GLsizei count, const GLfloat *value);
void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2);
void glUniform3iv(GLint location, GLsizei count, const GLint *value);
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void glUniform4fv(GLint location, GLsizei count, const GLfloat *value);
void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
void glUniform4iv(GLint location, GLsizei count, const GLint *value);
void glUniform1ui(GLint location, GLuint v0);
void glUniform2ui(GLint location, GLuint v0, GLuint v1);
void glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2);
void glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3);
void glUniform1uiv(GLint location, GLsizei count, const GLuint *value);
void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
GLint glGetUniformLocation(GLuint program, const GLchar *name);
GLint glGetAttribLocation(GLuint program, const GLchar *name);
GLuint glGetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName);
void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding);
void glGenVertexArrays(GLsizei n, GLuint *arrays);
void glDeleteVertexArrays(GLsizei n, const GLuint *arrays);
GLboolean glIsVertexArray(GLuint array);
void glBindVertexArray(GLuint array);
void glEnableVertexAttribArray(GLuint index);
void glDisableVertexAttribArray(GLuint index);
void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params);
void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params);
void glGetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid **pointer);
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
void glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glGetVertexAttribIiv(GLuint index, GLenum pname, GLint *params);
void glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint *params);
void glVertexAttribI1i(GLuint index, GLint x);
void glVertexAttribI2i(GLuint index, GLint x, GLint y);
void glVertexAttribI3i(GLuint index, GLint x, GLint y, GLint z);
void glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w);
void glVertexAttribI1ui(GLuint index, GLuint x);
void glVertexAttribI2ui(GLuint index, GLuint x, GLuint y);
void glVertexAttribI3ui(GLuint index, GLuint x, GLuint y, GLuint z);
void glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w);
void glVertexAttribI1iv(GLuint index, const GLint *v);
void glVertexAttribI2iv(GLuint index, const GLint *v);
void glVertexAttribI3iv(GLuint index, const GLint *v);
void glVertexAttribI4iv(GLuint index, const GLint *v);
void glVertexAttribI1uiv(GLuint index, const GLuint *v);
void glVertexAttribI2uiv(GLuint index, const GLuint *v);
void glVertexAttribI3uiv(GLuint index, const GLuint *v);
void glVertexAttribI4uiv(GLuint index, const GLuint *v);
void glVertexAttrib1f(GLuint index, GLfloat x);
void glVertexAttrib1fv(GLuint index, const GLfloat *v);
void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y);
void glVertexAttrib2fv(GLuint index, const GLfloat *v);
void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z);
void glVertexAttrib3fv(GLuint index, const GLfloat *v);
void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void glVertexAttrib4fv(GLuint index, const GLfloat *v);
void glVertexAttribDivisor(GLuint index, GLuint divisor);
void glGenFramebuffers(GLsizei n, GLuint *framebuffers);
void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers);
GLboolean glIsFramebuffer(GLuint framebuffer);
void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers);
void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers);
GLboolean glIsRenderbuffer(GLuint renderbuffer);
void glBindFramebuffer(GLenum target, GLuint framebuffer);
void glBindRenderbuffer(GLenum target, GLuint renderbuffer);
GLenum glCheckFramebufferStatus(GLenum target);
void glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level);
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params);
void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params);
void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
void glDrawBuffer(GLenum buf);
void glDrawBuffers(GLsizei n, const GLenum *bufs);
void glReadBuffer(GLenum src);
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei instancecount);
void glFlush(void);
void glFinish(void);
void glUniform4uiv(GLint location, GLsizei count, const GLuint *value);
void glGetUniformuiv(GLuint program, GLint location, GLuint *params);
void glGenQueries(GLsizei n, GLuint *ids);
void glDeleteQueries(GLsizei n, const GLuint *ids);
GLboolean glIsQuery(GLuint id);
void glBeginQuery(GLenum target, GLuint id);
void glEndQuery(GLenum target);
void glGetQueryiv(GLenum target, GLenum pname, GLint *params);
void glGetQueryObjectiv(GLuint id, GLenum pname, GLint *params);
void glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params);
void glBeginConditionalRender(GLuint id, GLenum mode);
void glEndConditionalRender(void);
void glBeginQueryIndexed(GLenum target, GLuint index, GLuint id);
void glEndQueryIndexed(GLenum target, GLuint index);
void glGetQueryIndexediv(GLenum target, GLuint index, GLenum pname, GLint *params);
void glGenTransformFeedbacks(GLsizei n, GLuint *ids);
void glDeleteTransformFeedbacks(GLsizei n, const GLuint *ids);
void glLogicOp(GLenum opcode);
void glPointSize(GLfloat size);
void glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length);
void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels);
void glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border);
void glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width);
void glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void glCompressedTexImage1D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const GLvoid *data);
void glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid *data);
void glCompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const GLvoid *data);
void glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid *data);
void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params);
void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params);
void glGetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint *params);
void glGetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformBlockName);
void glGetActiveUniformsiv(GLuint program, GLsizei uniformCount, const GLuint *uniformIndices, GLenum pname, GLint *params);
void glGetActiveUniformName(GLuint program, GLuint uniformIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformName);
void glUniform2uiv(GLint location, GLsizei count, const GLuint *value);
void glUniform3uiv(GLint location, GLsizei count, const GLuint *value);
void glWiiULoadShaderGroup(GLuint program, const void *shaderGroup);
void glWiiULoadShaderGroupGFD(GLuint program, GLuint index, const void *gfdData);
GLenum glGetError(void);
const GLubyte *glGetString(GLenum name);
const GLubyte *glGetStringi(GLenum name, GLuint index);
void glGetBooleanv(GLenum pname, GLboolean *params);
void glGetDoublev(GLenum pname, GLdouble *params);
void glGetIntegerv(GLenum pname, GLint *params);
void glGetFloatv(GLenum pname, GLfloat *params);
void glReleaseShaderCompiler(void);
void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat, const GLvoid *binary, GLsizei length);
void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision);
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data);
void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data);



void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices);
void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLint basevertex);
void glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices, GLint basevertex);
void glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei instancecount, GLint basevertex);
void glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei drawcount);
void glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const GLvoid *const *indices, GLsizei drawcount);
void glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei *count, GLenum type, const GLvoid *const *indices, GLsizei drawcount, const GLint *basevertex);
void glPrimitiveRestartIndex(GLuint index);


void glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint *value);
void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint *value);
void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value);
void glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil);


void glEnablei(GLenum cap, GLuint index);
void glDisablei(GLenum cap, GLuint index);
GLboolean glIsEnabledi(GLenum cap, GLuint index);
void glColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a);


void glGetIntegeri_v(GLenum target, GLuint index, GLint *data);
void glGetInteger64v(GLenum pname, GLint64 *data);
void glGetInteger64i_v(GLenum target, GLuint index, GLint64 *data);
void glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params);


void glCopyBufferSubData(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size);


void glGetUniformIndices(GLuint program, GLsizei uniformCount, const GLchar *const *uniformNames, GLuint *uniformIndices);


void glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer);


void glBindFragDataLocation(GLuint program, GLuint color, const GLchar *name);
GLint glGetFragDataLocation(GLuint program, const GLchar *name);


void glBindFragDataLocationIndexed(GLuint program, GLuint colorNumber, GLuint index, const GLchar *name);
GLint glGetFragDataIndex(GLuint program, const GLchar *name);


void glBeginTransformFeedback(GLenum primitiveMode);
void glEndTransformFeedback(void);
void glPauseTransformFeedback(void);
void glResumeTransformFeedback(void);
void glBindTransformFeedback(GLenum target, GLuint id);
GLboolean glIsTransformFeedback(GLuint id);
void glTransformFeedbackVaryings(GLuint program, GLsizei count, const GLchar *const *varyings, GLenum bufferMode);
void glGetTransformFeedbackVarying(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLsizei *size, GLenum *type, GLchar *name);
void glDrawTransformFeedback(GLenum mode, GLuint id);


void glClampColor(GLenum target, GLenum clamp);


void glProvokingVertex(GLenum mode);


typedef struct __GLsync *GLsync;
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_ALREADY_SIGNALED           0x911A
#define GL_TIMEOUT_EXPIRED            0x911B
#define GL_CONDITION_SATISFIED        0x911C
#define GL_WAIT_FAILED_GL             0x911D
#define GL_SYNC_FLUSH_COMMANDS_BIT    0x00000001
#define GL_TIMEOUT_IGNORED            0xFFFFFFFFFFFFFFFFull
GLsync glFenceSync(GLenum condition, GLbitfield flags);
GLboolean glIsSync(GLsync sync);
void glDeleteSync(GLsync sync);
GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout);
void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout);
void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei *length, GLint *values);


void glGetMultisamplefv(GLenum pname, GLuint index, GLfloat *val);
void glSampleMaski(GLuint maskNumber, GLbitfield mask);
void glTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations);
void glTexImage3DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations);


#define GL_TIME_ELAPSED 0x88BF
#define GL_TIMESTAMP    0x8E28
void glQueryCounter(GLuint id, GLenum target);
void glGetQueryObjecti64v(GLuint id, GLenum pname, GLint64 *params);
void glGetQueryObjectui64v(GLuint id, GLenum pname, GLuint64 *params);


#define GL_INT_2_10_10_10_REV          0x8D9F
#define GL_UNSIGNED_INT_2_10_10_10_REV 0x8368
void glVertexAttribP1ui(GLuint index, GLenum type, GLboolean normalized, GLuint value);
void glVertexAttribP2ui(GLuint index, GLenum type, GLboolean normalized, GLuint value);
void glVertexAttribP3ui(GLuint index, GLenum type, GLboolean normalized, GLuint value);
void glVertexAttribP4ui(GLuint index, GLenum type, GLboolean normalized, GLuint value);
void glVertexAttribP1uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value);
void glVertexAttribP2uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value);
void glVertexAttribP3uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value);
void glVertexAttribP4uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value);


void glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer);


void glFramebufferTexture1D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void glFramebufferTexture3D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset);
void glGetBooleani_v(GLenum target, GLuint index, GLboolean *data);
void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, GLvoid *data);
void glGetCompressedTexImage(GLenum target, GLint level, GLvoid *img);
void glGetPointerv(GLenum pname, GLvoid **params);
void glGetSamplerParameterIiv(GLuint sampler, GLenum pname, GLint *params);
void glGetSamplerParameterIuiv(GLuint sampler, GLenum pname, GLuint *params);
const GLubyte *glGetString(GLenum name);
const GLubyte *glGetStringi(GLenum name, GLuint index);
void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels);
void glGetTexParameterIiv(GLenum target, GLenum pname, GLint *params);
void glGetTexParameterIuiv(GLenum target, GLenum pname, GLuint *params);
void glGetVertexAttribdv(GLuint index, GLenum pname, GLdouble *params);
void glPixelStoref(GLenum pname, GLfloat param);
void glPointParameterf(GLenum pname, GLfloat param);
void glPointParameterfv(GLenum pname, const GLfloat *params);
void glPointParameteri(GLenum pname, GLint param);
void glPointParameteriv(GLenum pname, const GLint *params);
void glSamplerParameterIiv(GLuint sampler, GLenum pname, const GLint *param);
void glSamplerParameterIuiv(GLuint sampler, GLenum pname, const GLuint *param);
void glTexParameterIiv(GLenum target, GLenum pname, const GLint *params);
void glTexParameterIuiv(GLenum target, GLenum pname, const GLuint *params);
void glVertexAttrib1d(GLuint index, GLdouble x);
void glVertexAttrib1dv(GLuint index, const GLdouble *v);
void glVertexAttrib1s(GLuint index, GLshort x);
void glVertexAttrib1sv(GLuint index, const GLshort *v);
void glVertexAttrib2d(GLuint index, GLdouble x, GLdouble y);
void glVertexAttrib2dv(GLuint index, const GLdouble *v);
void glVertexAttrib2s(GLuint index, GLshort x, GLshort y);
void glVertexAttrib2sv(GLuint index, const GLshort *v);
void glVertexAttrib3d(GLuint index, GLdouble x, GLdouble y, GLdouble z);
void glVertexAttrib3dv(GLuint index, const GLdouble *v);
void glVertexAttrib3s(GLuint index, GLshort x, GLshort y, GLshort z);
void glVertexAttrib3sv(GLuint index, const GLshort *v);
void glVertexAttrib4Nbv(GLuint index, const GLbyte *v);
void glVertexAttrib4Niv(GLuint index, const GLint *v);
void glVertexAttrib4Nsv(GLuint index, const GLshort *v);
void glVertexAttrib4Nub(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w);
void glVertexAttrib4Nubv(GLuint index, const GLubyte *v);
void glVertexAttrib4Nuiv(GLuint index, const GLuint *v);
void glVertexAttrib4Nusv(GLuint index, const GLushort *v);
void glVertexAttrib4bv(GLuint index, const GLbyte *v);
void glVertexAttrib4d(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void glVertexAttrib4dv(GLuint index, const GLdouble *v);
void glVertexAttrib4iv(GLuint index, const GLint *v);
void glVertexAttrib4s(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w);
void glVertexAttrib4sv(GLuint index, const GLshort *v);
void glVertexAttrib4ubv(GLuint index, const GLubyte *v);
void glVertexAttrib4uiv(GLuint index, const GLuint *v);
void glVertexAttrib4usv(GLuint index, const GLushort *v);
void glVertexAttribI4bv(GLuint index, const GLbyte *v);
void glVertexAttribI4sv(GLuint index, const GLshort *v);
void glVertexAttribI4ubv(GLuint index, const GLubyte *v);
void glVertexAttribI4usv(GLuint index, const GLushort *v);



struct GX2Texture* gl_get_gx2_texture(GLuint texture);

#ifdef __cplusplus
}
#endif
#endif
