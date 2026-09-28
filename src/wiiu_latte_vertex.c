#include "wiiu_latte_vertex.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "wiiu_memory.h"

enum {
    GX2_VERTEX_SHADER_DESCRIPTOR_SIZE = 0x134u,
    GX2_VERTEX_SHADER_PROGRAM_SIZE_OFFSET = 0xD0u,
    GX2_VERTEX_SHADER_PROGRAM_OFFSET = 0xD4u,
    LATTE_CF_ALU_THRESHOLD = 0x40u,
    LATTE_ALU_OP3_THRESHOLD = 0x08u,
    LATTE_ALU_OP2_DOT4 = 0x050u,
    LATTE_ALU_OP2_DOT4_IEEE = 0x051u,
    LATTE_ALU_SOURCE_GPR_LIMIT = 128u,
    LATTE_ALU_SOURCE_UNIFORM_BASE = 256u,
    LATTE_ALU_SOURCE_UNIFORM_LIMIT = 512u,
    LATTE_ALU_SOURCE_PV = 254u,
    LATTE_ALU_SOURCE_LITERAL = 253u,
    LATTE_MAX_GROUP_INSTRUCTIONS = 128u,
};

typedef struct {
    u32 word0;
    u32 word1;
} LatteAluInstruction;

static bool guest_range_valid(CPUState* cpu, u32 address, u32 size) {
    if (!cpu || size == 0u)
        return false;
    u64 end = (u64)address + size;
    if (end > 0x100000000ULL)
        return false;
    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    return memory && wiiu_memory_find(memory, address, size) != NULL;
}

static bool read_program_word_le(CPUState* cpu, u32 address, u32* out) {
    if (!out || !guest_range_valid(cpu, address, 4u))
        return false;
    *out = (u32)mem_read8(cpu, address + 0u) |
           ((u32)mem_read8(cpu, address + 1u) << 8u) |
           ((u32)mem_read8(cpu, address + 2u) << 16u) |
           ((u32)mem_read8(cpu, address + 3u) << 24u);
    return true;
}

static bool read_alu_instruction(CPUState* cpu, u32 program, u32 slot,
                                 LatteAluInstruction* out) {
    if (!out || slot > 0x1FFFFFFFu)
        return false;
    u32 address = program + slot * 8u;
    return read_program_word_le(cpu, address, &out->word0) &&
           read_program_word_le(cpu, address + 4u, &out->word1);
}

static bool alu_is_dot4(const LatteAluInstruction* instruction) {
    if (!instruction)
        return false;
    u32 op3 = (instruction->word1 >> 13u) & 0x1Fu;
    if (op3 >= LATTE_ALU_OP3_THRESHOLD)
        return false;
    u32 op2 = (instruction->word1 >> 7u) & 0x7FFu;
    return op2 == LATTE_ALU_OP2_DOT4 || op2 == LATTE_ALU_OP2_DOT4_IEEE;
}

static bool alu_is_op3(const LatteAluInstruction* instruction) {
    return instruction && ((instruction->word1 >> 13u) & 0x1Fu) >=
                              LATTE_ALU_OP3_THRESHOLD;
}

static u32 alu_source_select(const LatteAluInstruction* instruction,
                             u32 source) {
    if (source == 0u)
        return instruction->word0 & 0x1FFu;
    if (source == 1u)
        return (instruction->word0 >> 13u) & 0x1FFu;
    return instruction->word1 & 0x1FFu;
}

static u32 alu_source_channel(const LatteAluInstruction* instruction,
                              u32 source) {
    if (source == 0u)
        return (instruction->word0 >> 10u) & 0x3u;
    if (source == 1u)
        return (instruction->word0 >> 23u) & 0x3u;
    return (instruction->word1 >> 10u) & 0x3u;
}

static bool alu_writes_gpr(const LatteAluInstruction* instruction) {
    return instruction && ((instruction->word1 >> 4u) & 1u) != 0u;
}

static u32 alu_destination_gpr(const LatteAluInstruction* instruction) {
    return (instruction->word1 >> 21u) & 0x7Fu;
}

static u32 alu_destination_component(const LatteAluInstruction* instruction) {
    return (instruction->word1 >> 29u) & 0x3u;
}

static bool alu_last_in_group(const LatteAluInstruction* instruction) {
    return instruction && ((instruction->word0 >> 31u) & 1u) != 0u;
}

static u8 alu_literal_mask(const LatteAluInstruction* instruction) {
    if (!instruction)
        return 0u;
    u8 mask = 0u;
    u32 source_count = alu_is_op3(instruction) ? 3u : 2u;
    for (u32 source = 0u; source < source_count; source++) {
        if (alu_source_select(instruction, source) == LATTE_ALU_SOURCE_LITERAL)
            mask |= (u8)(1u << alu_source_channel(instruction, source));
    }
    return mask;
}

