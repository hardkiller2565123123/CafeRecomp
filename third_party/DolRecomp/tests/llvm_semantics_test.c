#include "cpu/cpu.h"

#include <stdio.h>
#include <string.h>

void func_80001000(CPUState* cpu);
void func_80002880(CPUState* cpu);
void func_80002900(CPUState* cpu);
void func_80003000(CPUState* cpu);
void func_80003100(CPUState* cpu);
void func_80003200(CPUState* cpu);
void func_80003300(CPUState* cpu);
void func_80003400(CPUState* cpu);
void func_80003500(CPUState* cpu);
void func_80003700(CPUState* cpu);
void func_80003710(CPUState* cpu);
void func_80003720(CPUState* cpu);
void func_80003740(CPUState* cpu);
void func_80003750(CPUState* cpu);
void func_80003760(CPUState* cpu);
void func_80003800(CPUState* cpu);
void func_80003D20(CPUState* cpu);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #x); return 1; } } while (0)

static u32 random_state = 0xD01C0DEu;
static u32 reads;
static u32 writes;
static u32 fallbacks;
static u32 cache_calls;
static u8 cache_op;
static u32 cache_ea;

static void prepare(CPUState* cpu, u32 pc) {
    cpu->pc = pc;
    cpu->lr = 0x81234567u;
    cpu->exception = 0;
    cpu->program_exception = 0;
}

