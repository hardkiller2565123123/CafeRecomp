#include "wiiu_latte_fetch.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "wiiu_memory.h"

enum {
    GX2_FORMAT_INT = 0x100u,
    GX2_FORMAT_SIGNED = 0x200u,
    GX2_FORMAT_FLOAT = 0x800u,

    LATTE_ENDIAN_NONE = 0u,
    LATTE_ENDIAN_U16 = 1u,
    LATTE_ENDIAN_U32 = 2u,
    LATTE_ENDIAN_DEFAULT = 3u,

    LATTE_DST_X = 0u,
    LATTE_DST_Y = 1u,
    LATTE_DST_Z = 2u,
    LATTE_DST_W = 3u,
    LATTE_DST_CONST_0 = 4u,
    LATTE_DST_CONST_1 = 5u,

    LATTE_CF_VTX = 0x02u,
    LATTE_CF_VTX_TC = 0x03u,
    LATTE_CF_RETURN = 0x14u,
    LATTE_VTX_INST_SEMANTIC = 1u,
};

static const u8 g_raw_format_to_fetch_format[] = {
    0x01u, 0x02u, 0x05u, 0x06u, 0x07u, 0x0Du, 0x0Eu,
    0x0Fu, 0x10u, 0x16u, 0x1Au, 0x19u, 0x1Du, 0x1Eu,
    0x1Fu, 0x20u, 0x2Fu, 0x30u, 0x22u, 0x23u,
};

static bool guest_range_valid(CPUState* cpu, u32 address, u32 size) {
    if (!cpu || address == 0u || size == 0u)
        return false;
    u64 end = (u64)address + size;
    if (end > 0x100000000ULL)
        return false;
    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    return memory && wiiu_memory_find(memory, address, size) != NULL;
}

static bool format_is_float(u32 format) {
    switch (format) {
    case 0x06u:
    case 0x0Eu:
    case 0x10u:
    case 0x16u:
    case 0x1Eu:
    case 0x20u:
    case 0x23u:
    case 0x2Eu:
    case 0x30u:
        return true;
    default:
        return false;
    }
}

static bool describe_fetch_format(u32 format, WiiULatteFetchFormatInfo* info) {
    if (!info)
        return false;

    switch (format) {
    case 0x01u:
        info->component_count = 1u;
        info->component_bits[0] = 8u;
        info->byte_size = 1u;
        break;
    case 0x02u:
        info->component_count = 2u;
        info->component_bits[0] = 4u;
        info->component_bits[1] = 4u;
        info->byte_size = 1u;
        break;
    case 0x05u:
    case 0x06u:
        info->component_count = 1u;
        info->component_bits[0] = 16u;
        info->byte_size = 2u;
        break;
    case 0x07u:
        info->component_count = 2u;
        info->component_bits[0] = 8u;
        info->component_bits[1] = 8u;
        info->byte_size = 2u;
        break;
    case 0x0Du:
    case 0x0Eu:
        info->component_count = 1u;
        info->component_bits[0] = 32u;
        info->byte_size = 4u;
        break;
    case 0x0Fu:
    case 0x10u:
        info->component_count = 2u;
        info->component_bits[0] = 16u;
        info->component_bits[1] = 16u;
        info->byte_size = 4u;
        break;
    case 0x16u:
        info->component_count = 3u;
        info->component_bits[0] = 11u;
        info->component_bits[1] = 11u;
        info->component_bits[2] = 10u;
        info->byte_size = 4u;
        break;
    case 0x19u:
        info->component_count = 4u;
        info->component_bits[0] = 10u;
        info->component_bits[1] = 10u;
        info->component_bits[2] = 10u;
        info->component_bits[3] = 2u;
        info->byte_size = 4u;
        break;
    case 0x1Au:
        info->component_count = 4u;
        info->component_bits[0] = 8u;
        info->component_bits[1] = 8u;
        info->component_bits[2] = 8u;
        info->component_bits[3] = 8u;
        info->byte_size = 4u;
        break;
    case 0x1Du:
    case 0x1Eu:
        info->component_count = 2u;
        info->component_bits[0] = 32u;
        info->component_bits[1] = 32u;
        info->byte_size = 8u;
        break;
    case 0x1Fu:
    case 0x20u:
        info->component_count = 4u;
        info->component_bits[0] = 16u;
        info->component_bits[1] = 16u;
        info->component_bits[2] = 16u;
        info->component_bits[3] = 16u;
        info->byte_size = 8u;
        break;
    case 0x22u:
    case 0x23u:
        info->component_count = 4u;
        info->component_bits[0] = 32u;
        info->component_bits[1] = 32u;
        info->component_bits[2] = 32u;
        info->component_bits[3] = 32u;
        info->byte_size = 16u;
        break;
    case 0x2Cu:
        info->component_count = 3u;
        info->component_bits[0] = 8u;
        info->component_bits[1] = 8u;
        info->component_bits[2] = 8u;
        info->byte_size = 3u;
        break;
    case 0x2Du:
    case 0x2Eu:
        info->component_count = 3u;
        info->component_bits[0] = 16u;
        info->component_bits[1] = 16u;
        info->component_bits[2] = 16u;
        info->byte_size = 6u;
        break;
    case 0x2Fu:
    case 0x30u:
        info->component_count = 3u;
        info->component_bits[0] = 32u;
        info->component_bits[1] = 32u;
        info->component_bits[2] = 32u;
        info->byte_size = 12u;
        break;
    default:
        return false;
    }
    return true;
}