static bool decode_dot4_row(const LatteAluInstruction instructions[4],
                            u8* input_gpr, u8* output_gpr,
                            u8* output_component,
                            u16* uniform_vector) {
    if (!instructions || !input_gpr || !output_gpr || !output_component ||
        !uniform_vector) {
        return false;
    }

    u32 write_index = 4u;
    u32 position_register = UINT32_MAX;
    u32 uniform_register = UINT32_MAX;
    for (u32 component = 0u; component < 4u; component++) {
        const LatteAluInstruction* instruction = &instructions[component];
        if (!alu_is_dot4(instruction))
            return false;

        u32 input_select = alu_source_select(instruction, 0u);
        u32 input_channel = alu_source_channel(instruction, 0u);
        u32 uniform_select = alu_source_select(instruction, 1u);
        u32 uniform_channel = alu_source_channel(instruction, 1u);
        if (uniform_select < LATTE_ALU_SOURCE_UNIFORM_BASE ||
            uniform_select >= LATTE_ALU_SOURCE_UNIFORM_LIMIT ||
            uniform_channel != component) {
            return false;
        }
        u32 vector = uniform_select - LATTE_ALU_SOURCE_UNIFORM_BASE;
        if (uniform_register == UINT32_MAX)
            uniform_register = vector;
        else if (uniform_register != vector) {
            return false;
        }

        if (component < 3u) {
            if (input_select >= LATTE_ALU_SOURCE_GPR_LIMIT ||
                input_channel != component) {
                return false;
            }
            if (position_register == UINT32_MAX)
                position_register = input_select;
            else if (position_register != input_select) {
                return false;
            }
        } else {
            /*
             * Wii U UI shaders commonly source homogeneous W from the
             * previous-vector register or from a temporary containing 1.0.
             * The input stream supplies XYZ, so the native path provides W.
             */
            bool homogeneous_w =
                input_channel == 3u &&
                (input_select == position_register ||
                 input_select == LATTE_ALU_SOURCE_PV ||
                 input_select < LATTE_ALU_SOURCE_GPR_LIMIT);
            if (!homogeneous_w)
                return false;
        }

        if (alu_writes_gpr(instruction)) {
            if (write_index != 4u)
                return false;
            write_index = component;
        }
    }

    if (write_index == 4u || position_register == UINT32_MAX ||
        uniform_register == UINT32_MAX) {
        return false;
    }

    *input_gpr = (u8)position_register;
    *output_gpr = (u8)alu_destination_gpr(&instructions[write_index]);
    *output_component =
        (u8)alu_destination_component(&instructions[write_index]);
    *uniform_vector = (u16)uniform_register;
    return true;
}

static void scan_alu_group(const LatteAluInstruction* instructions,
                           u32 instruction_count, bool found[4],
                           u8* input_gpr, u8* output_gpr,
                           u16 uniform_vectors[4]) {
    if (!instructions || instruction_count < 4u)
        return;
    for (u32 first = 0u; first + 4u <= instruction_count; first++) {
        u8 row_input_gpr;
        u8 row_output_gpr;
        u8 output_component;
        u16 uniform_vector;
        if (!decode_dot4_row(&instructions[first], &row_input_gpr,
                             &row_output_gpr, &output_component,
                             &uniform_vector)) {
            continue;
        }
        if (output_component >= 4u)
            continue;
        if (!found[output_component]) {
            if ((*input_gpr != 0xFFu && *input_gpr != row_input_gpr) ||
                (*output_gpr != 0xFFu && *output_gpr != row_output_gpr)) {
                continue;
            }
            *input_gpr = row_input_gpr;
            *output_gpr = row_output_gpr;
            uniform_vectors[output_component] = uniform_vector;
            found[output_component] = true;
        }
    }
}

static void scan_alu_clause(CPUState* cpu, u32 program, u32 program_slots,
                            u32 address, u32 count, bool found[4],
                            u8* input_gpr, u8* output_gpr,
                            u16 uniform_vectors[4]) {
    if (!cpu || address >= program_slots || count > program_slots - address)
        return;

    u32 slot = 0u;
    while (slot < count) {
        LatteAluInstruction group[LATTE_MAX_GROUP_INSTRUCTIONS];
        u32 group_count = 0u;
        u8 literal_mask = 0u;
        bool complete = false;
        while (slot < count && group_count < LATTE_MAX_GROUP_INSTRUCTIONS) {
            LatteAluInstruction* instruction = &group[group_count];
            if (!read_alu_instruction(cpu, program, address + slot,
                                      instruction)) {
                return;
            }
            literal_mask |= alu_literal_mask(instruction);
            group_count++;
            slot++;
            if (alu_last_in_group(instruction)) {
                complete = true;
                break;
            }
        }
        if (!complete)
            return;

        scan_alu_group(group, group_count, found, input_gpr, output_gpr,
                       uniform_vectors);

        u32 literal_slots = 0u;
        if ((literal_mask & 0x3u) != 0u)
            literal_slots++;
        if ((literal_mask & 0xCu) != 0u)
            literal_slots++;
        if (literal_slots > count - slot)
            return;
        slot += literal_slots;
    }
}

