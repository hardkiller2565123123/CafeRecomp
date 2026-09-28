#ifndef SM3DW_WIIU_GX2_CEMU_LAYOUT_H
#define SM3DW_WIIU_GX2_CEMU_LAYOUT_H

#include <stdbool.h>

#include "common/types.h"

/*
 * Produces the address-space dimensions expected by Latte's tiled surface
 * layout. Compressed GX2 descriptors commonly report pitch in texels while
 * the tile address calculation operates on 4x4 blocks.
 */
bool wiiu_cemu_normalize_texture_layout(u32 width, u32 height, u32 format,
                                        u32 descriptor_pitch,
                                        u32 image_size, u32* pitch_out,
                                        u32* height_out);

/*
 * Translates a Latte texture coordinate into the byte address used by GX2
 * surfaces. This is the single-sample, color-surface portion of Cemu's
 * LatteAddrLib algorithm and covers linear, micro-tiled, and macro-tiled
 * textures used by the standalone renderer.
 */
bool wiiu_cemu_surface_offset(u32 x, u32 y, u32 slice, u32 bpp, u32 pitch,
                              u32 height, u32 tile_mode, u32 swizzle,
                              u32* offset_out);

#endif