bool wiiu_latte_fetch_format_info(u32 gx2_format,
                                  WiiULatteFetchFormatInfo* info) {
    if (!info)
        return false;
    memset(info, 0, sizeof(*info));

    u32 raw_format = gx2_format & 0x3Fu;
    u32 fetch_format = raw_format;
    if (raw_format < sizeof(g_raw_format_to_fetch_format))
        fetch_format = g_raw_format_to_fetch_format[raw_format];

    info->gx2_format = gx2_format;
    info->fetch_format = fetch_format;
    info->integer = (gx2_format & GX2_FORMAT_INT) != 0u;
    info->signed_values = (gx2_format & GX2_FORMAT_SIGNED) != 0u;
    info->floating = (gx2_format & GX2_FORMAT_FLOAT) != 0u ||
                     format_is_float(fetch_format);
    info->normalized = !info->integer && !info->floating;
    if (!describe_fetch_format(fetch_format, info)) {
        memset(info, 0, sizeof(*info));
        return false;
    }
    return true;
}

bool wiiu_latte_fetch_stream_format_info(
    const WiiULatteFetchStream* stream, WiiULatteFetchFormatInfo* info) {
    if (!stream || !info)
        return false;
    if (!stream->format_is_fetch)
        return wiiu_latte_fetch_format_info(stream->format, info);

    memset(info, 0, sizeof(*info));
    info->gx2_format = stream->format;
    info->fetch_format = stream->format & 0x3Fu;
    info->integer = stream->number_format == 1u;
    info->signed_values = stream->signed_values;
    info->floating = format_is_float(info->fetch_format);
    info->normalized = !info->integer && !info->floating &&
                       stream->number_format == 0u;
    if (!describe_fetch_format(info->fetch_format, info)) {
        memset(info, 0, sizeof(*info));
        return false;
    }
    return true;
}

static u32 default_endian_swap(u32 raw_format) {
    switch (raw_format & 0x3Fu) {
    case 0u:
    case 1u:
    case 4u:
    case 10u:
        return LATTE_ENDIAN_NONE;
    case 2u:
    case 3u:
    case 7u:
    case 8u:
    case 14u:
    case 15u:
        return LATTE_ENDIAN_U16;
    default:
        return LATTE_ENDIAN_U32;
    }
}