bool wiiu_latte_vertex_transform_decode(CPUState* cpu, u32 descriptor,
                                        WiiULatteVertexTransform* transform) {
    if (!cpu || !transform || descriptor == 0u ||
        !guest_range_valid(cpu, descriptor,
                           GX2_VERTEX_SHADER_DESCRIPTOR_SIZE)) {
        return false;
    }

    u32 program_size =
        mem_read32(cpu, descriptor + GX2_VERTEX_SHADER_PROGRAM_SIZE_OFFSET);
    u32 program = mem_read32(cpu, descriptor + GX2_VERTEX_SHADER_PROGRAM_OFFSET);
    if (program == 0u || program_size < 8u || program_size > 0x100000u ||
        !guest_range_valid(cpu, program, program_size)) {
        return false;
    }
    if (transform->valid && transform->descriptor == descriptor &&
        transform->program == program && transform->program_size == program_size) {
        return true;
    }

    WiiULatteVertexTransform decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.descriptor = descriptor;
    decoded.program = program;
    decoded.program_size = program_size;

    const u32 program_slots = program_size / 8u;
    bool rows_found[4] = {false, false, false, false};
    u8 input_gpr = 0xFFu;
    u8 output_gpr = 0xFFu;
    for (u32 cf = 0u; cf < program_slots; cf++) {
        u32 word0;
        u32 word1;
        if (!read_program_word_le(cpu, program + cf * 8u, &word0) ||
            !read_program_word_le(cpu, program + cf * 8u + 4u, &word1)) {
            break;
        }
        u32 instruction = (word1 >> 23u) & 0x7Fu;
        if (instruction >= LATTE_CF_ALU_THRESHOLD) {
            u32 address = word0 & 0x3FFFFFu;
            u32 count = ((word1 >> 18u) & 0x7Fu) + 1u;
            scan_alu_clause(cpu, program, program_slots, address, count,
                            rows_found, &input_gpr, &output_gpr,
                            decoded.uniform_vectors);
        } else if (((word1 >> 21u) & 1u) != 0u) {
            break;
        }
    }

    if (!rows_found[0] || !rows_found[1] || !rows_found[2] ||
        !rows_found[3] || input_gpr == 0xFFu || output_gpr == 0xFFu) {
        memset(transform, 0, sizeof(*transform));
        return false;
    }

    decoded.valid = true;
    decoded.input_gpr = input_gpr;
    *transform = decoded;
    return true;
}

