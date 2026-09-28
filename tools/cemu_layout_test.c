#include "wiiu_gx2_cemu_layout.h"
#include <stdio.h>
#include <stdlib.h>

static void expect_offset(const char* name, u32 x, u32 y, u32 bpp, u32 pitch,
                          u32 height, u32 tile, u32 swizzle, u32 expected) {
    u32 actual = 0;
    if (!wiiu_cemu_surface_offset(x, y, 0, bpp, pitch, height, tile, swizzle, &actual) || actual != expected) {
        fprintf(stderr, "%s: expected 0x%X, got 0x%X\n", name, expected, actual);
        exit(1);
    }
}

int main(void) {
    expect_offset("linear-origin", 0, 0, 32, 128, 64, 1, 0, 0);
    expect_offset("linear-next-pixel", 1, 0, 32, 128, 64, 1, 0, 4);
    expect_offset("linear-next-row", 0, 1, 32, 128, 64, 1, 0, 512);
    expect_offset("micro-origin", 0, 0, 32, 128, 64, 2, 0, 0);
    expect_offset("micro-x1", 1, 0, 32, 128, 64, 2, 0, 4);
    expect_offset("micro-y1", 0, 1, 32, 128, 64, 2, 0, 16);
    puts("Cemu layout tests passed.");
    return 0;
}