static bool write_program_word_le(CPUState* cpu, u32 address, u32 value) {
    if (!guest_range_valid(cpu, address, 4u))
        return false;
    mem_write8(cpu, address + 0u, (u8)value);
    mem_write8(cpu, address + 1u, (u8)(value >> 8u));
    mem_write8(cpu, address + 2u, (u8)(value >> 16u));
    mem_write8(cpu, address + 3u, (u8)(value >> 24u));
    return true;
}

static bool read_program_word_le(CPUState* cpu, u32 address, u32* value) {
    if (!value || !guest_range_valid(cpu, address, 4u))
        return false;
    *value = (u32)mem_read8(cpu, address + 0u) |
             ((u32)mem_read8(cpu, address + 1u) << 8u) |
             ((u32)mem_read8(cpu, address + 2u) << 16u) |
             ((u32)mem_read8(cpu, address + 3u) << 24u);
    return true;
}

static u32 fetch_program_cf_size(u32 stream_count) {
    u32 cf_instructions = ((stream_count + 15u) / 16u) + 1u;
    return (cf_instructions * 8u + 15u) & ~15u;
}

static bool stream_fetch_format(const WiiULatteFetchStream* stream,
                                u32* fetch_format, u32* number_format,
                                bool* signed_values) {
    if (!stream || !fetch_format || !number_format || !signed_values)
        return false;
    if (stream->format_is_fetch) {
        WiiULatteFetchFormatInfo info;
        memset(&info, 0, sizeof(info));
        if (!describe_fetch_format(stream->format & 0x3Fu, &info))
            return false;
        *fetch_format = stream->format & 0x3Fu;
        *number_format = stream->number_format & 0x3u;
        *signed_values = stream->signed_values;
        return true;
    }

    WiiULatteFetchFormatInfo info;
    if (!wiiu_latte_fetch_format_info(stream->format, &info))
        return false;
    *fetch_format = info.fetch_format;
    *number_format = (stream->format & GX2_FORMAT_FLOAT) != 0u
                         ? 2u
                         : ((stream->format & GX2_FORMAT_INT) != 0u ? 1u
                                                                     : 0u);
    *signed_values = (stream->format & GX2_FORMAT_SIGNED) != 0u;
    return true;
}

