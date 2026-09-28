#include <stdio.h>
#include <math.h>
#include <string.h>

#include "../src/cpu/cpu.h"

void func_80004020(CPUState* ctx);
void func_80004040(CPUState* ctx);
void func_80004060(CPUState* ctx);
void func_80004068(CPUState* ctx);
void func_80004070(CPUState* ctx);

static u64 bits_of(f64 value) {
    u64 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

int main(void) {
    CPUState cpu;
    if (!cpu_init(&cpu))
        return 1;

    cpu.pc = 0x80004020u;
    cpu.lr = 0x81234564u;
    cpu.gpr[3] = 1000;

    u32 calls = 0;
    while (cpu.pc != cpu.lr && calls < 32) {
        cpu.downcount = 0;
        func_80004020(&cpu);
        calls++;
    }

    int integer_ok = cpu.pc == cpu.lr && cpu.gpr[3] == 0 &&
                     (cpu.cr & 0xF0000000u) == 0x20000000u && calls < 20;
    if (!integer_ok) {
        fprintf(stderr, "pc=%08X r3=%u cr=%08X calls=%u\n",
                cpu.pc, cpu.gpr[3], cpu.cr, calls);
    }
    for (u32 i = 0; i < 1000; ++i)
        mem_write32(&cpu, 0x80001000u + i * 4u, i);
    cpu.pc = 0x80004040u;
    cpu.lr = 0x81234564u;
    cpu.gpr[3] = 1000;
    cpu.gpr[5] = 0x80001000u;
    calls = 0;
    while (cpu.pc != cpu.lr && calls < 32) {
        cpu.downcount = 0;
        func_80004040(&cpu);
        calls++;
    }
    int memory_ok = cpu.pc == cpu.lr && cpu.gpr[3] == 0 &&
                    cpu.gpr[4] == 999 && cpu.gpr[5] == 0x80001FA0u &&
                    calls < 24;
    if (!memory_ok) {
        fprintf(stderr, "memory pc=%08X r3=%u r4=%u r5=%08X calls=%u\n",
                cpu.pc, cpu.gpr[3], cpu.gpr[4], cpu.gpr[5], calls);
    }

    cpu.msr = 0x00002000u;
    cpu.lr = 0x81234564u;
    cpu.fpscr = 0;
    cpu.fpr[1] = NAN;
    cpu.fpr[2] = 1.0;
    cpu.pc = 0x80004060u;
    func_80004060(&cpu);
    int compare_ok = (cpu.fpscr & 0x00080000u) != 0 &&
                     ((cpu.fpscr >> 12) & 0xFu) == 1u &&
                     ((cpu.cr >> 20) & 0xFu) == 1u;

    cpu.fpscr = 0xA0000000u;
    cpu.cr = 0;
    cpu.fpr[1] = 1.25;
    cpu.fpr[2] = 2.5;
    cpu.pc = 0x80004068u;
    func_80004068(&cpu);
    int record_ok = cpu.fpr[3] == 3.75 && cpu.ps1[3] == 3.75 &&
                    ((cpu.cr >> 24) & 0xFu) == 0xAu;

    cpu.fpr[1] = 0x1.0000000000001p+0;
    cpu.fpr[2] = -0x1.0000000000001p+0;
    u64 merge_a = bits_of(cpu.fpr[1]);
    u64 merge_b = bits_of(cpu.fpr[2]);
    cpu.pc = 0x80004070u;
    func_80004070(&cpu);
    int merge_ok = bits_of(cpu.fpr[5]) == merge_a &&
                   bits_of(cpu.ps1[5]) == merge_b;

    if (!compare_ok || !record_ok || !merge_ok) {
        fprintf(stderr, "float compare=%d record=%d merge=%d fpscr=%08X cr=%08X\n",
                compare_ok, record_ok, merge_ok, cpu.fpscr, cpu.cr);
    }

    cpu_free(&cpu);
    return !(integer_ok && memory_ok && compare_ok && record_ok && merge_ok);
}
