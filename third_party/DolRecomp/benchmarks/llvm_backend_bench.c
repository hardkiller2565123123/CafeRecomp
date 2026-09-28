#include "cpu/cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

void func_80003100(CPUState *cpu);
void func_80003300(CPUState *cpu);
void func_80003500(CPUState *cpu);
void func_80003750(CPUState *cpu);
void func_80003800(CPUState *cpu);
void func_80003900(CPUState *cpu);
void func_80003920(CPUState *cpu);
void func_80003940(CPUState *cpu);
void func_80003980(CPUState *cpu);
void func_800039C0(CPUState *cpu);
void func_80003A00(CPUState *cpu);
void func_80003A20(CPUState *cpu);
void func_80003A60(CPUState *cpu);
void func_80003AA0(CPUState *cpu);
void func_80004200(CPUState *cpu);
void func_80004500(CPUState *cpu);

static u32 interception_address;
static u64 interception_queries;

static bool region_query(CPUState *cpu, u32 address) {
  if (address != PPC_HOST_CALL_NATIVE_REGION_QUERY)
    return false;
  interception_queries++;
  bool blocked = interception_address >= cpu->external_addr &&
                 interception_address < cpu->external_value;
  cpu->external_rid = PPC_NATIVE_REGION_QUERY_HANDLED;
  return blocked;
}

static double seconds(void) {
#if defined(_WIN32)
  LARGE_INTEGER frequency;
  LARGE_INTEGER now;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&now);
  return (double)now.QuadPart / (double)frequency.QuadPart;
#else
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
#endif
}

static void prepare(CPUState *cpu, u32 pc) {
  cpu->pc = pc;
  cpu->lr = 0x81234567u;
  cpu->exception = 0;
  cpu->downcount = 1000000000;
  cpu->msr = 1u << 13;
}

static void report(const char *name, u32 iterations, double begin,
                   volatile u64 checksum) {
  double elapsed = seconds() - begin;
  printf("%-12s iterations=%u seconds=%.6f ns/op=%.2f checksum=%llu\n", name,
         iterations, elapsed, elapsed * 1e9 / iterations,
         (unsigned long long)checksum);
}

static void bench_cr_loop(CPUState *cpu, u32 iterations) {
  volatile u64 checksum = 0;
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80003800u);
    do {
      func_80003800(cpu);
    } while (cpu->pc != 0x81234564u);
    checksum += cpu->gpr[3];
  }
  report("cr_loop", iterations, begin, checksum);
}

static void bench_carry(CPUState *cpu, u32 iterations) {
  volatile u64 checksum = 0;
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80003750u);
    cpu->gpr[3] = 0xFFFFFFFFu;
    cpu->gpr[4] = i | 1u;
    cpu->gpr[7] = 0xFFFFFFFFu;
    cpu->gpr[8] = i;
    func_80003750(cpu);
    checksum += cpu->gpr[5] + cpu->gpr[6];
  }
  report("carry_chain", iterations, begin, checksum);
}

static void bench_memory(CPUState *cpu, u32 iterations) {
  volatile u64 checksum = 0;
  cpu->gpr[4] = GC_RAM_BASE + 0x1000u;
  mem_write32(cpu, cpu->gpr[4], 0x12345678u);
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80003100u);
    cpu->gpr[5] = i;
    func_80003100(cpu);
    checksum += cpu->gpr[3] + cpu->gpr[5];
  }
  report("fastmem", iterations, begin, checksum);
}

static void bench_static_memory(CPUState *cpu, u32 iterations) {
  volatile u64 checksum = 0;
  mem_write32(cpu, GC_RAM_BASE + 0x600u, 0x12345678u);
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80003300u);
    func_80003300(cpu);
    checksum += cpu->gpr[3];
  }
  report("staticmem", iterations, begin, checksum);
}

static void bench_static_memory_chain(CPUState *cpu, u32 iterations) {
  volatile u64 checksum = 0;
  mem_write32(cpu, GC_RAM_BASE + 0x600u, 0x12345678u);
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80004500u);
    cpu->gpr[5] = i;
    func_80004500(cpu);
    checksum += cpu->gpr[3] + cpu->gpr[5];
  }
  report("static_chain", iterations, begin, checksum);
}

