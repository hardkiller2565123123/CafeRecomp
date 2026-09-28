#ifndef SM3DW_WIIU_LATTE_FETCH_H
#define SM3DW_WIIU_LATTE_FETCH_H

#include <stdbool.h>

#include "cpu/cpu.h"

enum {
    WIIU_LATTE_FETCH_MAX_STREAMS = 16u,
    WIIU_LATTE_FETCH_MAX_GPRS = 128u,
};

typedef struct {
    bool valid;
    u32 size;
    u32 stride;
    u32 data;
} WiiULatteFetchBuffer;

typedef struct {
    u32 location;
    u32 buffer;
    u32 offset;
    u32 format;
    u32 index_type;
    u32 divisor;
    u32 dest_sel;
    u32 endian_swap;
    /* Set for a format decoded from native Latte fetch bytecode. */
    bool format_is_fetch;
    u8 number_format;
    bool signed_values;
} WiiULatteFetchStream;

typedef struct {
    u32 gx2_format;
    u32 fetch_format;
    u32 byte_size;
    u8 component_count;
    u8 component_bits[4];
    bool normalized;
    bool integer;
    bool signed_values;
    bool floating;
} WiiULatteFetchFormatInfo;

typedef struct {
    float gpr[WIIU_LATTE_FETCH_MAX_GPRS][4];
    u8 valid[WIIU_LATTE_FETCH_MAX_GPRS];
    u32 fetched_count;
    u32 failed_count;
} WiiULatteFetchedVertex;

bool wiiu_latte_fetch_format_info(u32 gx2_format,
                                  WiiULatteFetchFormatInfo* info);
bool wiiu_latte_fetch_stream_format_info(
    const WiiULatteFetchStream* stream, WiiULatteFetchFormatInfo* info);
bool wiiu_latte_fetch_build_program(
    CPUState* cpu, u32 program, u32 capacity,
    const WiiULatteFetchStream* streams, u32 stream_count,
    u32 divisors[2], u32* divisor_count, u32* program_size);
bool wiiu_latte_fetch_parse_program(
    CPUState* cpu, u32 program, u32 program_size, const u32 divisors[2],
    u32 divisor_count, WiiULatteFetchStream* streams, u32 stream_capacity,
    u32* stream_count);
bool wiiu_latte_fetch_vertex(CPUState* cpu,
                             const WiiULatteFetchStream* streams,
                             u32 stream_count,
                             const WiiULatteFetchBuffer* buffers,
                             u32 buffer_count, u32 vertex_index,
                             u32 instance_index,
                             WiiULatteFetchedVertex* result);
bool wiiu_latte_fetch_get_gpr(const WiiULatteFetchedVertex* result,
                              u32 location, float output[4]);

#endif
