#ifndef SM3DW_WIIU_MEMORY_H
#define SM3DW_WIIU_MEMORY_H

#include "common/types.h"
#include "cpu/cpu.h"
#include "rpx_runtime.h"

#define WIIU_MAX_SEGMENTS 160
/* U-King's final RPX allocation ends below 0x10600000. Keep the rest of
   MEM2 contiguous for the title's own heap hierarchy. Host-owned scratch,
   OS objects and stacks must not split this arena: sead allocations need
   to remain inside their owning heap (including their block metadata). */
#define WIIU_GUEST_HEAP_BASE  0x10600000u
#define WIIU_GUEST_HEAP_SIZE  0x3FA00000u
#define WIIU_GUEST_AUX_HEAP_BASE  0x60000000u
#define WIIU_GUEST_AUX_HEAP_SIZE  0x10000000u
#define WIIU_GUEST_OVERLAY_BASE   0xA0000000u
#define WIIU_GUEST_OVERLAY_SIZE   0x1C000000u
#define WIIU_GUEST_GPU_HEAP_BASE  0x50000000u
#define WIIU_GUEST_GPU_HEAP_SIZE  0x10000000u
#define WIIU_GUEST_FG_HEAP_BASE   WIIU_GUEST_GPU_HEAP_BASE
#define WIIU_GUEST_FG_HEAP_SIZE   0x08000000u
#define WIIU_GUEST_MEM1_HEAP_BASE 0x58000000u
#define WIIU_GUEST_MEM1_HEAP_SIZE 0x08000000u
#define WIIU_GUEST_OS_BASE    0x70000000u
#define WIIU_GUEST_OS_SIZE    0x01000000u
#define WIIU_GUEST_THREAD     (WIIU_GUEST_OS_BASE + 0x100u)
#define WIIU_GUEST_STACK_BASE 0x71000000u
#define WIIU_GUEST_STACK_SIZE 0x00200000u
#define WIIU_GUEST_STACK_TOP  (WIIU_GUEST_STACK_BASE + WIIU_GUEST_STACK_SIZE)

typedef struct {
    char name[64];
    u32 base;
    u32 size;
    bool writable;
    u8* data;
} WiiUMemorySegment;

typedef struct {
    WiiUMemorySegment segments[WIIU_MAX_SEGMENTS];
    u32 segment_count;
    WiiUMemorySegment** page_map;
    u8** direct_page_map;
    WiiUMemorySegment* last_segment;
    u32 unmapped_read_count;
    u32 unmapped_write_count;
    u32 readonly_write_count;
    CPUState* reservations[256];
    u32 reservation_count;
} WiiUMemory;

void wiiu_memory_init(WiiUMemory* memory);
void wiiu_memory_free(WiiUMemory* memory);
bool wiiu_memory_add_segment(WiiUMemory* memory, const char* name, u32 base,
                             u32 size, const u8* initial_data,
                             bool writable);
bool wiiu_memory_load_rpx(WiiUMemory* memory, const RPXFile* rpx);
bool wiiu_memory_install_default_regions(WiiUMemory* memory);
void wiiu_memory_bind_cpu(WiiUMemory* memory, CPUState* cpu);
/* Retain a CPU's live lwarx reservation across a cooperative dispatch. */
void wiiu_memory_track_reservation(CPUState* cpu);
void wiiu_memory_dump_map(const WiiUMemory* memory);
u32 wiiu_memory_canonical_address(u32 address);
WiiUMemorySegment* wiiu_memory_find(WiiUMemory* memory, u32 address,
                                    u32 size);

#endif