static void bench_direct_call(CPUState *cpu, u32 iterations,
                              const char *mode) {
  volatile u64 checksum = 0;
  interception_queries = 0;
  interception_address = 0;
  cpu->host_call = NULL;
  if (!strcmp(mode, "call_unrelated")) {
    cpu->host_call = region_query;
    interception_address = 0x80004000u;
  } else if (!strcmp(mode, "call_target")) {
    cpu->host_call = region_query;
    interception_address = 0x80003600u;
  }
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80003500u);
    cpu->gpr[3] = i;
    cpu->gpr[4] = i;
    func_80003500(cpu);
    checksum += cpu->gpr[3] + cpu->gpr[4] + cpu->pc;
  }
  report(mode, iterations, begin, checksum + interception_queries);
}

static void bench_call_chain(CPUState *cpu, u32 iterations) {
  const u32 inner = 64;
  const u32 outer = (iterations + inner - 1u) / inner;
  volatile u64 checksum = 0;
  double begin = seconds();
  for (u32 i = 0; i < outer; i++) {
    prepare(cpu, 0x80004200u);
    cpu->gpr[4] = i;
    cpu->gpr[5] = inner;
    func_80004200(cpu);
    checksum += cpu->gpr[4] + cpu->pc;
  }
  report("call_chain", outer * inner, begin, checksum);
}

static void set_pair(CPUState *cpu, u32 reg, f32 lane0, f32 lane1) {
  cpu->fpr[reg] = lane0;
  cpu->ps1[reg] = lane1;
}

static void bench_paired_add(CPUState *cpu, u32 iterations) {
  volatile u64 checksum = 0;
  set_pair(cpu, 2, 1.25f, -3.5f);
  set_pair(cpu, 3, 4.0f, 0.75f);
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80003900u);
    func_80003900(cpu);
    checksum += (u64)(cpu->fpr[1] * 1024.0) ^ (u64)(cpu->ps1[1] * -1024.0);
  }
  report("ps_add", iterations, begin, checksum);
}

static void bench_paired_chain(CPUState *cpu, u32 iterations, bool helpers) {
  volatile u64 checksum = 0;
  set_pair(cpu, 2, 1.25f, -3.5f);
  set_pair(cpu, 3, 4.0f, 0.75f);
  set_pair(cpu, 5, 0.5f, -2.0f);
  set_pair(cpu, 7, 7.0f, 1.5f);
  set_pair(cpu, 9, -0.25f, 3.0f);
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80003920u);
    if (helpers) {
      ppc_ps_add_op(cpu, 1, 2, 3);
      ppc_ps_mul_op(cpu, 4, 1, 5);
      ppc_ps_add_op(cpu, 6, 4, 7);
      ppc_ps_sub_op(cpu, 8, 6, 9);
    } else {
      func_80003920(cpu);
    }
    checksum += (u64)(cpu->fpr[8] * 1024.0) ^ (u64)(cpu->ps1[8] * -1024.0);
  }
  report(helpers ? "ps_chain_ref" : "ps_chain", iterations, begin, checksum);
}

static void bench_psq_chain(CPUState *cpu, u32 iterations) {
  volatile u64 checksum = 0;
  const u32 address = GC_RAM_BASE + 0x2000u;
  cpu->gpr[4] = address;
  cpu->gqr[0] = 0;
  cpu->hid2 |= PPC_HID2_PSE | PPC_HID2_LSQE;
  set_pair(cpu, 5, 0.5f, -1.0f);
  mem_write32(cpu, address + 0, 0x3FA00000u);
  mem_write32(cpu, address + 4, 0xC0600000u);
  mem_write32(cpu, address + 8, 0x40800000u);
  mem_write32(cpu, address + 12, 0x3F400000u);
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80003940u);
    func_80003940(cpu);
    checksum += mem_read32(cpu, address + 16) ^ mem_read32(cpu, address + 20);
  }
  report("psq_chain", iterations, begin, checksum);
}

static void bench_paired_ssa(CPUState *cpu, u32 iterations, bool helpers) {
  volatile u64 checksum = 0;
  set_pair(cpu, 2, 1.25f, -3.5f);
  set_pair(cpu, 3, 4.0f, 0.75f);
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x800039C0u);
    if (helpers) {
      ppc_ps_add_op(cpu, 1, 2, 3);
      ppc_ps_mul_op(cpu, 4, 1, 1);
      ppc_ps_add_op(cpu, 6, 4, 1);
      ppc_ps_sub_op(cpu, 8, 6, 4);
    } else {
      func_800039C0(cpu);
    }
    checksum += (u64)(cpu->fpr[8] * 1024.0) ^ (u64)(cpu->ps1[8] * -1024.0);
  }
  report(helpers ? "ps_helpers" : "ps_ssa", iterations, begin, checksum);
}

