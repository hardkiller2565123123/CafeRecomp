#include "cpu/cpu.h"

#include <stdio.h>

void func_80002D00(CPUState* cpu);
void func_80003500(CPUState* cpu);
void func_80003600(CPUState* cpu);
void func_80003B00(CPUState* cpu);
void func_80003B08(CPUState* cpu);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #x); return 1; } } while (0)

static u32 blocked_address;
static u32 callback_address;
static u32 callbacks;
static u32 queries;
static u32 callback_mode;

static bool intercept(CPUState* cpu, u32 address) {
    if (address == PPC_HOST_CALL_NATIVE_REGION_QUERY) {
        bool blocked = blocked_address >= cpu->external_addr &&
                       blocked_address < cpu->external_value;
        cpu->external_rid = PPC_NATIVE_REGION_QUERY_HANDLED;
        queries++;
        return blocked;
    }
    if (address != callback_address)
        return false;
    callbacks++;
    if (callback_mode == 1) {
        cpu->gpr[4] = 90u;
        cpu->pc = cpu->lr;
        return true;
    }
    if (callback_mode == 2)
        return false;
    if (callback_mode == 3) {
        cpu->gpr[3] = 55u;
        cpu->pc = 0x80002E04u;
        return true;
    }
    if (callback_mode == 4) {
        cpu->lr = cpu->gpr[0];
        cpu->pc = 0x80003B0Cu;
        return true;
    }
    return false;
}

static void prepare(CPUState* cpu, u32 pc) {
    cpu->pc = pc;
    cpu->lr = 0x81234564u;
    cpu->exception = 0;
    cpu->program_exception = 0;
    cpu->downcount = 0;
}

static void enable(CPUState* cpu, u32 blocked, u32 address, u32 mode) {
    blocked_address = blocked;
    callback_address = address;
    callback_mode = mode;
    callbacks = 0;
    queries = 0;
    cpu->host_call = intercept;
}

int main(void) {
    CPUState cpu;
    CHECK(cpu_init(&cpu));

    prepare(&cpu, 0x80003500u);
    cpu.gpr[3] = 10u;
    cpu.gpr[4] = 20u;
    func_80003500(&cpu);
    CHECK(cpu.gpr[3] == 11u && cpu.gpr[4] == 22u);

    prepare(&cpu, 0x80003500u);
    cpu.gpr[3] = 10u;
    cpu.gpr[4] = 20u;
    enable(&cpu, 0x80003600u, 0x80003600u, 1);
    func_80003500(&cpu);
    CHECK(cpu.pc == 0x80003600u && cpu.gpr[4] == 20u && callbacks == 0u);
    CHECK(ppc_host_call(&cpu, cpu.pc));
    CHECK(callbacks == 1u && cpu.gpr[4] == 90u && cpu.pc == 0x80003508u);
    func_80003500(&cpu);
    CHECK(cpu.gpr[3] == 11u && cpu.gpr[4] == 90u && cpu.pc == 0x81234564u);

    prepare(&cpu, 0x80003500u);
    cpu.gpr[3] = 10u;
    cpu.gpr[4] = 20u;
    enable(&cpu, 0x80003604u, 0x80003604u, 2);
    func_80003500(&cpu);
    CHECK(cpu.pc == 0x80003600u && cpu.gpr[4] == 20u);
    cpu.gpr[4] += 2u;
    cpu.pc = 0x80003604u;
    CHECK(!ppc_host_call(&cpu, cpu.pc));
    CHECK(callbacks == 1u);
    cpu.pc = cpu.lr;
    func_80003500(&cpu);
    CHECK(cpu.gpr[3] == 11u && cpu.gpr[4] == 22u);

    prepare(&cpu, 0x80003604u);
    cpu.gpr[4] = 20u;
    enable(&cpu, 0x80003604u, 0x80003604u, 2);
    func_80003600(&cpu);
    CHECK(cpu.pc == 0x80003604u && cpu.gpr[4] == 20u && callbacks == 0u);
    CHECK(!ppc_host_call(&cpu, cpu.pc));
    CHECK(callbacks == 1u);

    prepare(&cpu, 0x80002D00u);
    enable(&cpu, 0x80002E00u, 0x80002E00u, 3);
    func_80002D00(&cpu);
    CHECK(cpu.pc == 0x80002E00u && cpu.gpr[3] != 55u);
    CHECK(ppc_host_call(&cpu, cpu.pc));
    CHECK(callbacks == 1u && cpu.gpr[3] == 55u);

    prepare(&cpu, 0x80003B00u);
    cpu.gpr[5] = 3u;
    cpu.gpr[6] = 7u;
    enable(&cpu, 0x80003B08u, 0x80003B08u, 4);
    func_80003B00(&cpu);
    CHECK(cpu.pc == 0x80003B08u && cpu.gpr[6] == 8u && cpu.gpr[5] == 3u);
    func_80003B08(&cpu);
    CHECK(cpu.pc == 0x80003B08u && cpu.gpr[5] == 3u);
    CHECK(ppc_host_call(&cpu, cpu.pc));
    CHECK(callbacks == 1u && cpu.pc == 0x80003B0Cu);
    cpu.gpr[5] += 7u;
    cpu.pc = cpu.lr;
    CHECK(cpu.gpr[5] == 10u && cpu.pc == 0x81234564u);

    prepare(&cpu, 0x80003600u);
    cpu.gpr[4] = 20u;
    enable(&cpu, 0x80003604u, 0x80003604u, 2);
    func_80003600(&cpu);
    CHECK(cpu.gpr[4] == 20u && queries != 0u);
    cpu.host_call = NULL;
    prepare(&cpu, 0x80003600u);
    func_80003600(&cpu);
    CHECK(cpu.gpr[4] == 22u && cpu.pc == 0x81234564u);

    cpu_free(&cpu);
    return 0;
}
