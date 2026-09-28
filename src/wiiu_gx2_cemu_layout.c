/*
 * SPDX-License-Identifier: MPL-2.0
 *
 * Derived from Cemu's LatteTextureLoader layout handling:
 * https://github.com/cemu-project/Cemu
 * Commit: 13d1826b4d018c2b6674f58b9b772e21ba084f61
 *
 * This is a deliberately small C adaptation for the standalone recomp
 * renderer. The full Cemu renderer remains in third_party/Cemu.
 */

#include "wiiu_gx2_cemu_layout.h"

enum {
    CEMU_LATTICE_PIPES = 2u,
    CEMU_LATTICE_BANKS = 4u,
    CEMU_LATTICE_PIPE_INTERLEAVE_BITS = 8u,
    CEMU_LATTICE_ROW_SIZE = 2048u,
    CEMU_LATTICE_SWAP_SIZE = 256u,
};

static bool is_block_compressed(u32 format) {
    u32 hw_format = format & 0x3Fu;
    return hw_format >= 0x31u && hw_format <= 0x35u;
}

static u32 bytes_per_block(u32 format) {
    u32 hw_format = format & 0x3Fu;
    return (hw_format == 0x31u || hw_format == 0x34u) ? 8u : 16u;
}

bool wiiu_cemu_normalize_texture_layout(u32 width, u32 height, u32 format,
                                        u32 descriptor_pitch,
                                        u32 image_size, u32* pitch_out,
                                        u32* height_out) {
    if (!pitch_out || !height_out || width == 0u || height == 0u)
        return false;

    if (!is_block_compressed(format)) {
        *pitch_out = descriptor_pitch != 0u ? descriptor_pitch : width;
        *height_out = height;
        return *pitch_out != 0u;
    }

    u32 block_width = (width + 3u) >> 2;
    u32 block_height = (height + 3u) >> 2;
    u32 pitch = descriptor_pitch != 0u ? descriptor_pitch : block_width;

    /*
     * Cemu's texture loader addresses compressed surfaces in blocks. GX2
     * descriptors often retain a texel pitch, so accept the converted form
     * when the unconverted surface cannot fit in the supplied image range.
     */
    if ((pitch & 3u) == 0u && pitch >= block_width * 4u) {
        u32 block_pitch = pitch >> 2;
        u64 original_minimum =
            (u64)pitch * block_height * bytes_per_block(format);
        u64 block_minimum =
            (u64)block_pitch * block_height * bytes_per_block(format);
        if (block_pitch >= block_width &&
            (image_size == 0u ||
             (original_minimum > image_size && block_minimum <= image_size))) {
            pitch = block_pitch;
        }
    }

    if (pitch < block_width)
        pitch = block_width;
    *pitch_out = pitch;
    *height_out = block_height;
    return true;
}

static bool cemu_tile_mode_thick(u32 tile_mode) {
    return tile_mode == 3u || tile_mode == 7u || tile_mode == 13u ||
           tile_mode == 15u;
}

static bool cemu_tile_mode_bank_swapped(u32 tile_mode) {
    return tile_mode == 8u || tile_mode == 9u || tile_mode == 10u ||
           tile_mode == 11u || tile_mode == 14u || tile_mode == 15u;
}

static u32 cemu_micro_tile_pixel_index(u32 x, u32 y, u32 z, u32 bpp,
                                        u32 tile_mode) {
    u32 bits[8] = {0u};
    switch (bpp) {
    case 8u:
        bits[0] = x & 1u;
        bits[1] = (x >> 1) & 1u;
        bits[2] = (x >> 2) & 1u;
        bits[3] = (y >> 1) & 1u;
        bits[4] = y & 1u;
        bits[5] = (y >> 2) & 1u;
        break;
    case 16u:
        bits[0] = x & 1u;
        bits[1] = (x >> 1) & 1u;
        bits[2] = (x >> 2) & 1u;
        bits[3] = y & 1u;
        bits[4] = (y >> 1) & 1u;
        bits[5] = (y >> 2) & 1u;
        break;
    case 32u:
    case 96u:
        bits[0] = x & 1u;
        bits[1] = (x >> 1) & 1u;
        bits[2] = y & 1u;
        bits[3] = (x >> 2) & 1u;
        bits[4] = (y >> 1) & 1u;
        bits[5] = (y >> 2) & 1u;
        break;
    case 64u:
        bits[0] = x & 1u;
        bits[1] = y & 1u;
        bits[2] = (x >> 1) & 1u;
        bits[3] = (x >> 2) & 1u;
        bits[4] = (y >> 1) & 1u;
        bits[5] = (y >> 2) & 1u;
        break;
    case 128u:
        bits[0] = y & 1u;
        bits[1] = x & 1u;
        bits[2] = (x >> 1) & 1u;
        bits[3] = (x >> 2) & 1u;
        bits[4] = (y >> 1) & 1u;
        bits[5] = (y >> 2) & 1u;
        break;
    default:
        bits[0] = x & 1u;
        bits[1] = (x >> 1) & 1u;
        bits[2] = y & 1u;
        bits[3] = (x >> 2) & 1u;
        bits[4] = (y >> 1) & 1u;
        bits[5] = (y >> 2) & 1u;
        break;
    }
    if (cemu_tile_mode_thick(tile_mode)) {
        bits[6] = z & 1u;
        bits[7] = (z >> 1) & 1u;
    }
    u32 index = 0u;
    for (u32 bit = 0u; bit < 8u; bit++)
        index |= bits[bit] << bit;
    return index;
}

