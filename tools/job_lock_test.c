#include "wiiu_memory.h"
#include <stdio.h>

/* Run the actual generated queue-consumer chunk at the captured contention
   point. No game assets, forced unlocks, or synthetic completion flags. */
extern void func_030DC020(CPUState* cpu);

static void cache_fallback(CPUState* cpu, u32 raw, u32 cia) {
    if (((raw >> 1) & 1023u) != 54u) { cpu->exception = 1; return; }
    cpu->pc = cia + 4;
}

static void step(CPUState* cpu) {
    cpu->downcount = 256;
    if (cpu->pc == 0x0434C6C8u) cpu->pc = cpu->lr; /* OSMemoryBarrier */
    else func_030DC020(cpu);
    wiiu_memory_track_reservation(cpu);
}

int main(void) {
    WiiUMemory memory;
    CPUState main_cpu = {0}, worker = {0};
    const u32 queue = WIIU_GUEST_HEAP_BASE;
    wiiu_memory_init(&memory);
    if (!wiiu_memory_add_segment(&memory, "queue", queue, 4096, NULL, true)) return 1;
    wiiu_memory_bind_cpu(&memory, &main_cpu);
    wiiu_memory_bind_cpu(&memory, &worker);
    main_cpu.instruction_fallback = worker.instruction_fallback = cache_fallback;
    mem_write32(&main_cpu, queue + 16, 1);
    mem_write32(&main_cpu, queue + 120, 2);
    mem_write8(&main_cpu, queue + 128, 1);
    main_cpu.pc = 0x030DF674u;
    main_cpu.lr = 0x030DF624u;
    main_cpu.gpr[12] = main_cpu.gpr[29] = queue + 16;
    main_cpu.gpr[22] = 1;
    main_cpu.gpr[24] = queue;
    main_cpu.gpr[25] = queue + 256;
    main_cpu.gpr[28] = 1;
    worker.pc = 0x030DF730u;
    worker.lr = 0x030DF730u;
    worker.gpr[12] = worker.gpr[29] = worker.gpr[31] = queue + 16;
    worker.gpr[24] = queue;
    worker.gpr[25] = queue + 260;
    worker.gpr[28] = 1;
    u32 turns;
    for (turns = 0; turns < 128; ++turns) {
        step(&main_cpu);
        if (mem_read32(&main_cpu, queue + 124) == 0) step(&main_cpu);
        if (mem_read32(&main_cpu, queue + 124) != 0) break;
        step(&worker);
        if (main_cpu.exception || worker.exception) break;
    }
    int result = turns == 128 || main_cpu.exception || worker.exception ||
                 mem_read32(&main_cpu, queue + 124) != 1 ||
                 memory.unmapped_read_count || memory.unmapped_write_count;
    fprintf(stderr, "queue contention: turns=%u main=%08X worker=%08X claimed=%u\n",
            turns, main_cpu.pc, worker.pc, mem_read32(&main_cpu, queue + 124));
    wiiu_memory_free(&memory);
    return result;
}
