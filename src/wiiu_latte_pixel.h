#ifndef WIIU_LATTE_PIXEL_H
#define WIIU_LATTE_PIXEL_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Bounded PS translation with structured forward if/else. Unsupported instructions, uninitialized
   lanes, relative addressing and depth exports fail before any draw. Color
   exports support eight simultaneous render targets and burst instructions. */
#define WIIU_PIXEL_INPUTS 16
#define WIIU_PIXEL_CONSTANTS 256
typedef struct { uint16_t bank, vector; } WiiULattePixelConstant;
typedef struct {
    char hlsl[65536];
    uint8_t input_mask[WIIU_PIXEL_INPUTS];
    uint16_t texture_mask, sampler_mask;
    uint16_t block_mask;
    uint8_t target_mask;
    uint32_t key;
    unsigned constant_count;
    /* Byte offset of the instruction being translated, including on failure. */
    unsigned instruction_offset;
    WiiULattePixelConstant constants[WIIU_PIXEL_CONSTANTS];
} WiiULattePixelProgram;
bool wiiu_latte_translate_pixel(const uint8_t* bytes, size_t size,
    unsigned input_count, WiiULattePixelProgram* result);
#endif
