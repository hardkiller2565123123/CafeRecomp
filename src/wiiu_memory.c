#include "wiiu_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SHF_WRITE = 0x00000001,
    WIIU_MEMORY_PAGE_SHIFT = 16u,
    WIIU_MEMORY_PAGE_SIZE = 1u << WIIU_MEMORY_PAGE_SHIFT,
    WIIU_MEMORY_PAGE_COUNT = 1u << (32u - WIIU_MEMORY_PAGE_SHIFT),
};

static bool range_contains(u32 base, u32 size, u32 address, u32 access_size) {
    if (size == 0 || access_size == 0)
        return false;
    if (address < base)
        return false;

    u32 offset = address - base;
    return offset < size && access_size <= size - offset;
}

void wiiu_memory_init(WiiUMemory* memory) {
    memset(memory, 0, sizeof(*memory));
    memory->page_map = (WiiUMemorySegment**)calloc(
        WIIU_MEMORY_PAGE_COUNT, sizeof(*memory->page_map));
    memory->direct_page_map = (u8**)calloc(
        WIIU_MEMORY_PAGE_COUNT, sizeof(*memory->direct_page_map));
    if (!memory->page_map) {
        fprintf(stderr,
                "warn: Wii U fast memory map unavailable; using segment scan\n");
    }
    if (!memory->direct_page_map) {
        fprintf(stderr,
                "warn: Wii U direct memory map unavailable; using callbacks\n");
    }
}

void wiiu_memory_free(WiiUMemory* memory) {
    for (u32 i = 0; i < memory->segment_count; i++) {
        free(memory->segments[i].data);
        memory->segments[i].data = NULL;
    }
    free(memory->page_map);
    memory->page_map = NULL;
    free(memory->direct_page_map);
    memory->direct_page_map = NULL;
    memset(memory, 0, sizeof(*memory));
}

static void map_segment_pages(WiiUMemory* memory,
                              WiiUMemorySegment* segment) {
    if (!memory || !segment || segment->size == 0u)
        return;

    if (memory->page_map) {
        u64 last_address = (u64)segment->base + segment->size - 1u;
        u32 first_page = segment->base >> WIIU_MEMORY_PAGE_SHIFT;
        u32 last_page = (u32)(last_address >> WIIU_MEMORY_PAGE_SHIFT);
        for (u32 page = first_page;; page++) {
            memory->page_map[page] = segment;
            if (page == last_page)
                break;
        }
    }

    if (memory->direct_page_map) {
        const u64 page_size = WIIU_MEMORY_PAGE_SIZE;
        const u64 segment_base = segment->base;
        const u64 first_full_page =
            (segment_base + page_size - 1u) & ~(page_size - 1u);
        const u64 full_page_end =
            (segment_base + segment->size) & ~(page_size - 1u);
        for (u64 address = first_full_page; address < full_page_end;
             address += page_size) {
            u32 page = (u32)(address >> WIIU_MEMORY_PAGE_SHIFT);
            memory->direct_page_map[page] =
                segment->data + (u32)(address - segment_base);
        }
    }
}

bool wiiu_memory_add_segment(WiiUMemory* memory, const char* name, u32 base,
                             u32 size, const u8* initial_data,
                             bool writable) {
    if (size == 0)
        return true;
    if (memory->segment_count >= WIIU_MAX_SEGMENTS) {
        fprintf(stderr, "error: too many Wii U memory segments\n");
        return false;
    }

    u8* data = (u8*)calloc(1, size);
    if (!data) {
        fprintf(stderr, "error: failed to allocate Wii U segment '%s'\n",
                name ? name : "<unnamed>");
        return false;
    }

    if (initial_data)
        memcpy(data, initial_data, size);

    WiiUMemorySegment* segment = &memory->segments[memory->segment_count++];
    snprintf(segment->name, sizeof(segment->name), "%s",
             name ? name : "<unnamed>");
    segment->base = base;
    segment->size = size;
    segment->writable = writable;
    segment->data = data;
    map_segment_pages(memory, segment);
    return true;
}