static u32 cemu_surface_rotation(u32 tile_mode) {
    if (tile_mode >= 4u && tile_mode <= 11u)
        return CEMU_LATTICE_PIPES * ((CEMU_LATTICE_BANKS >> 1u) - 1u);
    if (tile_mode >= 12u && tile_mode <= 15u)
        return 1u;
    return 0u;
}

static u32 cemu_bank_swap_width(u32 tile_mode, u32 bpp, u32 pitch) {
    if (!cemu_tile_mode_bank_swapped(tile_mode) || bpp == 0u || pitch == 0u)
        return 0u;

    u32 bytes_per_sample = 8u * bpp;
    u32 samples_per_tile = CEMU_LATTICE_SWAP_SIZE / bytes_per_sample;
    u32 slices_per_tile = samples_per_tile == 0u ? 1u : 1u;
    u32 bytes_per_tile_slice = bytes_per_sample / slices_per_tile;
    u32 aspect_ratio = (tile_mode == 5u || tile_mode == 9u)
                           ? 2u
                           : (tile_mode == 6u || tile_mode == 10u) ? 4u : 1u;
    u32 swap_tiles = (CEMU_LATTICE_SWAP_SIZE >> 1u) / bpp;
    if (swap_tiles == 0u)
        swap_tiles = 1u;
    u64 swap_width =
        (u64)swap_tiles * 8u * CEMU_LATTICE_BANKS;
    u64 height_bytes =
        (u64)aspect_ratio * CEMU_LATTICE_PIPES * bpp / slices_per_tile;
    if (height_bytes == 0u)
        return 0u;
    u64 swap_max = (u64)CEMU_LATTICE_PIPES * CEMU_LATTICE_BANKS *
                   CEMU_LATTICE_ROW_SIZE / height_bytes;
    u64 swap_min = (u64)(1u << CEMU_LATTICE_PIPE_INTERLEAVE_BITS) * 8u *
                   CEMU_LATTICE_BANKS / bytes_per_tile_slice;
    u64 width = swap_max >= swap_width
                    ? (swap_width > swap_min ? swap_width : swap_min)
                    : swap_max;
    while (width >= (u64)pitch * 2u)
        width >>= 1u;
    return width <= 0xFFFFFFFFull ? (u32)width : 0u;
}

static bool cemu_store_offset(u64 value, u32* offset_out) {
    if (!offset_out || value > 0xFFFFFFFFull)
        return false;
    *offset_out = (u32)value;
    return true;
}

static bool cemu_linear_offset(u32 x, u32 y, u32 slice, u32 bpp, u32 pitch,
                               u32 height, u32* offset_out) {
    u64 pixel = (u64)x + (u64)pitch * y +
                (u64)slice * height * pitch;
    return cemu_store_offset(pixel * bpp / 8u, offset_out);
}

static bool cemu_micro_tiled_offset(u32 x, u32 y, u32 slice, u32 bpp,
                                    u32 pitch, u32 height, u32 tile_mode,
                                    u32* offset_out) {
    u32 thickness = cemu_tile_mode_thick(tile_mode) ? 4u : 1u;
    u64 tile_bytes = (u64)thickness * ((bpp * 64u + 7u) >> 3u);
    u64 tile_offset =
        tile_bytes * ((x >> 3u) + (u64)(pitch >> 3u) * (y >> 3u));
    u64 slice_bytes = ((u64)height * pitch * thickness * bpp + 7u) / 8u;
    u64 slice_offset = slice_bytes * (slice / thickness);
    u32 pixel = cemu_micro_tile_pixel_index(x, y, slice, bpp, tile_mode);
    return cemu_store_offset(tile_offset + slice_offset +
                                 ((u64)bpp * pixel >> 3u),
                             offset_out);
}

