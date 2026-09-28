#ifndef SM3DW_WIIU_LATTE_VERTEX_H
#define SM3DW_WIIU_LATTE_VERTEX_H

#include <stdbool.h>

#include "cpu/cpu.h"

/*
 * A compact native decode of the common GX2 vertex-matrix pattern. It keeps
 * the host renderer independent from any emulator shader runtime.
 */
typedef struct {
    bool valid;
    u32 descriptor;
    u32 program;
    u32 program_size;
    u8 input_gpr;
    u16 uniform_vectors[4];
} WiiULatteVertexTransform;

bool wiiu_latte_vertex_transform_decode(CPUState* cpu, u32 descriptor,
                                        WiiULatteVertexTransform* transform);
bool wiiu_latte_vertex_transform_apply(
    const WiiULatteVertexTransform* transform, const u32* uniform_words,
    const u8* uniform_valid, u32 uniform_word_count, const float input[4],
    float output[4]);

/* BOTW's compact UV-only layout shader builds position before its optional
   color/UV branches. Decode that position prefix separately from glyph MVPs. */
bool wiiu_latte_layout_position(CPUState* cpu, u32 descriptor,
    const u32* uniforms, const u8* valid, u32 count,
    const float uv[2], float clip[4]);

#endif
