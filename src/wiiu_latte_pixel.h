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
#define WIIU_PIXEL_TEXTURES 32
typedef struct { uint16_t bank, vector; } WiiULattePixelConstant;
typedef struct {
    char hlsl[65536];
    uint8_t input_mask[WIIU_PIXEL_INPUTS];
    uint32_t texture_mask;
    uint32_t array_texture_mask;
    uint32_t volume_texture_mask;
    bool relative_uniforms;
    uint16_t sampler_mask;
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
bool wiiu_latte_translate_pixel_position(const uint8_t* bytes, size_t size,
    unsigned input_count, unsigned position_register, WiiULattePixelProgram* result);
bool wiiu_latte_translate_pixel_layout(const uint8_t* bytes, size_t size,
    unsigned input_count, unsigned position_register, uint32_t array_mask,
    WiiULattePixelProgram* result);
bool wiiu_latte_translate_pixel_interpolation(const uint8_t* bytes, size_t size,
    unsigned input_count, unsigned position_register, uint32_t array_mask,
    uint32_t linear_mask, WiiULattePixelProgram* result);
bool wiiu_latte_translate_pixel_resources(const uint8_t* bytes, size_t size,
    unsigned input_count, unsigned position_register, uint32_t array_mask,
    uint32_t linear_mask, uint32_t volume_mask, WiiULattePixelProgram* result);
bool wiiu_latte_translate_pixel_targets(const uint8_t* bytes, size_t size,
    unsigned input_count, unsigned position_register, uint32_t array_mask,
    uint32_t linear_mask, uint32_t volume_mask, uint8_t targets,
    WiiULattePixelProgram* result);
bool wiiu_latte_translate_pixel_channels(const uint8_t* bytes, size_t size,
    unsigned input_count, unsigned position_register, uint32_t array_mask,
    uint32_t linear_mask, uint32_t volume_mask, uint32_t channels,
    WiiULattePixelProgram* result);
#endif
