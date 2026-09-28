#include "wiiu_memory.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

int main(int argc, char** argv) {
    WiiUMemory memory;
    wiiu_memory_init(&memory);
    if (!wiiu_memory_install_default_regions(&memory))
        return 1;
    int result = 0;
    CPUState cpu = {0};
    wiiu_memory_bind_cpu(&memory, &cpu);
    /* Exercise the external-read fast page table against the checked path:
       all load widths, unaligned loads, page crossings, and Cafe aliases. */
    const u32 read_base = WIIU_GUEST_HEAP_BASE + 0xFFF0u;
    for (u32 i = 0; i < 32u; ++i)
        cpu.external_write(&cpu, read_base + i, 0x80u + i, 1u);
    for (u32 size = 1u; size <= 8u; size *= 2u) {
        for (u32 offset = 0u; offset < 24u; ++offset) {
            u64 expected = 0;
            for (u32 byte = 0; byte < size; ++byte)
                expected = (expected << 8u) | (0x80u + offset + byte);
            u32 address = read_base + offset;
            if (cpu.external_read(&cpu, address, (u8)size) != expected ||
                cpu.external_read(&cpu, address | 0x80000000u, (u8)size) != expected) {
                fprintf(stderr, "Direct guest load mismatch at %08X width=%u\n", address, size);
                result = 1;
            }
        }
    }
    /* A small RPX-style segment must not expose its surrounding page. */
    const u32 partial = 0x06000020u;
    if (!wiiu_memory_add_segment(&memory, "partial", partial, 32u, NULL, true))
        return 1;
    cpu.external_write(&cpu, partial + 24u, 0x0123456789ABCDEFull, 8u);
    if (cpu.external_read(&cpu, partial + 24u, 8u) != 0x0123456789ABCDEFull ||
        cpu.external_read(&cpu, partial + 25u, 8u) != 0u ||
        cpu.external_read(&cpu, partial - 1u, 1u) != 0u ||
        memory.unmapped_read_count != 2u) {
        fprintf(stderr, "Partial-page guest load escaped its segment\n");
        result = 1;
    }
    const u32 texture_record = 0x20408800u;
    const u32 overlay_read_buffer = 0xA0408800u;
    cpu.external_write(&cpu, texture_record, 0x47583242u, 4u);
    cpu.external_write(&cpu, overlay_read_buffer, 0x9F1047E0u, 4u);
    if (wiiu_memory_canonical_address(overlay_read_buffer) != overlay_read_buffer ||
        cpu.external_read(&cpu, texture_record, 4u) != 0x47583242u ||
        cpu.external_read(&cpu, overlay_read_buffer, 4u) != 0x9F1047E0u ||
        wiiu_memory_find(&memory, texture_record, 4u) ==
            wiiu_memory_find(&memory, overlay_read_buffer, 4u)) {
        fprintf(stderr, "Overlay read buffer aliases live MEM2 texture data\n");
        result = 1;
    }
    if (WIIU_GUEST_HEAP_BASE + WIIU_GUEST_HEAP_SIZE != 0x50000000u ||
        WIIU_GUEST_HEAP_SIZE < 0x30000000u) {
        fprintf(stderr, "Title MEM2 heap is truncated\n");
        result = 1;
    }
    for (u32 i = 0; i < memory.segment_count; ++i) {
        WiiUMemorySegment* a = &memory.segments[i];
        u64 end = (u64)a->base + a->size;
        if (end > 0x100000000ull ||
            wiiu_memory_find(&memory, a->base, 1) != a ||
            wiiu_memory_find(&memory, (u32)(end - 1), 1) != a) {
            fprintf(stderr, "Invalid region boundary: %s\n", a->name);
            result = 1;
        }
        for (u32 j = i + 1; j < memory.segment_count; ++j) {
            WiiUMemorySegment* b = &memory.segments[j];
            if ((u64)a->base < (u64)b->base + b->size &&
                (u64)b->base < end) {
                fprintf(stderr, "Overlapping regions: %s / %s\n", a->name, b->name);
                result = 1;
            }
        }
    }
    CPUState peer = cpu;
    const u32 atomic_word = WIIU_GUEST_HEAP_BASE + 0x100u;
    cpu.reserve_addr = atomic_word;
    peer.reserve_addr = atomic_word + 8;
    cpu.reserve_valid = peer.reserve_valid = true;
    wiiu_memory_track_reservation(&cpu);
    wiiu_memory_track_reservation(&peer);
    cpu.external_write(&cpu, atomic_word + 32, 0, 4);
    if (!cpu.reserve_valid || !peer.reserve_valid) {
        fprintf(stderr, "Unrelated cache-line store invalidated reservation\n");
        result = 1;
    }
    cpu.external_write(&cpu, atomic_word + 1, 0, 1);
    if (cpu.reserve_valid || peer.reserve_valid || memory.reservation_count) {
        fprintf(stderr, "Shared cache-line store retained an atomic reservation\n");
        result = 1;
    }
    peer.reserve_addr = atomic_word + 32;
    peer.reserve_valid = true;
    wiiu_memory_track_reservation(&peer);
    cpu.external_write(&cpu, atomic_word + 28, 0, 8);
    if (peer.reserve_valid || memory.reservation_count) {
        fprintf(stderr, "Cross-line store retained an atomic reservation\n");
        result = 1;
    }
    if (argc > 1 && strcmp(argv[1], "--benchmark") == 0) {
        u64 checksums[2] = {0};
        u8** pages = memory.direct_page_map;
        const u32 addresses[] = {WIIU_GUEST_HEAP_BASE, WIIU_GUEST_STACK_BASE,
            WIIU_GUEST_OVERLAY_BASE, WIIU_GUEST_GPU_HEAP_BASE};
        for (u32 mode = 0; mode < 2; ++mode) {
            memory.direct_page_map = mode ? pages : NULL;
            clock_t start = clock();
            for (u32 i = 0; i < 10000000u; ++i)
                checksums[mode] += cpu.external_read(&cpu,
                    addresses[i & 3u] + (i & 0xFFFCu), 4u);
            fprintf(stderr, "Guest reads %s: %.1f ms\n", mode ? "direct" : "checked",
                    (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC);
        }
        memory.direct_page_map = pages;
        if (checksums[0] != checksums[1]) result = 1;
    }
    wiiu_memory_free(&memory);
    return result;
}