bool wiiu_latte_fetch_build_program(
    CPUState* cpu, u32 program, u32 capacity,
    const WiiULatteFetchStream* streams, u32 stream_count,
    u32 divisors[2], u32* divisor_count, u32* program_size) {
    if (!cpu || !streams || !divisors || !divisor_count || !program_size ||
        stream_count == 0u || stream_count > WIIU_LATTE_FETCH_MAX_STREAMS) {
        return false;
    }

    u32 cf_size = fetch_program_cf_size(stream_count);
    u64 total_size64 = (u64)cf_size + (u64)stream_count * 16u;
    if (total_size64 > 0xFFFFFFFFu || total_size64 > capacity ||
        !guest_range_valid(cpu, program, (u32)total_size64)) {
        return false;
    }
    u32 total_size = (u32)total_size64;
    for (u32 i = 0u; i < total_size; i++)
        mem_write8(cpu, program + i, 0u);

    for (u32 first = 0u, cf = 0u; first < stream_count;
         first += 16u, cf++) {
        u32 clause_count = stream_count - first;
        if (clause_count > 16u)
            clause_count = 16u;
        u32 word0 = (cf_size + first * 16u) >> 3u;
        u32 encoded_count = clause_count - 1u;
        u32 word1 = (LATTE_CF_VTX_TC << 23u) |
                    ((encoded_count & 0x7u) << 10u) |
                    ((encoded_count & 0x8u) << 16u);
        if (!write_program_word_le(cpu, program + cf * 8u, word0) ||
            !write_program_word_le(cpu, program + cf * 8u + 4u, word1)) {
            return false;
        }
    }
    u32 return_slot = (stream_count + 15u) / 16u;
    if (!write_program_word_le(cpu, program + return_slot * 8u, 0u) ||
        !write_program_word_le(cpu, program + return_slot * 8u + 4u,
                               (LATTE_CF_RETURN << 23u) | (1u << 31u))) {
        return false;
    }

    divisors[0] = 0u;
    divisors[1] = 0u;
    *divisor_count = 0u;
    for (u32 i = 0u; i < stream_count; i++) {
        const WiiULatteFetchStream* stream = &streams[i];
        u32 fetch_format;
        u32 number_format;
        bool signed_values;
        if (stream->location > 0xFFu || stream->buffer >= 16u ||
            stream->offset > 0xFFFFu || stream->index_type > 2u ||
            !stream_fetch_format(stream, &fetch_format, &number_format,
                                 &signed_values)) {
            return false;
        }

        u32 source_select = 0u;
        if (stream->index_type == 1u) {
            if (stream->divisor == 1u) {
                source_select = 3u;
            } else if (stream->divisor > 1u) {
                u32 divisor_index = 2u;
                for (u32 divisor = 0u; divisor < *divisor_count; divisor++) {
                    if (divisors[divisor] == stream->divisor) {
                        divisor_index = divisor;
                        break;
                    }
                }
                if (divisor_index == 2u) {
                    if (*divisor_count >= 2u)
                        return false;
                    divisor_index = *divisor_count;
                    divisors[divisor_index] = stream->divisor;
                    (*divisor_count)++;
                }
                source_select = divisor_index + 1u;
            } else {
                return false;
            }
        }

        u32 word0 = LATTE_VTX_INST_SEMANTIC |
                    ((stream->index_type & 0x3u) << 5u) |
                    ((stream->buffer + 0xA0u) << 8u) |
                    ((source_select & 0x3u) << 24u);
        u32 word1 = (stream->location & 0xFFu) |
                    (((stream->dest_sel >> 24u) & 0x7u) << 9u) |
                    (((stream->dest_sel >> 16u) & 0x7u) << 12u) |
                    (((stream->dest_sel >> 8u) & 0x7u) << 15u) |
                    ((stream->dest_sel & 0x7u) << 18u) |
                    ((fetch_format & 0x3Fu) << 22u) |
                    ((number_format & 0x3u) << 28u) |
                    (signed_values ? (1u << 30u) : 0u);
        u32 endian = stream->endian_swap & 0x3u;
        if (endian == LATTE_ENDIAN_DEFAULT)
            endian = default_endian_swap(stream->format);
        u32 word2 = (stream->offset & 0xFFFFu) | ((endian & 0x3u) << 16u);
        u32 instruction = program + cf_size + i * 16u;
        if (!write_program_word_le(cpu, instruction + 0u, word0) ||
            !write_program_word_le(cpu, instruction + 4u, word1) ||
            !write_program_word_le(cpu, instruction + 8u, word2) ||
            !write_program_word_le(cpu, instruction + 12u, 0u)) {
            return false;
        }
    }

    *program_size = total_size;
    return true;
}