bool wiiu_memory_load_rpx(WiiUMemory* memory, const RPXFile* rpx) {
    for (u32 i = 0; i < rpx->load_section_count; i++) {
        const RPXLoadSection* load = &rpx->load_sections[i];
        if (load->address == 0 || load->size == 0)
            continue;

        // Wii U data-import slots are loader-owned writable indirections even
        // when their RPX section is otherwise marked read-only.
        const bool writable = (load->flags & SHF_WRITE) != 0 || load->nobits ||
                              strncmp(load->name, ".dimport_", 9) == 0;
        if (!wiiu_memory_add_segment(memory, load->name, load->address,
                                     load->size, load->data, writable)) {
            return false;
        }
    }

    /* U-King v208's Cafe loader veneers occupy the page immediately after
       .text. Function entries are dispatched by the host-call bridge, while
       data-import indirections in the same arena must remain readable and
       writable (notably MEMAllocFromDefaultHeap* and the GHS runtime slots). */
    if (!wiiu_memory_add_segment(memory, "loader_import_arena", 0x04348000u,
                                 0x00008000u, NULL, true)) {
        return false;
    }

    return true;
}

bool wiiu_memory_install_default_regions(WiiUMemory* memory) {
    /* Cafe overlay memory is independent of MEM2, not its uncached alias.
       BOTW uses A0400180 for compressed read buffers while allocating live
       decompressed resources across 20400180 in MEM2. */
    if (!wiiu_memory_add_segment(memory, "overlay_arena", WIIU_GUEST_OVERLAY_BASE,
                                 WIIU_GUEST_OVERLAY_SIZE, NULL, true)) {
        return false;
    }
    if (!wiiu_memory_add_segment(memory, "host_heap", WIIU_GUEST_HEAP_BASE,
                                 WIIU_GUEST_HEAP_SIZE, NULL, true)) {
        return false;
    }

    if (!wiiu_memory_add_segment(memory, "host_aux_heap",
                                 WIIU_GUEST_AUX_HEAP_BASE,
                                 WIIU_GUEST_AUX_HEAP_SIZE, NULL, true)) {
        return false;
    }

    if (!wiiu_memory_add_segment(memory, "host_gpu_heap",
                                 WIIU_GUEST_GPU_HEAP_BASE,
                                 WIIU_GUEST_GPU_HEAP_SIZE, NULL, true)) {
        return false;
    }

    if (!wiiu_memory_add_segment(memory, "host_os", WIIU_GUEST_OS_BASE,
                                 WIIU_GUEST_OS_SIZE, NULL, true)) {
        return false;
    }

    if (!wiiu_memory_add_segment(memory, "initial_stack",
                                 WIIU_GUEST_STACK_BASE, WIIU_GUEST_STACK_SIZE,
                                 NULL, true)) {
        return false;
    }

    return true;
}

u32 wiiu_memory_canonical_address(u32 address) {
    /* Cafe's 0x8-0x9 virtual region aliases the low 512 MiB of memory. */
    if ((address & 0xE0000000u) == 0x80000000u)
        return address & 0x1FFFFFFFu;
    /* 0xA0000000..0xBBFFFFFF is Cafe's separate 448 MiB overlay arena. */
    return address;
}

WiiUMemorySegment* wiiu_memory_find(WiiUMemory* memory, u32 address,
                                     u32 size) {
    if (!memory)
        return NULL;

    address = wiiu_memory_canonical_address(address);
    WiiUMemorySegment* segment = memory->last_segment;
    if (segment && range_contains(segment->base, segment->size, address,
                                  size)) {
        return segment;
    }

    if (memory->page_map) {
        segment = memory->page_map[address >> WIIU_MEMORY_PAGE_SHIFT];
        if (segment && range_contains(segment->base, segment->size, address,
                                      size)) {
            memory->last_segment = segment;
            return segment;
        }
    }

    for (u32 i = 0; i < memory->segment_count; i++) {
        segment = &memory->segments[i];
        if (range_contains(segment->base, segment->size, address, size))
        {
            memory->last_segment = segment;
            return segment;
        }
    }

    return NULL;
}

static u8* segment_ptr(WiiUMemorySegment* segment, u32 address) {
    return segment->data + (address - segment->base);
}

static u64 wiiu_external_read(CPUState* cpu, u32 ea, u8 size) {
    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    const u32 address = wiiu_memory_canonical_address(ea);
    const u32 offset = address & (WIIU_MEMORY_PAGE_SIZE - 1u);
    const u8* p = NULL;
    /* Only fully mapped pages have a direct pointer. Keep partial-page and
       cross-page accesses on the checked segment path, including diagnostics.
       Stores still use that path to preserve cross-CPU reservations and
       read-only warnings. No CPUState/compiled-runtime ABI change is needed. */
    if (memory->direct_page_map && size &&
        size <= WIIU_MEMORY_PAGE_SIZE - offset) {
        p = memory->direct_page_map[address >> WIIU_MEMORY_PAGE_SHIFT];
        if (p) p += offset;
    }
    if (!p) {
        WiiUMemorySegment* segment = wiiu_memory_find(memory, ea, size);
        if (!segment) {
            if (memory->unmapped_read_count++ < 32) {
                fprintf(stderr,
                        "warn: Wii U read%u from unmapped 0x%08X pc=0x%08X "
                        "lr=0x%08X\n",
                        (unsigned)(size * 8), ea, cpu->pc, cpu->lr);
            }
            return 0;
        }
        p = segment_ptr(segment, address);
    }
    switch (size) {
    case 1:
        return p[0];
    case 2:
        return read_be16(p);
    case 4:
        return read_be32(p);
    case 8:
        return read_be64(p);
    default:
        return 0;
    }
}