static float float_from_bits(u32 bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

bool wiiu_latte_layout_position(CPUState* cpu, u32 descriptor,
    const u32* uniforms, const u8* valid, u32 count,
    const float uv[2], float clip[4]) {
    /* Lift only the verified position prefix, not arbitrary vertex shaders.
       R1.xy is the UV input. The first ALU clause computes:
       local=(u*c14.x+c14.z, -v*c14.y+c14.w, 0, 1),
       view=c3..c5 * local, clip=c7..c10 * (view,1).
       Subsequent clauses operate on color/UV outputs, not position R2.
       Words are Latte little-endian, unlike guest descriptor/uniform data. */
    static const u32 prefix[] = {
        0x0021C001,0x00000080, 0x001F1401,0x20000C90,
        0x001F00F8,0x40000C90, 0x001F00FD,0x60000C90,
        0x801F00F9,0x6FE00C90, 0x3F800000,0x00000000,
        0x0121C0FE,0x00000010, 0x80A1C4FE,0x40000080,
        0x01A1C8FE,0x20000010, 0x801FA101,0x60201810,
        0x40000000,0x00000000,
        0x00206000,0x0FE02810, 0x00A064FE,0x20002800,
        0x01206800,0x40002800, 0x81A06C00,0x60002800,
        0x00208000,0x00002800, 0x00A08400,0x2FE02810,
        0x01208800,0x40002800, 0x81A08C00,0x60002800,
        0x0020A000,0x00002800, 0x00A0A400,0x20002800,
        0x0120A800,0x4FE02810, 0x81A0AC00,0x60002800,
        0x0020E07F,0x00402810, 0x00A0E47F,0x20002800,
        0x0120E0FE,0x40002800, 0x81A0EC7F,0x60002800,
        0x0021007F,0x00002800, 0x00A1047F,0x20402810,
        0x0121087F,0x40002800, 0x81A10C7F,0x60002800,
        0x0021207F,0x00002800, 0x00A1247F,0x20002800,
        0x0121287F,0x40402810, 0x81A12C7F,0x60002800,
        0x0021407F,0x00002800, 0x00A1447F,0x20002800,
        0x0121487F,0x40002800, 0x81A14C7F,0x60402810
    };
    if (!cpu || !uniforms || !valid || !uv || !clip || count < 60u ||
        !guest_range_valid(cpu, descriptor, GX2_VERTEX_SHADER_DESCRIPTOR_SIZE))
        return false;
    u32 size = mem_read32(cpu, descriptor + GX2_VERTEX_SHADER_PROGRAM_SIZE_OFFSET);
    u32 program = mem_read32(cpu, descriptor + GX2_VERTEX_SHADER_PROGRAM_OFFSET);
    if (size < 16u || !guest_range_valid(cpu, program, size)) return false;
    u32 address, control;
    if (!read_program_word_le(cpu, program + 8u, &address) ||
        !read_program_word_le(cpu, program + 12u, &control) ||
        ((control >> 23u) & 0x7Fu) != 0x49u) return false;
    u64 offset = (u64)(address & 0x3FFFFFu) * 8u;
    if (offset + sizeof(prefix) > size) return false;
    u32 first;
    if (!read_program_word_le(cpu, program + (u32)offset, &first)) return false;
    /* The later title shader adds one uniform vector, keeps local Z in R4,
       and exports position from R6 instead of R2. Its math is identical. */
    bool later_layout = first == prefix[0] + (1u << 13u);
    for (u32 i = 0; i < sizeof(prefix)/sizeof(prefix[0]); ++i) {
        u32 word;
        u32 expected = prefix[i], instruction = i / 2u;
        if (later_layout) {
            if ((i & 1u) == 0u) {
                u32 source1 = (expected >> 13u) & 511u;
                if (source1 >= 256u) expected += 1u << 13u;
                if (instruction == 9u) expected += 1u;
                if (instruction == 13u || instruction == 17u || instruction == 21u)
                    expected += 4u;
            } else if (instruction == 2u || instruction == 23u ||
                       instruction == 28u || instruction == 33u || instruction == 38u) {
                expected += 4u << 21u;
            }
        }
        if (!read_program_word_le(cpu, program + (u32)offset + i*4u, &word) ||
            word != expected) return false;
    }
    u32 shift = later_layout ? 4u : 0u;
    if (count < 60u + shift) return false;
    uniforms += shift;
    valid += shift;
    for (u32 i = 12; i < 60; ++i) {
        if ((i < 24 || (i >= 28 && i < 44) || i >= 56) &&
            (!valid[i] || !isfinite(float_from_bits(uniforms[i])))) return false;
    }
    float local[4] = {
        uv[0]*float_from_bits(uniforms[56]) + float_from_bits(uniforms[58]),
        -uv[1]*float_from_bits(uniforms[57]) + float_from_bits(uniforms[59]),
        0.0f, 1.0f
    };
    float view[4] = {0,0,0,1};
    for (u32 row = 0; row < 3; ++row)
        for (u32 col = 0; col < 4; ++col)
            view[row] += local[col] * float_from_bits(uniforms[12+row*4+col]);
    for (u32 row = 0; row < 4; ++row) {
        clip[row] = 0;
        for (u32 col = 0; col < 4; ++col)
            clip[row] += view[col] * float_from_bits(uniforms[28+row*4+col]);
        if (!isfinite(clip[row])) return false;
    }
    return true;
}

bool wiiu_latte_vertex_transform_apply(
    const WiiULatteVertexTransform* transform, const u32* uniform_words,
    const u8* uniform_valid, u32 uniform_word_count, const float input[4],
    float output[4]) {
    if (!transform || !transform->valid || !uniform_words ||
        !uniform_valid || !input || !output) {
        return false;
    }

    for (u32 row = 0u; row < 4u; row++) {
        u32 offset = (u32)transform->uniform_vectors[row] * 4u;
        if (offset > uniform_word_count || uniform_word_count - offset < 4u ||
            !uniform_valid[offset] || !uniform_valid[offset + 1u] ||
            !uniform_valid[offset + 2u] || !uniform_valid[offset + 3u]) {
            return false;
        }
        output[row] = input[0] * float_from_bits(uniform_words[offset]) +
                      input[1] * float_from_bits(uniform_words[offset + 1u]) +
                      input[2] * float_from_bits(uniform_words[offset + 2u]) +
                      input[3] * float_from_bits(uniform_words[offset + 3u]);
        if (!isfinite(output[row]))
            return false;
    }
    return true;
}