bool wiiu_latte_fetch_parse_program(
    CPUState* cpu, u32 program, u32 program_size, const u32 divisors[2],
    u32 divisor_count, WiiULatteFetchStream* streams, u32 stream_capacity,
    u32* stream_count) {
    if (stream_count)
        *stream_count = 0u;
    if (!cpu || !streams || !stream_count || stream_capacity == 0u ||
        program_size < 8u || !guest_range_valid(cpu, program, program_size)) {
        return false;
    }
    if (stream_capacity > WIIU_LATTE_FETCH_MAX_STREAMS)
        stream_capacity = WIIU_LATTE_FETCH_MAX_STREAMS;
    if (divisor_count > 2u)
        divisor_count = 2u;

    u32 parsed = 0u;
    u32 slots = program_size / 8u;
    for (u32 cf = 0u; cf < slots; cf++) {
        u32 word0;
        u32 word1;
        if (!read_program_word_le(cpu, program + cf * 8u, &word0) ||
            !read_program_word_le(cpu, program + cf * 8u + 4u, &word1)) {
            break;
        }
        u32 opcode = (word1 >> 23u) & 0x7Fu;
        if (opcode == LATTE_CF_RETURN)
            break;
        if (opcode != LATTE_CF_VTX && opcode != LATTE_CF_VTX_TC)
            continue;

        u32 clause = word0 << 3u;
        u32 clause_count = ((word1 >> 10u) & 0x7u) |
                           ((word1 >> 16u) & 0x8u);
        clause_count++;
        u64 clause_end = (u64)clause + (u64)clause_count * 16u;
        if (clause >= program_size || clause_end > program_size)
            return false;
        for (u32 i = 0u; i < clause_count; i++) {
            u32 instruction = program + clause + i * 16u;
            u32 vtx0;
            u32 vtx1;
            u32 vtx2;
            if (!read_program_word_le(cpu, instruction + 0u, &vtx0) ||
                !read_program_word_le(cpu, instruction + 4u, &vtx1) ||
                !read_program_word_le(cpu, instruction + 8u, &vtx2)) {
                return false;
            }
            if ((vtx0 & 0x1Fu) != LATTE_VTX_INST_SEMANTIC)
                continue;
            u32 buffer_id = (vtx0 >> 8u) & 0xFFu;
            u32 fetch_type = (vtx0 >> 5u) & 0x3u;
            if (buffer_id < 0xA0u || buffer_id >= 0xB0u ||
                fetch_type > 2u || parsed >= stream_capacity) {
                continue;
            }

            WiiULatteFetchStream* stream = &streams[parsed];
            memset(stream, 0, sizeof(*stream));
            stream->location = vtx1 & 0xFFu;
            stream->buffer = buffer_id - 0xA0u;
            stream->offset = vtx2 & 0xFFFFu;
            stream->format = (vtx1 >> 22u) & 0x3Fu;
            stream->index_type = fetch_type;
            stream->dest_sel =
                (((vtx1 >> 9u) & 0x7u) << 24u) |
                (((vtx1 >> 12u) & 0x7u) << 16u) |
                (((vtx1 >> 15u) & 0x7u) << 8u) |
                ((vtx1 >> 18u) & 0x7u);
            stream->endian_swap = (vtx2 >> 16u) & 0x3u;
            stream->format_is_fetch = true;
            stream->number_format = (u8)((vtx1 >> 28u) & 0x3u);
            stream->signed_values = ((vtx1 >> 30u) & 1u) != 0u;

            if (fetch_type == 1u) {
                u32 source_select = (vtx0 >> 24u) & 0x3u;
                if (source_select == 3u) {
                    stream->divisor = 1u;
                } else if (source_select == 1u || source_select == 2u) {
                    u32 divisor_index = source_select - 1u;
                    stream->divisor = divisor_index < divisor_count && divisors
                                          ? divisors[divisor_index]
                                          : 1u;
                    if (stream->divisor == 0u)
                        stream->divisor = 1u;
                } else {
                    continue;
                }
            }
            parsed++;
        }
    }

    *stream_count = parsed;
    return parsed != 0u;
}

static u16 read_fetch_u16(CPUState* cpu, u32 address, u32 endian_swap) {
    if (endian_swap == LATTE_ENDIAN_NONE) {
        return (u16)mem_read8(cpu, address) |
               ((u16)mem_read8(cpu, address + 1u) << 8u);
    }
    return mem_read16(cpu, address);
}

static u32 read_fetch_u32(CPUState* cpu, u32 address, u32 endian_swap) {
    if (endian_swap == LATTE_ENDIAN_NONE) {
        return (u32)mem_read8(cpu, address) |
               ((u32)mem_read8(cpu, address + 1u) << 8u) |
               ((u32)mem_read8(cpu, address + 2u) << 16u) |
               ((u32)mem_read8(cpu, address + 3u) << 24u);
    }
    return mem_read32(cpu, address);
}