static void bench_paired_fma(CPUState *cpu, u32 iterations, bool helpers) {
  volatile u64 checksum = 0;
  set_pair(cpu, 2, 1.25f, -3.5f);
  set_pair(cpu, 3, 4.0f, 0.75f);
  set_pair(cpu, 4, 0.5f, -2.0f);
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80003980u);
    if (helpers) {
      ppc_ps_madd_op(cpu, 10, 2, 3, 4, false, false);
      ppc_ps_madd_op(cpu, 11, 2, 3, 4, true, false);
      ppc_ps_madd_op(cpu, 12, 2, 3, 4, false, true);
      ppc_ps_madd_op(cpu, 13, 2, 3, 4, true, true);
      ppc_ps_madds0(cpu, 14, 2, 3, 4);
      ppc_ps_madds1(cpu, 15, 2, 3, 4);
    } else {
      func_80003980(cpu);
    }
    checksum += (u64)(cpu->fpr[15] * 1024.0) ^ (u64)(cpu->ps1[15] * -1024.0);
  }
  report(helpers ? "ps_fma_ref" : "ps_fma", iterations, begin, checksum);
}

static void bench_fma_single(CPUState *cpu, u32 iterations, bool helpers) {
  volatile u64 checksum = 0;
  set_pair(cpu, 2, 1.25f, -3.5f);
  set_pair(cpu, 3, 4.0f, 0.75f);
  set_pair(cpu, 4, 0.5f, -2.0f);
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80003A00u);
    if (helpers)
      ppc_ps_madd_op(cpu, 10, 2, 3, 4, false, false);
    else
      func_80003A00(cpu);
    checksum += (u64)(cpu->fpr[10] * 1024.0) ^ (u64)(cpu->ps1[10] * -1024.0);
  }
  report(helpers ? "fma_one_ref" : "fma_one", iterations, begin, checksum);
}

static void bench_fma_chain(CPUState *cpu, u32 iterations, bool helpers) {
  volatile u64 checksum = 0;
  set_pair(cpu, 2, 1.25f, -3.5f);
  set_pair(cpu, 3, 4.0f, 0.75f);
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80003A20u);
    if (helpers) {
      ppc_ps_add_op(cpu, 1, 2, 3);
      ppc_ps_madd_op(cpu, 4, 1, 1, 1, false, false);
      ppc_ps_madd_op(cpu, 5, 4, 1, 1, true, false);
      ppc_ps_madd_op(cpu, 6, 5, 1, 1, false, true);
      ppc_ps_madd_op(cpu, 7, 6, 1, 1, true, true);
      ppc_ps_madds0(cpu, 8, 7, 1, 1);
      ppc_ps_madds1(cpu, 9, 8, 1, 1);
    } else {
      func_80003A20(cpu);
    }
    checksum += (u64)(cpu->fpr[9] * 1024.0) ^ (u64)(cpu->ps1[9] * -1024.0);
  }
  report(helpers ? "fma_chain_ref" : "fma_chain", iterations, begin, checksum);
}

static void bench_psq_fma(CPUState *cpu, u32 iterations, bool helpers) {
  volatile u64 checksum = 0;
  const u32 address = GC_RAM_BASE + 0x2800u;
  cpu->gpr[4] = address;
  cpu->gqr[0] = 0;
  cpu->hid2 |= PPC_HID2_PSE | PPC_HID2_LSQE;
  mem_write32(cpu, address + 0, 0x3FA00000u);
  mem_write32(cpu, address + 4, 0xC0600000u);
  mem_write32(cpu, address + 8, 0x40800000u);
  mem_write32(cpu, address + 12, 0x3F400000u);
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80003A60u);
    if (helpers) {
      ppc_psq_load(cpu, 2, address, false, 0, false, 0x80003A60u);
      ppc_psq_load(cpu, 3, address + 8u, false, 0, false, 0x80003A64u);
      ppc_ps_madd_op(cpu, 1, 2, 3, 2, false, false);
      ppc_psq_store(cpu, 1, address + 16u, false, 0, false, 0x80003A6Cu);
    } else {
      func_80003A60(cpu);
    }
    checksum += mem_read32(cpu, address + 16) ^ mem_read32(cpu, address + 20);
  }
  report(helpers ? "psq_fma_ref" : "psq_fma", iterations, begin, checksum);
}

