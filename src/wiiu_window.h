#ifndef SM3DW_WIIU_WINDOW_H
#define SM3DW_WIIU_WINDOW_H

#include <stdbool.h>
#include <stdint.h>
#include "wiiu_latte_pixel.h"

typedef struct {
    float x;
    float y;
    float u;
    float v;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
} WiiUWindowGpuVertex;

typedef struct {
    bool valid;
    uint32_t color_source_factor;
    uint32_t color_destination_factor;
    uint32_t color_operation;
    bool separate_alpha;
    uint32_t alpha_source_factor;
    uint32_t alpha_destination_factor;
    uint32_t alpha_operation;
    bool color_control_valid;
    bool blend_enabled;
    bool color_enabled;
    bool write_mask_valid;
    uint8_t write_mask;
    bool constant_valid;
    float constant[4];
} WiiUWindowGpuBlendControl;

typedef struct {
    float bias[4];
    float scale[4];
    float font_coverage;
    float padding[3];
} WiiUWindowGpuMaterial;

/* Clip-space vertices retain W so the GPU clips and interpolates correctly.
   These passes consume real vertex-shader exports, not fullscreen guesses. */
typedef struct {
    float position[4];
    float parameters[WIIU_PIXEL_INPUTS][4];
} WiiUWindowPostVertex;
typedef struct {
    const uint8_t* pixels;
    uint32_t width,height;
    uint64_t serial;
    bool sampler_valid;
    uint32_t sampler_word0;
} WiiUWindowEffectTexture;
typedef struct {
    const WiiULattePixelProgram* program;
    WiiUWindowEffectTexture textures[16];
    uint32_t source_surfaces[16];
    uint32_t targets[8];
    WiiUWindowGpuBlendControl blends[8];
    /* First 16 vectors are texture component maps; remaining vectors are
       the translator's compact, draw-current uniform references. */
    uint32_t constants[16+WIIU_PIXEL_CONSTANTS][4];
    const uint8_t* uniform_blocks[16];
    uint32_t uniform_block_sizes[16];
} WiiUWindowSceneMaterial;
bool wiiu_window_gpu_draw_scene(uint32_t target,uint32_t width,uint32_t height,
    const WiiUWindowSceneMaterial* material,const WiiUWindowPostVertex* vertices,
    uint32_t count,const float viewport[4],const WiiUWindowGpuBlendControl* blend,
    bool scissor_valid,uint32_t sx,uint32_t sy,uint32_t sw,uint32_t sh);
bool wiiu_window_gpu_draw_effect(uint32_t target,uint32_t width,uint32_t height,
    const WiiUWindowEffectTexture textures[3],const WiiUWindowPostVertex* vertices,
    uint32_t count,uint32_t mode,const float viewport[4],const WiiUWindowGpuBlendControl* blend,
    bool scissor_valid,uint32_t sx,uint32_t sy,uint32_t sw,uint32_t sh);
bool wiiu_window_gpu_draw_postprocess(uint32_t target, uint32_t width,
    uint32_t height, uint32_t source_surface, const uint8_t* pixels,
    uint32_t texture_width, uint32_t texture_height, uint64_t texture_serial,
    const WiiUWindowPostVertex* vertices, uint32_t vertex_count,
    uint32_t mode, uint32_t component_map, const float viewport[4],
    const WiiUWindowGpuBlendControl* blend, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height);

bool wiiu_window_gpu_draw_material(
    uint32_t surface, uint32_t target_width, uint32_t target_height,
    const uint8_t* texture_pixels, uint32_t texture_width,
    uint32_t texture_height, uint64_t texture_serial,
    const WiiUWindowGpuVertex* vertices, uint32_t quad_count,
    const WiiUWindowGpuMaterial* material,
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height);

void wiiu_window_show(const char* reason);
void wiiu_window_pump(void);
bool wiiu_window_is_closed(void);
void wiiu_window_advance_input_frame(void);
void wiiu_window_present_solid(uint32_t width, uint32_t height, uint8_t red,
                               uint8_t green, uint8_t blue, uint8_t alpha);
void wiiu_window_present_bgra(uint32_t width, uint32_t height,
                              const uint8_t* pixels, uint32_t stride);
bool wiiu_window_gpu_clear(uint32_t surface, uint32_t width, uint32_t height,
                           uint8_t red, uint8_t green, uint8_t blue,
                           uint8_t alpha);
bool wiiu_window_gpu_configure(uint32_t surface,uint32_t width,uint32_t height,uint32_t format);
bool wiiu_window_gpu_clear_float(uint32_t surface,uint32_t width,uint32_t height,const float color[4]);
bool wiiu_window_gpu_readback_float(uint32_t surface,float* pixels,uint32_t width,uint32_t height);
bool wiiu_window_gpu_upload_bgra(uint32_t surface, uint32_t width,
                                 uint32_t height, const uint8_t* pixels,
                                 uint32_t stride);
bool wiiu_window_gpu_draw_bgra(
    uint32_t surface, uint32_t target_width, uint32_t target_height,
    const uint8_t* texture_pixels, uint32_t texture_width,
    uint32_t texture_height, uint64_t texture_serial,
    const WiiUWindowGpuVertex vertices[4],
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height);
bool wiiu_window_gpu_draw_bgra_quads(
    uint32_t surface, uint32_t target_width, uint32_t target_height,
    const uint8_t* texture_pixels, uint32_t texture_width,
    uint32_t texture_height, uint64_t texture_serial,
    const WiiUWindowGpuVertex* vertices, uint32_t quad_count,
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height);
bool wiiu_window_gpu_draw_bgra_color(
    uint32_t surface, uint32_t target_width, uint32_t target_height,
    const uint8_t* texture_pixels, uint32_t texture_width,
    uint32_t texture_height, uint64_t texture_serial,
    const WiiUWindowGpuVertex vertices[4],
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height);
bool wiiu_window_gpu_draw_surface(
    uint32_t target_surface, uint32_t target_width, uint32_t target_height,
    uint32_t source_surface, const WiiUWindowGpuVertex vertices[4],
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y,
    uint32_t scissor_width, uint32_t scissor_height);
bool wiiu_window_gpu_readback(uint32_t surface, uint8_t* pixels,
                              uint32_t width, uint32_t height,
                              uint32_t stride);
bool wiiu_window_gpu_draw_surface_material(
    uint32_t target_surface, uint32_t target_width, uint32_t target_height,
    uint32_t source_surface, const WiiUWindowGpuVertex vertices[4],
    const WiiUWindowGpuMaterial* material,
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y,
    uint32_t scissor_width, uint32_t scissor_height);
bool wiiu_window_gpu_present(uint32_t surface, uint32_t width,
                             uint32_t height);
void wiiu_window_gpu_invalidate(uint32_t surface);
bool wiiu_window_gpu_finish(void);
void wiiu_window_read_input(uint32_t* held, uint32_t* pressed,
                            uint32_t* released, float* left_x,
                            float* left_y);
void wiiu_window_read_touch(uint16_t* raw_x, uint16_t* raw_y,
                            uint32_t* touch);

#endif
