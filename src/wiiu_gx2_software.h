#ifndef SM3DW_WIIU_GX2_SOFTWARE_H
#define SM3DW_WIIU_GX2_SOFTWARE_H

#include <stdbool.h>

#include "common/types.h"
#include "cpu/cpu.h"

typedef struct {
    u32 image;
    u32 image_size;
    u32 width;
    u32 height;
    u32 depth;
    u32 format;
    u32 tile_mode;
    u32 swizzle;
    u32 pitch;
    u32 comp_map;
    u32 first_slice;
} WiiUGX2TextureView;

/* Base-mip normalized 2D sampling for vertex data. Float textures retain
   signed/HDR values; they are never quantized through the UI's BGRA8 path. */
bool wiiu_gx2_software_sample_float(CPUState* cpu,const WiiUGX2TextureView* texture,
    u32 sampler_word,float u,float v,float result[4]);
bool wiiu_gx2_software_sample_bgra(const u8* pixels,const WiiUGX2TextureView* texture,
    u32 sampler_word,float u,float v,float result[4]);
bool wiiu_gx2_software_sample_rgba_float(const float* pixels,const WiiUGX2TextureView* texture,
    u32 sampler_word,float u,float v,float result[4]);

bool wiiu_gx2_software_blit(CPUState* cpu,
                            const WiiUGX2TextureView* texture,
                            u8* destination, u32 destination_width,
                            u32 destination_height, u32 destination_stride,
                            u32 x, u32 y, u32 width, u32 height,
                            bool alpha_blend);

#endif