static bool cemu_macro_tiled_offset(u32 x, u32 y, u32 slice, u32 bpp,
                                    u32 pitch, u32 height, u32 tile_mode,
                                    u32 swizzle, u32* offset_out) {
    static const u32 bank_swap_order[4] = {0u, 1u, 3u, 2u};
    u32 thickness = cemu_tile_mode_thick(tile_mode) ? 4u : 1u;
    u32 pixel = cemu_micro_tile_pixel_index(x, y, slice, bpp, tile_mode);
    u64 element_offset = (u64)bpp * pixel >> 3u;

    u32 pipe = ((y >> 3u) ^ (x >> 3u)) & 1u;
    u32 bank = (y >> 4u) & 3u;
    bank = ((bank >> 1u) | (bank << 1u)) & 3u;
    bank = (bank ^ (x >> 3u)) & 3u;
    u32 bank_pipe = pipe + CEMU_LATTICE_PIPES * bank;
    u32 pipe_swizzle = (swizzle >> 8u) & 1u;
    u32 bank_swizzle = (swizzle >> 9u) & 3u;
    u32 slice_in = cemu_tile_mode_thick(tile_mode) ? slice >> 2u : slice;
    bank_pipe ^= pipe_swizzle + CEMU_LATTICE_PIPES * bank_swizzle +
                 slice_in * cemu_surface_rotation(tile_mode);
    bank_pipe %= CEMU_LATTICE_PIPES * CEMU_LATTICE_BANKS;
    pipe = bank_pipe % CEMU_LATTICE_PIPES;
    bank = bank_pipe / CEMU_LATTICE_PIPES;

    u64 slice_bytes = ((u64)height * pitch * thickness * bpp + 7u) / 8u;
    u64 slice_offset = slice_bytes * (slice / thickness);
    u32 macro_pitch = 8u * CEMU_LATTICE_BANKS;
    u32 macro_height = 8u * CEMU_LATTICE_PIPES;
    if (tile_mode == 5u || tile_mode == 9u) {
        macro_pitch >>= 1u;
        macro_height <<= 1u;
    } else if (tile_mode == 6u || tile_mode == 10u) {
        macro_pitch >>= 2u;
        macro_height <<= 2u;
    }
    if (macro_pitch == 0u || macro_height == 0u)
        return false;

    u64 macro_tile_bytes =
        ((u64)thickness * bpp * macro_height * macro_pitch + 7u) / 8u;
    u32 macro_tile_x = x / macro_pitch;
    u32 macro_tile_y = y / macro_height;
    u64 macro_tile_offset =
        ((u64)macro_tile_x + (u64)(pitch / macro_pitch) * macro_tile_y) *
        macro_tile_bytes;
    if (cemu_tile_mode_bank_swapped(tile_mode)) {
        u32 swap_width = cemu_bank_swap_width(tile_mode, bpp, pitch);
        if (swap_width != 0u) {
            u32 swap_index = macro_pitch * macro_tile_x / swap_width;
            bank ^= bank_swap_order[swap_index & (CEMU_LATTICE_BANKS - 1u)];
        }
    }

    u64 macro_slice_offset = (macro_tile_offset + slice_offset) >> 3u;
    macro_slice_offset += element_offset;
    u64 macro_high =
        macro_slice_offset & ~((1ull << CEMU_LATTICE_PIPE_INTERLEAVE_BITS) - 1ull);
    u64 macro_low =
        macro_slice_offset & ((1ull << CEMU_LATTICE_PIPE_INTERLEAVE_BITS) - 1ull);
    u64 pipe_offset = (u64)pipe << CEMU_LATTICE_PIPE_INTERLEAVE_BITS;
    u64 bank_offset =
        (u64)bank << (1u + CEMU_LATTICE_PIPE_INTERLEAVE_BITS);
    return cemu_store_offset((macro_high << 3u) | macro_low | pipe_offset |
                                 bank_offset,
                             offset_out);
}

bool wiiu_cemu_surface_offset(u32 x, u32 y, u32 slice, u32 bpp, u32 pitch,
                              u32 height, u32 tile_mode, u32 swizzle,
                              u32* offset_out) {
    if (!offset_out || bpp == 0u || pitch == 0u || height == 0u)
        return false;

    if (tile_mode == 16u || tile_mode == 32u || tile_mode <= 1u)
        return cemu_linear_offset(x, y, slice, bpp, pitch, height,
                                  offset_out);

    u32 hardware_tile_mode = tile_mode & 0xFu;
    if (hardware_tile_mode <= 3u)
        return cemu_micro_tiled_offset(x, y, slice, bpp, pitch, height,
                                       hardware_tile_mode, offset_out);
    return cemu_macro_tiled_offset(x, y, slice, bpp, pitch, height,
                                   hardware_tile_mode, swizzle, offset_out);
}
