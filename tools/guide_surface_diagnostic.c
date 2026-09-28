/*
 * Small offline diagnostic for a captured 1280x720 Wii U RGB565 guide
 * surface. It shares the Cemu-derived Latte address implementation used by
 * the standalone renderer, but is never linked into the game executable.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "wiiu_gx2_cemu_layout.h"

enum {
    SURFACE_WIDTH = 1280,
    SURFACE_HEIGHT = 720,
    SURFACE_PITCH = 1280,
    PANEL_SCALE = 4,
    PANEL_WIDTH = SURFACE_WIDTH / PANEL_SCALE,
    PANEL_HEIGHT = SURFACE_HEIGHT / PANEL_SCALE,
    PANEL_COLUMNS = 4,
    PANEL_ROWS = 3,
    OUTPUT_WIDTH = PANEL_WIDTH * PANEL_COLUMNS,
    OUTPUT_HEIGHT = PANEL_HEIGHT * PANEL_ROWS,
};

typedef struct {
    const char* name;
    u32 tile_mode;
    u32 swizzle;
    bool linear;
} LayoutVariant;

static const LayoutVariant k_variants[PANEL_COLUMNS * PANEL_ROWS] = {
    {"linear", 0u, 0u, true},
    {"micro-tiled-2", 2u, 0u, false},
    {"micro-tiled-3", 3u, 0u, false},
    {"macro-4-swizzle-000", 4u, 0x000u, false},
    {"macro-4-swizzle-100", 4u, 0x100u, false},
    {"macro-4-swizzle-200", 4u, 0x200u, false},
    {"macro-4-swizzle-300", 4u, 0x300u, false},
    {"macro-4-swizzle-400", 4u, 0x400u, false},
    {"macro-4-swizzle-500", 4u, 0x500u, false},
    {"macro-4-swizzle-600", 4u, 0x600u, false},
    {"macro-4-swizzle-700", 4u, 0x700u, false},
    {"macro-4-address-xor-300", 4u, 0x300u, false},
};

static u16 read_le16(const u8* bytes) {
    return (u16)bytes[0] | (u16)((u16)bytes[1] << 8u);
}

static void rgb565_to_bgra(u16 value, u8* out) {
    out[2] = (u8)((value & 31u) * 255u / 31u);
    out[1] = (u8)(((value >> 5u) & 63u) * 255u / 63u);
    out[0] = (u8)(((value >> 11u) & 31u) * 255u / 31u);
    out[3] = 255u;
}

static bool read_file(const char* path, u8** data_out, size_t* size_out) {
    FILE* file = fopen(path, "rb");
    if (!file)
        return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    u8* data = (u8*)malloc((size_t)length);
    if (!data) {
        fclose(file);
        return false;
    }
    bool ok = fread(data, 1u, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!ok) {
        free(data);
        return false;
    }
    *data_out = data;
    *size_out = (size_t)length;
    return true;
}

static bool write_bmp(const char* path, const u8* pixels) {
    const u32 image_bytes = OUTPUT_WIDTH * OUTPUT_HEIGHT * 4u;
    const u32 file_size = 54u + image_bytes;
    const u8 header[54] = {
        'B', 'M',
        (u8)(file_size & 0xFFu), (u8)((file_size >> 8u) & 0xFFu),
        (u8)((file_size >> 16u) & 0xFFu), (u8)((file_size >> 24u) & 0xFFu),
        0, 0, 0, 0, 54, 0, 0, 0,
        40, 0, 0, 0,
        (u8)(OUTPUT_WIDTH & 0xFFu), (u8)((OUTPUT_WIDTH >> 8u) & 0xFFu),
        0, 0,
        (u8)(OUTPUT_HEIGHT & 0xFFu), (u8)((OUTPUT_HEIGHT >> 8u) & 0xFFu),
        0, 0,
        1, 0, 32, 0,
        0, 0, 0, 0,
        (u8)(image_bytes & 0xFFu), (u8)((image_bytes >> 8u) & 0xFFu),
        (u8)((image_bytes >> 16u) & 0xFFu),
        (u8)((image_bytes >> 24u) & 0xFFu),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    FILE* file = fopen(path, "wb");
    if (!file)
        return false;
    bool ok = fwrite(header, 1u, sizeof(header), file) == sizeof(header);
    for (u32 row = 0; ok && row < OUTPUT_HEIGHT; row++) {
        const u8* source = pixels +
                           (size_t)(OUTPUT_HEIGHT - row - 1u) *
                               OUTPUT_WIDTH * 4u;
        ok = fwrite(source, 1u, OUTPUT_WIDTH * 4u, file) ==
             OUTPUT_WIDTH * 4u;
    }
    fclose(file);
    return ok;
}

static bool sample_variant(const u8* source, size_t source_size,
                           const LayoutVariant* variant, u32 x, u32 y,
                           u8* output) {
    u32 offset;
    if (variant->linear) {
        offset = (y * SURFACE_PITCH + x) * 2u;
    } else if (!wiiu_cemu_surface_offset(x, y, 0u, 16u, SURFACE_PITCH,
                                         SURFACE_HEIGHT, variant->tile_mode,
                                         variant->swizzle, &offset)) {
        return false;
    }
    if (variant == &k_variants[11])
        offset ^= 0x300u;
    if ((size_t)offset + 2u > source_size)
        return false;
    rgb565_to_bgra(read_le16(source + offset), output);
    return true;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <captured-rgb565.bin> <contact-sheet.bmp>\n",
                argv[0]);
        return 2;
    }

    u8* source = NULL;
    size_t source_size = 0;
    if (!read_file(argv[1], &source, &source_size)) {
        fprintf(stderr, "could not read %s\n", argv[1]);
        return 1;
    }
    u8* output = (u8*)calloc((size_t)OUTPUT_WIDTH * OUTPUT_HEIGHT, 4u);
    if (!output) {
        free(source);
        return 1;
    }

    for (u32 index = 0; index < PANEL_COLUMNS * PANEL_ROWS; index++) {
        const LayoutVariant* variant = &k_variants[index];
        u32 panel_x = (index % PANEL_COLUMNS) * PANEL_WIDTH;
        u32 panel_y = (index / PANEL_COLUMNS) * PANEL_HEIGHT;
        printf("panel %u: %s\n", index, variant->name);
        for (u32 y = 0; y < PANEL_HEIGHT; y++) {
            for (u32 x = 0; x < PANEL_WIDTH; x++) {
                u8* pixel = output +
                            ((size_t)(panel_y + y) * OUTPUT_WIDTH +
                             panel_x + x) *
                                4u;
                sample_variant(source, source_size, variant,
                               x * PANEL_SCALE + PANEL_SCALE / 2u,
                               y * PANEL_SCALE + PANEL_SCALE / 2u, pixel);
            }
        }
    }

    bool ok = write_bmp(argv[2], output);
    free(output);
    free(source);
    if (!ok) {
        fprintf(stderr, "could not write %s\n", argv[2]);
        return 1;
    }
    return 0;
}