static void bench_fixed_gqr_fma(CPUState *cpu, u32 iterations, bool helpers) {
  volatile u64 checksum = 0;
  const u32 address = GC_RAM_BASE + 0x2C00u;
  cpu->gpr[4] = address;
  cpu->hid2 |= PPC_HID2_PSE | PPC_HID2_LSQE;
  mem_write8(cpu, address, 3);
  mem_write8(cpu, address + 1u, 5);
  mem_write8(cpu, address + 2u, 7);
  mem_write8(cpu, address + 3u, 11);
  double begin = seconds();
  for (u32 i = 0; i < iterations; i++) {
    prepare(cpu, 0x80003AA0u);
    if (helpers) {
      cpu->gqr[0] = 0x00040004u;
      ppc_psq_load(cpu, 2, address, false, 0, false, 0x80003AACu);
      ppc_psq_load(cpu, 3, address + 2u, false, 0, false, 0x80003AB0u);
      ppc_ps_madd_op(cpu, 1, 2, 3, 2, false, false);
      ppc_psq_store(cpu, 1, address + 4u, false, 0, false, 0x80003AB8u);
    } else {
      func_80003AA0(cpu);
    }
    checksum +=
        mem_read8(cpu, address + 4u) ^ ((u64)mem_read8(cpu, address + 5u) << 8);
  }
  report(helpers ? "fixed_gqr_ref" : "fixed_gqr", iterations, begin, checksum);
}

int main(int argc, char **argv) {
  u32 iterations = argc > 1 ? (u32)strtoul(argv[1], NULL, 0) : 1000000u;
  const char *selected = argc > 2 ? argv[2] : NULL;
  CPUState cpu;
  if (!cpu_init(&cpu))
    return 1;
  if (!selected || !strcmp(selected, "cr_loop"))
    bench_cr_loop(&cpu, iterations);
  if (!selected || !strcmp(selected, "carry_chain"))
    bench_carry(&cpu, iterations);
  if (!selected || !strcmp(selected, "fastmem"))
    bench_memory(&cpu, iterations);
  if (!selected || !strcmp(selected, "staticmem"))
    bench_static_memory(&cpu, iterations);
  if (!selected || !strcmp(selected, "static_chain"))
    bench_static_memory_chain(&cpu, iterations);
  if (!selected || !strcmp(selected, "call_nomod"))
    bench_direct_call(&cpu, iterations, "call_nomod");
  if (!selected || !strcmp(selected, "call_loaded"))
    bench_direct_call(&cpu, iterations, "call_loaded");
  if (!selected || !strcmp(selected, "call_unrelated"))
    bench_direct_call(&cpu, iterations, "call_unrelated");
  if (!selected || !strcmp(selected, "call_target"))
    bench_direct_call(&cpu, iterations, "call_target");
  if (!selected || !strcmp(selected, "call_chain"))
    bench_call_chain(&cpu, iterations);
  if (!selected || !strcmp(selected, "ps_add"))
    bench_paired_add(&cpu, iterations);
  if (!selected || !strcmp(selected, "ps_chain"))
    bench_paired_chain(&cpu, iterations, false);
  if (!selected || !strcmp(selected, "ps_chain_ref"))
    bench_paired_chain(&cpu, iterations, true);
  if (!selected || !strcmp(selected, "psq_chain"))
    bench_psq_chain(&cpu, iterations);
  if (!selected || !strcmp(selected, "ps_ssa"))
    bench_paired_ssa(&cpu, iterations, false);
  if (!selected || !strcmp(selected, "ps_helpers"))
    bench_paired_ssa(&cpu, iterations, true);
  if (!selected || !strcmp(selected, "ps_fma"))
    bench_paired_fma(&cpu, iterations, false);
  if (!selected || !strcmp(selected, "ps_fma_ref"))
    bench_paired_fma(&cpu, iterations, true);
  if (!selected || !strcmp(selected, "fma_one"))
    bench_fma_single(&cpu, iterations, false);
  if (!selected || !strcmp(selected, "fma_one_ref"))
    bench_fma_single(&cpu, iterations, true);
  if (!selected || !strcmp(selected, "fma_chain"))
    bench_fma_chain(&cpu, iterations, false);
  if (!selected || !strcmp(selected, "fma_chain_ref"))
    bench_fma_chain(&cpu, iterations, true);
  if (!selected || !strcmp(selected, "psq_fma"))
    bench_psq_fma(&cpu, iterations, false);
  if (!selected || !strcmp(selected, "psq_fma_ref"))
    bench_psq_fma(&cpu, iterations, true);
  if (!selected || !strcmp(selected, "fixed_gqr"))
    bench_fixed_gqr_fma(&cpu, iterations, false);
  if (!selected || !strcmp(selected, "fixed_gqr_ref"))
    bench_fixed_gqr_fma(&cpu, iterations, true);
  cpu_free(&cpu);
  return 0;
}