static u32 random_u32(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static u32 leading_zeros(u32 value) {
    u32 count = 0;
    while (count < 32u && !(value & (0x80000000u >> count)))
        count++;
    return count;
}

static void reference_integer(CPUState* cpu) {
    cpu->gpr[3] = cpu->gpr[4] + cpu->gpr[5];
    cpu->gpr[6] = cpu->gpr[8] - cpu->gpr[7];
    cpu->gpr[9] = (u32)((s64)(s32)cpu->gpr[10] * (s64)(s32)cpu->gpr[11]);
    cpu->gpr[12] = cpu->gpr[13] & cpu->gpr[14];
    cpu->gpr[15] = cpu->gpr[16] | cpu->gpr[17];
    cpu->gpr[18] = cpu->gpr[19] ^ cpu->gpr[20];
    u32 left = cpu->gpr[23] & 63u;
    u32 right = cpu->gpr[26] & 63u;
    cpu->gpr[21] = left < 32u ? cpu->gpr[22] << left : 0;
    cpu->gpr[24] = right < 32u ? cpu->gpr[25] >> right : 0;
    cpu->gpr[27] = leading_zeros(cpu->gpr[28]);
    cpu->pc = cpu->lr & ~3u;
}

static int integer_properties(CPUState* generated, CPUState* reference) {
    for (u32 iteration = 0; iteration < 1000; iteration++) {
        for (u32 reg = 0; reg < 32; reg++)
            generated->gpr[reg] = reference->gpr[reg] = random_u32();
        generated->xer = reference->xer = random_u32();
        generated->cr = reference->cr = random_u32();
        prepare(generated, 0x80003000u);
        prepare(reference, 0x80003000u);
        func_80003000(generated);
        reference_integer(reference);
        if (memcmp(generated->gpr, reference->gpr,
                   sizeof(generated->gpr)) || generated->xer != reference->xer ||
            generated->cr != reference->cr || generated->pc != reference->pc) {
            fprintf(stderr, "integer mismatch seed=0x%08X iteration=%u\n",
                    0xD01C0DEu, iteration);
            return 0;
        }
    }
    return 1;
}

static void fallback(CPUState* cpu, u32 raw, u32 cia) {
    if (!raw)
        fallbacks++;
    cpu->pc = cia + 4u;
}

static u64 external_read(CPUState* cpu, u32 ea, u8 size) {
    reads++;
    cpu->external_addr = ea;
    cpu->external_rid = size;
    return 0xA1B2C3D4u;
}

static u64 faulting_read(CPUState* cpu, u32 ea, u8 size) {
    (void)size;
    ppc_dsi_exception(cpu, ea, 0x80003100u, PPC_DSI_EAR_DISABLED);
    return 0;
}

static void external_write(CPUState* cpu, u32 ea, u64 value, u8 size) {
    writes++;
    cpu->external_addr = ea;
    cpu->external_value = (u32)value;
    cpu->external_rid = size;
}

static void external_write32(CPUState* cpu, u32 ea, u32 value, u8 rid) {
    external_write(cpu, ea, value, rid);
}

static void cache_control(CPUState* cpu, u8 operation, u32 ea, u32 cia) {
    (void)cpu;
    (void)cia;
    cache_calls++;
    cache_op = operation;
    cache_ea = ea;
}

int main(void) {
    CPUState cpu;
    CPUState reference;
    CHECK(cpu_init(&cpu));
    CHECK(cpu_init(&reference));
    CHECK(integer_properties(&cpu, &reference));

    prepare(&cpu, 0x80001004u);
    cpu.gpr[3] = 0;
    cpu.gpr[1] = GC_RAM_BASE;
    cpu.fpr[18] = 2.0;
    cpu.fpr[19] = 3.0;
    cpu.fpr[20] = 4.0;
    cpu.downcount = 0;
    cpu.msr = 1u << 13;
    cpu.instruction_fallback = fallback;
    func_80001000(&cpu);
    CHECK(fallbacks == 1 && cpu.gpr[3] == 10 && cpu.pc == 0x81234564u);

    prepare(&cpu, 0x80002880u);
    cpu.msr = 1u << 13;
    cpu.hid2 = PPC_HID2_LSQE | PPC_HID2_PSE;
    cpu.gqr[0] = 0;
    cpu.gpr[4] = GC_RAM_BASE + 0x340u;
    mem_write32(&cpu, cpu.gpr[4], 0x3FA00000u);
    mem_write32(&cpu, cpu.gpr[4] + 4u, 0xC0200000u);
    func_80002880(&cpu);
    CHECK(cpu.fpr[1] == 1.25 && cpu.ps1[1] == -2.5);
    CHECK(mem_read32(&cpu, cpu.gpr[4] + 8u) == 0x3FA00000u);
    CHECK(mem_read32(&cpu, cpu.gpr[4] + 12u) == 0xC0200000u);

    prepare(&cpu, 0x80002880u);
    cpu.gqr[0] = 0x00040004u;
    cpu.gpr[4] = GC_RAM_BASE + 0x360u;
    mem_write8(&cpu, cpu.gpr[4], 10u);
    mem_write8(&cpu, cpu.gpr[4] + 1u, 20u);
    func_80002880(&cpu);
    CHECK(mem_read8(&cpu, cpu.gpr[4] + 8u) == 10u);
    CHECK(mem_read8(&cpu, cpu.gpr[4] + 9u) == 20u);

    prepare(&cpu, 0x80002880u);
    cpu.gqr[0] = 0;
    cpu.gpr[4] = GC_RAM_BASE + 0x362u;
    func_80002880(&cpu);
    CHECK((cpu.exception & PPC_EXC_ALIGNMENT) && cpu.dar == cpu.gpr[4]);

    prepare(&cpu, 0x80002900u);
    cpu.ear = 0x80000007u;
    cpu.external_write32 = external_write32;
    cpu.gpr[11] = 0xCAFEBABEu;
    cpu.gpr[12] = 0x1000u;
    cpu.gpr[13] = 0x22u;
    func_80002900(&cpu);
    CHECK((cpu.exception & PPC_EXC_ALIGNMENT) && cpu.dar == 0x1022u);

    prepare(&cpu, 0x80003100u);
    cpu.gpr[4] = GC_RAM_BASE + 0x400u;
    cpu.gpr[5] = 10;
    cpu.reserve_addr = cpu.gpr[4];
    cpu.reserve_valid = true;
    mem_write32(&cpu, cpu.gpr[4], 0x11223344u);
    func_80003100(&cpu);
    CHECK(cpu.gpr[3] == 0x11223344u && cpu.gpr[5] == 11);
    CHECK(mem_read32(&cpu, cpu.gpr[4] + 4u) == 0x11223344u);
    CHECK(!cpu.reserve_valid);

    CHECK(cpu_alloc_mem2(&cpu, 0x1000u));
    prepare(&cpu, 0x80003100u);
    cpu.gpr[4] = WII_MEM2_BASE + 0x100u;
    cpu.gpr[5] = 20;
    mem_write32(&cpu, cpu.gpr[4], 0x55667788u);
    func_80003100(&cpu);
    CHECK(cpu.gpr[3] == 0x55667788u && cpu.gpr[5] == 21);
    CHECK(mem_read32(&cpu, cpu.gpr[4] + 4u) == 0x55667788u);

    prepare(&cpu, 0x80003100u);
    cpu.gpr[4] = GC_RAM_BASE + 0x421u;
    mem_write32(&cpu, cpu.gpr[4], 0x89ABCDEFu);
    func_80003100(&cpu);
    CHECK(cpu.gpr[3] == 0x89ABCDEFu);
    CHECK(mem_read32(&cpu, cpu.gpr[4] + 4u) == 0x89ABCDEFu);

    prepare(&cpu, 0x80003100u);
    cpu.external_read = external_read;
    cpu.external_write = external_write;
    cpu.gpr[4] = GC_RAM_BASE + cpu.ram_size - 3u;
    cpu.gpr[5] = 30;
    reads = writes = 0;
    func_80003100(&cpu);
    CHECK(cpu.gpr[3] == 0xA1B2C3D4u && cpu.gpr[5] == 31);
    CHECK(reads == 1 && writes == 1);
    CHECK(cpu.external_addr == GC_RAM_BASE + cpu.ram_size + 1u);

    prepare(&cpu, 0x80003100u);
    cpu.external_read = faulting_read;
    cpu.gpr[4] = 0x70000000u;
    cpu.gpr[5] = 40;
    func_80003100(&cpu);
    CHECK((cpu.exception & PPC_EXC_DSI) && cpu.pc == PPC_VECTOR_DSI);
    CHECK(cpu.dar == 0x70000000u && cpu.gpr[5] == 40);

    prepare(&cpu, 0x80003300u);
    cpu.reserve_addr = GC_RAM_BASE + 0x600u;
    cpu.reserve_valid = true;
    mem_write32(&cpu, GC_RAM_BASE + 0x600u, 0x13579BDFu);
    func_80003300(&cpu);
    CHECK(cpu.gpr[3] == 0x13579BDFu);
    CHECK(cpu.gpr[4] == GC_RAM_BASE);
    CHECK(mem_read32(&cpu, GC_RAM_BASE + 0x604u) == 0x13579BDFu);
    CHECK(!cpu.reserve_valid);

    prepare(&cpu, 0x80003200u);
    cpu.gpr[4] = GC_RAM_BASE + 0x500u;
    cpu.gpr[5] = GC_RAM_BASE + 0x510u;
    mem_write32(&cpu, cpu.gpr[4], 0x01020304u);
    func_80003200(&cpu);
    CHECK(cpu.gpr[3] == 0x04030201u);
    CHECK(mem_read32(&cpu, cpu.gpr[5]) == 0x01020304u);

    prepare(&cpu, 0x80003400u);
    cpu.cache_control = cache_control;
    cpu.gpr[17] = GC_RAM_BASE + 0x800u;
    cpu.gpr[18] = 0x24u;
    func_80003400(&cpu);
    CHECK(cache_calls == 1 && cache_op == PPC_CACHE_ICBI);
    CHECK(cache_ea == GC_RAM_BASE + 0x824u);

    prepare(&cpu, 0x80003500u);
    cpu.gpr[3] = 10;
    cpu.gpr[4] = 20;
    func_80003500(&cpu);
    CHECK(cpu.gpr[3] == 11 && cpu.gpr[4] == 22);
    CHECK(cpu.pc == 0x81234564u);

    prepare(&cpu, 0x80003700u);
    cpu.cr = 0xA5C39E71u;
    func_80003700(&cpu);
    CHECK(cpu.gpr[10] == 0xA5C39E71u && cpu.cr == 0xA5C39E71u);

    prepare(&cpu, 0x80003710u);
    cpu.cr = 0x01234567u;
    cpu.gpr[10] = 0xFEDCBA98u;
    func_80003710(&cpu);
    CHECK(cpu.cr == 0xFEDCBA98u && cpu.gpr[11] == 0xFEDCBA98u);

    prepare(&cpu, 0x80003720u);
    cpu.cr = 0x0ABCDEF0u;
    cpu.xer = 0x80000000u;
    cpu.gpr[3] = 0xFFFFFFFFu;
    func_80003720(&cpu);
    CHECK(cpu.cr == 0x9ABCDEF0u && cpu.gpr[10] == 0x9ABCDEF0u);

    prepare(&cpu, 0x80003740u);
    cpu.xer = 0;
    cpu.gpr[10] = 0xE1234567u;
    func_80003740(&cpu);
    CHECK(cpu.xer == 0xE1234567u && cpu.gpr[11] == 0xE1234567u);

    prepare(&cpu, 0x80003750u);
    cpu.xer = 0xC1234567u;
    cpu.gpr[3] = 0xFFFFFFFFu;
    cpu.gpr[4] = 1u;
    cpu.gpr[7] = 0xFFFFFFFFu;
    cpu.gpr[8] = 0u;
    func_80003750(&cpu);
    CHECK(cpu.gpr[5] == 0 && cpu.gpr[6] == 0);
    CHECK(cpu.xer == 0xE1234567u && cpu.gpr[10] == 0xE1234567u);

    prepare(&cpu, 0x80003760u);
    cpu.cr = 0x12345678u;
    cpu.xer = 0xE1234567u;
    func_80003760(&cpu);
    CHECK(cpu.cr == 0x12E45678u && cpu.gpr[10] == 0x12E45678u);
    CHECK(cpu.xer == 0x01234567u && cpu.gpr[11] == 0x01234567u);

    prepare(&cpu, 0x80003800u);
    cpu.xer = 0;
    func_80003800(&cpu);
    while (cpu.pc != 0x81234564u)
        func_80003800(&cpu);
    CHECK(cpu.gpr[3] == 100u && (cpu.cr >> 28) == 2u);

    prepare(&cpu, 0x80003D20u);
    cpu.msr = 0;
    cpu.gpr[3] = 0x8000u;
    func_80003D20(&cpu);
    CHECK(cpu.msr == 0x8000u && cpu.pc == 0x80003D24u);

    prepare(&cpu, 0x80003D20u);
    cpu.msr = 0x8000u;
    cpu.gpr[3] = 0x8000u;
    func_80003D20(&cpu);
    CHECK(cpu.msr == 0x8000u && cpu.pc == 0x81234564u);

    cpu_free(&reference);
    cpu_free(&cpu);
    return 0;
}