static float float_from_bits(u32 bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static float half_to_float(u16 bits) {
    u32 exponent = (bits >> 10u) & 0x1Fu;
    u32 mantissa = bits & 0x3FFu;
    float value;
    if (exponent == 0u) {
        value = ldexpf((float)mantissa, -24);
    } else if (exponent == 0x1Fu) {
        value = mantissa == 0u ? INFINITY : NAN;
    } else {
        value = ldexpf(1.0f + (float)mantissa / 1024.0f,
                       (int)exponent - 15);
    }
    return (bits & 0x8000u) != 0u ? -value : value;
}

static float ufloat_to_float(u32 bits, u32 mantissa_bits) {
    u32 exponent = bits >> mantissa_bits;
    u32 mantissa_mask = (1u << mantissa_bits) - 1u;
    u32 mantissa = bits & mantissa_mask;
    if (exponent == 0u)
        return ldexpf((float)mantissa, -14 - (int)mantissa_bits);
    if (exponent == 0x1Fu)
        return mantissa == 0u ? INFINITY : NAN;
    return ldexpf(1.0f + (float)mantissa / (float)(1u << mantissa_bits),
                  (int)exponent - 15);
}

static s32 fetch_sign_extend(u32 value, u32 bits) {
    if (bits >= 32u)
        return (s32)value;
    u32 mask = (1u << bits) - 1u;
    value &= mask;
    if ((value & (1u << (bits - 1u))) == 0u)
        return (s32)value;
    return -(s32)((~value + 1u) & mask);
}

static float integer_to_float(u32 value, u32 bits,
                              const WiiULatteFetchFormatInfo* info) {
    if (!info)
        return 0.0f;
    if (info->signed_values) {
        s32 signed_value = fetch_sign_extend(value, bits);
        if (!info->normalized)
            return (float)signed_value;
        if (bits >= 32u) {
            if (signed_value == INT32_MIN)
                return -1.0f;
            return (float)signed_value / 2147483647.0f;
        }
        s32 maximum = (s32)((1u << (bits - 1u)) - 1u);
        if (signed_value <= -maximum)
            return -1.0f;
        return (float)signed_value / (float)maximum;
    }

    if (!info->normalized)
        return (float)value;
    if (bits >= 32u)
        return (float)((double)value / 4294967295.0);
    u32 maximum = (1u << bits) - 1u;
    return (float)value / (float)maximum;
}

static bool decode_components(CPUState* cpu, u32 address,
                              const WiiULatteFetchFormatInfo* info,
                              u32 endian_swap, float output[4]) {
    if (!cpu || !info || !output)
        return false;
    output[0] = 0.0f;
    output[1] = 0.0f;
    output[2] = 0.0f;
    output[3] = 1.0f;

    switch (info->fetch_format) {
    case 0x01u:
        output[0] = integer_to_float(mem_read8(cpu, address), 8u, info);
        break;
    case 0x02u: {
        u8 packed = mem_read8(cpu, address);
        output[0] = integer_to_float(packed & 0xFu, 4u, info);
        output[1] = integer_to_float(packed >> 4u, 4u, info);
        break;
    }
    case 0x05u:
        output[0] =
            integer_to_float(read_fetch_u16(cpu, address, endian_swap), 16u,
                             info);
        break;
    case 0x06u:
        output[0] = half_to_float(read_fetch_u16(cpu, address, endian_swap));
        break;
    case 0x07u:
        output[0] = integer_to_float(mem_read8(cpu, address), 8u, info);
        output[1] = integer_to_float(mem_read8(cpu, address + 1u), 8u, info);
        break;
    case 0x0Du:
        output[0] =
            integer_to_float(read_fetch_u32(cpu, address, endian_swap), 32u,
                             info);
        break;
    case 0x0Eu:
        output[0] = float_from_bits(read_fetch_u32(cpu, address, endian_swap));
        break;
    case 0x0Fu:
        output[0] =
            integer_to_float(read_fetch_u16(cpu, address, endian_swap), 16u,
                             info);
        output[1] = integer_to_float(
            read_fetch_u16(cpu, address + 2u, endian_swap), 16u, info);
        break;
    case 0x10u:
        output[0] = half_to_float(read_fetch_u16(cpu, address, endian_swap));
        output[1] =
            half_to_float(read_fetch_u16(cpu, address + 2u, endian_swap));
        break;
    case 0x16u: {
        u32 packed = read_fetch_u32(cpu, address, endian_swap);
        output[0] = ufloat_to_float(packed & 0x7FFu, 6u);
        output[1] = ufloat_to_float((packed >> 11u) & 0x7FFu, 6u);
        output[2] = ufloat_to_float((packed >> 22u) & 0x3FFu, 5u);
        break;
    }
    case 0x19u: {
        u32 packed = read_fetch_u32(cpu, address, endian_swap);
        output[0] = integer_to_float(packed & 0x3FFu, 10u, info);
        output[1] = integer_to_float((packed >> 10u) & 0x3FFu, 10u, info);
        output[2] = integer_to_float((packed >> 20u) & 0x3FFu, 10u, info);
        output[3] = integer_to_float((packed >> 30u) & 0x3u, 2u, info);
        break;
    }
    case 0x1Au:
        for (u32 component = 0u; component < 4u; component++) {
            output[component] = integer_to_float(
                mem_read8(cpu, address + component), 8u, info);
        }
        break;
    case 0x1Du:
        for (u32 component = 0u; component < 2u; component++) {
            output[component] = integer_to_float(
                read_fetch_u32(cpu, address + component * 4u, endian_swap),
                32u, info);
        }
        break;
    case 0x1Eu:
        for (u32 component = 0u; component < 2u; component++) {
            output[component] = float_from_bits(
                read_fetch_u32(cpu, address + component * 4u, endian_swap));
        }
        break;
    case 0x1Fu:
        for (u32 component = 0u; component < 4u; component++) {
            output[component] = integer_to_float(
                read_fetch_u16(cpu, address + component * 2u, endian_swap),
                16u, info);
        }
        break;
    case 0x20u:
        for (u32 component = 0u; component < 4u; component++) {
            output[component] = half_to_float(
                read_fetch_u16(cpu, address + component * 2u, endian_swap));
        }
        break;
    case 0x22u:
        for (u32 component = 0u; component < 4u; component++) {
            output[component] = integer_to_float(
                read_fetch_u32(cpu, address + component * 4u, endian_swap),
                32u, info);
        }
        break;
    case 0x23u:
        for (u32 component = 0u; component < 4u; component++) {
            output[component] = float_from_bits(
                read_fetch_u32(cpu, address + component * 4u, endian_swap));
        }
        break;
    case 0x2Cu:
        for (u32 component = 0u; component < 3u; component++) {
            output[component] = integer_to_float(
                mem_read8(cpu, address + component), 8u, info);
        }
        break;
    case 0x2Du:
        for (u32 component = 0u; component < 3u; component++) {
            output[component] = integer_to_float(
                read_fetch_u16(cpu, address + component * 2u, endian_swap),
                16u, info);
        }
        break;
    case 0x2Eu:
        for (u32 component = 0u; component < 3u; component++) {
            output[component] = half_to_float(
                read_fetch_u16(cpu, address + component * 2u, endian_swap));
        }
        break;
    case 0x2Fu:
        for (u32 component = 0u; component < 3u; component++) {
            output[component] = integer_to_float(
                read_fetch_u32(cpu, address + component * 4u, endian_swap),
                32u, info);
        }
        break;
    case 0x30u:
        for (u32 component = 0u; component < 3u; component++) {
            output[component] = float_from_bits(
                read_fetch_u32(cpu, address + component * 4u, endian_swap));
        }
        break;
    default:
        return false;
    }

    for (u32 component = 0u; component < info->component_count; component++) {
        if (!isfinite(output[component]))
            return false;
    }
    return true;
}

static void apply_destination_selection(const WiiULatteFetchStream* stream,
                                        const float source[4],
                                        float output[4]) {
    for (u32 component = 0u; component < 4u; component++) {
        u32 selector = (stream->dest_sel >> (24u - component * 8u)) & 0x7u;
        switch (selector) {
        case LATTE_DST_X:
        case LATTE_DST_Y:
        case LATTE_DST_Z:
        case LATTE_DST_W:
            output[component] = source[selector];
            break;
        case LATTE_DST_CONST_1:
            output[component] = 1.0f;
            break;
        case LATTE_DST_CONST_0:
        default:
            output[component] = 0.0f;
            break;
        }
    }
}

bool wiiu_latte_fetch_vertex(CPUState* cpu,
                             const WiiULatteFetchStream* streams,
                             u32 stream_count,
                             const WiiULatteFetchBuffer* buffers,
                             u32 buffer_count, u32 vertex_index,
                             u32 instance_index,
                             WiiULatteFetchedVertex* result) {
    if (!cpu || !streams || !buffers || !result)
        return false;
    if (stream_count > WIIU_LATTE_FETCH_MAX_STREAMS)
        stream_count = WIIU_LATTE_FETCH_MAX_STREAMS;

    memset(result, 0, sizeof(*result));
    for (u32 stream_index = 0u; stream_index < stream_count; stream_index++) {
        const WiiULatteFetchStream* stream = &streams[stream_index];
        if (stream->location >= WIIU_LATTE_FETCH_MAX_GPRS ||
            stream->buffer >= buffer_count) {
            result->failed_count++;
            continue;
        }

        WiiULatteFetchFormatInfo format;
        if (!wiiu_latte_fetch_stream_format_info(stream, &format)) {
            result->failed_count++;
            continue;
        }

        const WiiULatteFetchBuffer* buffer = &buffers[stream->buffer];
        if (!buffer->valid || buffer->stride == 0u || buffer->data == 0u) {
            result->failed_count++;
            continue;
        }

        u32 source_index = vertex_index;
        if (stream->index_type == 1u) {
            u32 divisor = stream->divisor == 0u ? 1u : stream->divisor;
            source_index = instance_index / divisor;
        } else if (stream->index_type == 2u) {
            source_index = 0u;
        } else if (stream->index_type != 0u) {
            result->failed_count++;
            continue;
        }

        u64 byte_offset = (u64)source_index * buffer->stride + stream->offset;
        if (byte_offset > buffer->size ||
            format.byte_size > buffer->size - byte_offset) {
            result->failed_count++;
            continue;
        }
        u64 address64 = (u64)buffer->data + byte_offset;
        if (address64 > 0xFFFFFFFFu ||
            !guest_range_valid(cpu, (u32)address64, format.byte_size)) {
            result->failed_count++;
            continue;
        }

        u32 endian_swap = stream->endian_swap & 0x3u;
        if (endian_swap == LATTE_ENDIAN_DEFAULT)
            endian_swap = default_endian_swap(stream->format);

        float source[4];
        if (!decode_components(cpu, (u32)address64, &format, endian_swap,
                               source)) {
            result->failed_count++;
            continue;
        }
        apply_destination_selection(stream, source, result->gpr[stream->location]);
        result->valid[stream->location] = 1u;
        result->fetched_count++;
    }
    return result->fetched_count != 0u;
}

bool wiiu_latte_fetch_get_gpr(const WiiULatteFetchedVertex* result,
                              u32 location, float output[4]) {
    if (!result || !output || location >= WIIU_LATTE_FETCH_MAX_GPRS ||
        result->valid[location] == 0u) {
        return false;
    }
    memcpy(output, result->gpr[location], sizeof(result->gpr[location]));
    return true;
}
