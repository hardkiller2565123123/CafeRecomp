#ifndef SM3DW_WIIU_GX2_H
#define SM3DW_WIIU_GX2_H

#include <stdbool.h>

#include "cpu/cpu.h"

void wiiu_gx2_reset(void);
bool wiiu_gx2_has_active_frame(void);
/* Diagnostic copy of the captured sampler registers (not guest pointers). */
bool wiiu_gx2_get_pixel_sampler(u32 slot,u32 words[3]);
bool wiiu_gx2_sample_vertex_texture(void* cpu,unsigned texture,unsigned sampler,
    float u,float v,float lod,float result[4]);
void wiiu_gx2_handle_import(CPUState* cpu, const char* name);
/* Diagnostic lookup of the image last bound through this descriptor. */
bool wiiu_gx2_readback_color_buffer(u32 descriptor,u8* pixels,u32 width,u32 height,u32 stride);

#endif
