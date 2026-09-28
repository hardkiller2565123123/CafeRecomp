#include "wiiu_memory.h"
#include <stdio.h>
#include <string.h>

extern void func_03A38020(CPUState* cpu);
extern void func_03A3C020(CPUState* cpu);
extern void func_03C44020(CPUState* cpu);

/* Exercise the game's actual animation tick and completion flag, including
   its integer-to-double duration conversion. No forced scene transitions. */
int main(void) {
    WiiUMemory memory;
    CPUState cpu = {0};
    const u32 base = WIIU_GUEST_HEAP_BASE, animation = base + 256;
    wiiu_memory_init(&memory);
    if (!wiiu_memory_add_segment(&memory, "animation", base, 8192, NULL, true) ||
        !wiiu_memory_add_segment(&memory, "constants", 0x1035D000, 4096, NULL, true)) return 1;
    wiiu_memory_bind_cpu(&memory, &cpu);
    cpu.msr |= 0x00002000u; /* Guest MSR[FP], as in the launcher. */
    mem_write32(&cpu, 0x1035D400, 0x43300000); /* 2^52 conversion bias */
    mem_write32(&cpu, animation + 8, base + 512);
    mem_write16(&cpu, base + 520, 8); /* Eight-frame Decide resource. */
    mem_write32(&cpu, animation + 44, 0x3F800000); /* Playback speed 1. */
    for (u32 tick = 0; tick < 8; ++tick) {
        cpu.gpr[1] = base + 8000;
        cpu.gpr[3] = animation;
        cpu.fpr[1] = 1.0;
        cpu.pc = 0x03A3BE20;
        cpu.lr = 0xFFFFFFFC;
        for (u32 step = 0; step < 100 && cpu.pc != 0xFFFFFFFC && !cpu.exception; ++step) {
            cpu.downcount = 8192;
            if (cpu.pc >= 0x03A38020 && cpu.pc < 0x03A3C020) func_03A38020(&cpu);
            else if (cpu.pc >= 0x03A3C020 && cpu.pc < 0x03A40020) func_03A3C020(&cpu);
            else if (cpu.pc >= 0x03C44020 && cpu.pc < 0x03C48020) func_03C44020(&cpu);
            else { fprintf(stderr, "Unexpected animation PC %08X\n", cpu.pc); return 1; }
        }
        float expected = (float)(tick + 1);
        u32 expected_bits;
        memcpy(&expected_bits, &expected, sizeof(expected_bits));
        if (cpu.exception || cpu.pc != 0xFFFFFFFC ||
            mem_read32(&cpu, animation + 12) != expected_bits ||
            (mem_read8(&cpu, animation + 51) & 1u) != (tick == 7)) {
            fprintf(stderr, "Animation tick %u failed: pc=%08X frame=%08X flags=%02X\n",
                    tick, cpu.pc, mem_read32(&cpu, animation + 12), mem_read8(&cpu, animation + 51));
            return 1;
        }
    }
    int result = mem_read32(&cpu, animation + 44) != 0 ||
                 memory.unmapped_read_count || memory.unmapped_write_count;
    wiiu_memory_free(&memory);
    return result;
}