void wiiu_memory_track_reservation(CPUState* cpu) {
    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    if (!memory) return;
    for (u32 i = 0; i < memory->reservation_count; ++i) {
        if (memory->reservations[i] != cpu) continue;
        if (!cpu->reserve_valid)
            memory->reservations[i] = memory->reservations[--memory->reservation_count];
        return;
    }
    if (!cpu->reserve_valid) return;
    if (memory->reservation_count < 256u)
        memory->reservations[memory->reservation_count++] = cpu;
    else
        cpu->reserve_valid = false; /* A reservation may fail spuriously. */
}

static void invalidate_reservation(CPUState* cpu, u32 first, u32 last) {
    if (!cpu->reserve_valid) return;
    u32 line = wiiu_memory_canonical_address(cpu->reserve_addr) & ~31u;
    if (line >= first && line <= last) cpu->reserve_valid = false;
}

static void wiiu_external_write(CPUState* cpu, u32 ea, u64 value, u8 size) {
    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    static u32 title_runtime_global_writes;
    if (size == 4u && ea == 0x1047BE88u &&
        title_runtime_global_writes++ < 8u) {
        fprintf(stderr,
                "loader: title runtime global write=0x%08X pc=0x%08X "
                "lr=0x%08X r3=0x%08X r30=0x%08X r31=0x%08X\n",
                (u32)value, cpu->pc, cpu->lr, cpu->gpr[3],
                cpu->gpr[30], cpu->gpr[31]);
    }
    WiiUMemorySegment* segment = wiiu_memory_find(memory, ea, size);
    if (!segment) {
        if (memory->unmapped_write_count++ < 32) {
            fprintf(stderr,
                    "warn: Wii U write%u to unmapped 0x%08X pc=0x%08X "
                    "lr=0x%08X\n",
                    (unsigned)(size * 8), ea, cpu->pc, cpu->lr);
        }
        return;
    }

    if (!segment->writable && memory->readonly_write_count++ < 16) {
        fprintf(stderr,
                "warn: Wii U write%u to read-only segment %s at 0x%08X "
                "pc=0x%08X lr=0x%08X\n",
                (unsigned)(size * 8), segment->name, ea, cpu->pc, cpu->lr);
    }

    u32 address = wiiu_memory_canonical_address(ea);
    u32 first_line = address & ~31u;
    u32 last_line = (address + size - 1u) & ~31u;
    invalidate_reservation(cpu, first_line, last_line);
    /* Cached/coherent host memory still needs guest reservation semantics:
       a store by another cooperative CPU invalidates a pending stwcx. */
    for (u32 i = 0; i < memory->reservation_count;) {
        CPUState* other = memory->reservations[i];
        invalidate_reservation(other, first_line, last_line);
        if (!other->reserve_valid)
            memory->reservations[i] = memory->reservations[--memory->reservation_count];
        else ++i;
    }
    u8* p = segment_ptr(segment, address);
    switch (size) {
    case 1:
        p[0] = (u8)value;
        break;
    case 2:
        write_be16(p, (u16)value);
        break;
    case 4:
        write_be32(p, (u32)value);
        break;
    case 8:
        write_be64(p, value);
        break;
    default:
        break;
    }
}

void wiiu_memory_bind_cpu(WiiUMemory* memory, CPUState* cpu) {
    cpu->external_user_data = memory;
    cpu->external_read = wiiu_external_read;
    cpu->external_write = wiiu_external_write;
    /* Newer DolRecomp CPU cores route non-GC/Wii addresses through the
       external callbacks above instead of exposing the legacy fastmem table. */
}

void wiiu_memory_dump_map(const WiiUMemory* memory) {
    printf("Wii U memory map (%u segments):\n", memory->segment_count);
    for (u32 i = 0; i < memory->segment_count; i++) {
        const WiiUMemorySegment* segment = &memory->segments[i];
        printf("  %-24s 0x%08X - 0x%08X  %s\n", segment->name,
               segment->base, segment->base + segment->size,
               segment->writable ? "rw" : "ro");
    }
}
