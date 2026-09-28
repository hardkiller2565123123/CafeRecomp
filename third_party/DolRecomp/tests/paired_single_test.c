#include "cpu/cpu.h"

#include <stdio.h>
#include <string.h>

void func_80003920(CPUState *cpu);
void func_80003940(CPUState *cpu);
void func_80003980(CPUState *cpu);
void func_800039C0(CPUState *cpu);
void func_80003A00(CPUState *cpu);
void func_80003A20(CPUState *cpu);
void func_80003A60(CPUState *cpu);
void func_80003AA0(CPUState *cpu);

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x);    \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static u64 random_state = 0x6A09E667F3BCC909ull;

static u64 random_u64(void) {
  random_state ^= random_state << 13;
  random_state ^= random_state >> 7;
  random_state ^= random_state << 17;
  return random_state;
}

static f64 from_bits(u64 bits) {
  f64 value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

static u64 to_bits(f64 value) {
  u64 bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static f64 widen_f32(u32 bits) {
  f32 value;
  memcpy(&value, &bits, sizeof(value));
  return (f64)value;
}

static void set_pair(CPUState *cpu, u32 reg, u64 lane0, u64 lane1) {
  cpu->fpr[reg] = from_bits(lane0);
  cpu->ps1[reg] = from_bits(lane1);
}

static void prepare(CPUState *cpu, u32 pc, u32 fpscr) {
  cpu->pc = pc;
  cpu->lr = 0x81234567u;
  cpu->msr = 1u << 13;
  cpu->fpscr = fpscr;
  cpu->exception = 0;
  ppc_fpscr_control_updated(cpu);
}

static int same_pair(const CPUState *left, const CPUState *right, u32 reg) {
  return to_bits(left->fpr[reg]) == to_bits(right->fpr[reg]) &&
         to_bits(left->ps1[reg]) == to_bits(right->ps1[reg]);
}

static int check_chain(CPUState *generated, CPUState *reference,
                       const u64 values[10], u32 fpscr, u32 iteration) {
  static const u8 regs[] = {2, 3, 5, 7, 9};
  for (u32 i = 0; i < 5; i++) {
    set_pair(generated, regs[i], values[i * 2], values[i * 2 + 1]);
    set_pair(reference, regs[i], values[i * 2], values[i * 2 + 1]);
  }
  prepare(generated, 0x80003920u, fpscr);
  func_80003920(generated);

  prepare(reference, 0x80003920u, fpscr);
  ppc_ps_add_op(reference, 1, 2, 3);
  ppc_ps_mul_op(reference, 4, 1, 5);
  ppc_ps_add_op(reference, 6, 4, 7);
  ppc_ps_sub_op(reference, 8, 6, 9);
  if (!same_pair(generated, reference, 1) ||
      !same_pair(generated, reference, 4) ||
      !same_pair(generated, reference, 6) ||
      !same_pair(generated, reference, 8) ||
      generated->fpscr != reference->fpscr) {
    fprintf(stderr, "paired chain mismatch iteration=%u fpscr=0x%08X\n",
            iteration, fpscr);
    for (u32 reg = 1; reg <= 8; reg++) {
      if (reg == 2 || reg == 3 || reg == 5 || reg == 7)
        continue;
      fprintf(stderr,
              "  f%u generated=%016llX/%016llX reference=%016llX/%016llX\n",
              reg, (unsigned long long)to_bits(generated->fpr[reg]),
              (unsigned long long)to_bits(generated->ps1[reg]),
              (unsigned long long)to_bits(reference->fpr[reg]),
              (unsigned long long)to_bits(reference->ps1[reg]));
    }
    fprintf(stderr, "  fpscr generated=%08X reference=%08X\n", generated->fpscr,
            reference->fpscr);
    return 0;
  }
  return 1;
}

static int check_fma(CPUState *generated, CPUState *reference,
                     const u64 values[6], u32 fpscr, u32 iteration) {
  set_pair(generated, 2, values[0], values[1]);
  set_pair(generated, 3, values[2], values[3]);
  set_pair(generated, 4, values[4], values[5]);
  set_pair(reference, 2, values[0], values[1]);
  set_pair(reference, 3, values[2], values[3]);
  set_pair(reference, 4, values[4], values[5]);
  prepare(generated, 0x80003980u, fpscr);
  func_80003980(generated);

  prepare(reference, 0x80003980u, fpscr);
  ppc_ps_madd_op(reference, 10, 2, 3, 4, false, false);
  ppc_ps_madd_op(reference, 11, 2, 3, 4, true, false);
  ppc_ps_madd_op(reference, 12, 2, 3, 4, false, true);
  ppc_ps_madd_op(reference, 13, 2, 3, 4, true, true);
  ppc_ps_madds0(reference, 14, 2, 3, 4);
  ppc_ps_madds1(reference, 15, 2, 3, 4);
  for (u32 reg = 10; reg <= 15; reg++) {
    if (!same_pair(generated, reference, reg)) {
      fprintf(stderr, "paired FMA mismatch iteration=%u fpscr=0x%08X f%u\n",
              iteration, fpscr, reg);
      return 0;
    }
  }
  if (generated->fpscr != reference->fpscr) {
    fprintf(stderr, "paired FMA FPSCR mismatch iteration=%u\n", iteration);
    return 0;
  }
  return 1;
}

static int check_ssa_chain(CPUState *generated, CPUState *reference,
                           const u64 values[4], u32 fpscr, u32 iteration) {
  set_pair(generated, 2, values[0], values[1]);
  set_pair(generated, 3, values[2], values[3]);
  set_pair(reference, 2, values[0], values[1]);
  set_pair(reference, 3, values[2], values[3]);
  prepare(generated, 0x800039C0u, fpscr);
  func_800039C0(generated);

  prepare(reference, 0x800039C0u, fpscr);
  ppc_ps_add_op(reference, 1, 2, 3);
  ppc_ps_mul_op(reference, 4, 1, 1);
  ppc_ps_add_op(reference, 6, 4, 1);
  ppc_ps_sub_op(reference, 8, 6, 4);
  if (!same_pair(generated, reference, 1) ||
      !same_pair(generated, reference, 4) ||
      !same_pair(generated, reference, 6) ||
      !same_pair(generated, reference, 8) ||
      generated->fpscr != reference->fpscr) {
    fprintf(stderr, "paired SSA chain mismatch iteration=%u fpscr=0x%08X\n",
            iteration, fpscr);
    return 0;
  }
  return 1;
}

static int check_fma_chain(CPUState *generated, CPUState *reference,
                           const u64 values[4], u32 fpscr, u32 iteration) {
  set_pair(generated, 2, values[0], values[1]);
  set_pair(generated, 3, values[2], values[3]);
  set_pair(reference, 2, values[0], values[1]);
  set_pair(reference, 3, values[2], values[3]);
  prepare(generated, 0x80003A20u, fpscr);
  func_80003A20(generated);
  prepare(reference, 0x80003A20u, fpscr);
  ppc_ps_add_op(reference, 1, 2, 3);
  ppc_ps_madd_op(reference, 4, 1, 1, 1, false, false);
  ppc_ps_madd_op(reference, 5, 4, 1, 1, true, false);
  ppc_ps_madd_op(reference, 6, 5, 1, 1, false, true);
  ppc_ps_madd_op(reference, 7, 6, 1, 1, true, true);
  ppc_ps_madds0(reference, 8, 7, 1, 1);
  ppc_ps_madds1(reference, 9, 8, 1, 1);
  for (u32 reg = 1; reg <= 9; reg++) {
    if (reg == 2 || reg == 3)
      continue;
    if (!same_pair(generated, reference, reg)) {
      fprintf(stderr,
              "paired FMA chain mismatch iteration=%u fpscr=0x%08X f%u\n",
              iteration, fpscr, reg);
      return 0;
    }
  }
  return generated->fpscr == reference->fpscr;
}

static int check_psq_fma(CPUState *generated, CPUState *reference,
                         const u32 values[4], u32 fpscr, u32 iteration) {
  const u32 address = GC_RAM_BASE + 0x2480u;
  CPUState *states[] = {generated, reference};
  for (u32 state = 0; state < 2; state++) {
    prepare(states[state], 0x80003A60u, fpscr);
    states[state]->gpr[4] = address;
    states[state]->gqr[0] = 0;
    states[state]->hid2 |= PPC_HID2_PSE | PPC_HID2_LSQE;
    for (u32 lane = 0; lane < 4; lane++)
      mem_write32(states[state], address + lane * 4u, values[lane]);
  }
  func_80003A60(generated);
  if (!ppc_psq_load(reference, 2, address, false, 0, false, 0x80003A60u) ||
      !ppc_psq_load(reference, 3, address + 8u, false, 0, false, 0x80003A64u))
    return 0;
  ppc_ps_madd_op(reference, 1, 2, 3, 2, false, false);
  if (!ppc_psq_store(reference, 1, address + 16u, false, 0, false, 0x80003A6Cu))
    return 0;
  if (mem_read32(generated, address + 16u) !=
          mem_read32(reference, address + 16u) ||
      mem_read32(generated, address + 20u) !=
          mem_read32(reference, address + 20u) ||
      !same_pair(generated, reference, 1) ||
      generated->fpscr != reference->fpscr) {
    fprintf(stderr, "PSQ FMA mismatch iteration=%u fpscr=0x%08X\n", iteration,
            fpscr);
    fprintf(stderr, "  input=%08X/%08X %08X/%08X\n", values[0], values[1],
            values[2], values[3]);
    fprintf(stderr,
            "  f1 generated=%016llX/%016llX reference=%016llX/%016llX\n",
            (unsigned long long)to_bits(generated->fpr[1]),
            (unsigned long long)to_bits(generated->ps1[1]),
            (unsigned long long)to_bits(reference->fpr[1]),
            (unsigned long long)to_bits(reference->ps1[1]));
    fprintf(stderr, "  memory generated=%08X/%08X reference=%08X/%08X\n",
            mem_read32(generated, address + 16u),
            mem_read32(generated, address + 20u),
            mem_read32(reference, address + 16u),
            mem_read32(reference, address + 20u));
    return 0;
  }
  return 1;
}

static int check_fixed_gqr_fma(CPUState *generated, CPUState *reference,
                               const u8 values[4], u32 fpscr, u32 iteration) {
  const u32 address = GC_RAM_BASE + 0x24C0u;
  CPUState *states[] = {generated, reference};
  for (u32 state = 0; state < 2; state++) {
    prepare(states[state], 0x80003AA0u, fpscr);
    states[state]->gpr[4] = address;
    states[state]->hid2 |= PPC_HID2_PSE | PPC_HID2_LSQE;
    for (u32 lane = 0; lane < 4; lane++)
      mem_write8(states[state], address + lane, values[lane]);
  }
  func_80003AA0(generated);
  reference->gqr[0] = 0x00040004u;
  if (!ppc_psq_load(reference, 2, address, false, 0, false, 0x80003AACu) ||
      !ppc_psq_load(reference, 3, address + 2u, false, 0, false, 0x80003AB0u))
    return 0;
  ppc_ps_madd_op(reference, 1, 2, 3, 2, false, false);
  if (!ppc_psq_store(reference, 1, address + 4u, false, 0, false, 0x80003AB8u))
    return 0;
  if (mem_read8(generated, address + 4u) !=
          mem_read8(reference, address + 4u) ||
      mem_read8(generated, address + 5u) !=
          mem_read8(reference, address + 5u) ||
      !same_pair(generated, reference, 1) ||
      generated->fpscr != reference->fpscr) {
    fprintf(stderr, "fixed GQR FMA mismatch iteration=%u fpscr=0x%08X\n",
            iteration, fpscr);
    return 0;
  }
  return 1;
}

static int check_psq_chain(CPUState *generated, CPUState *reference,
                           const u32 values[6], u32 fpscr, u32 iteration) {
  const u32 address = GC_RAM_BASE + 0x2400u;
  CPUState *states[] = {generated, reference};
  for (u32 state = 0; state < 2; state++) {
    prepare(states[state], 0x80003940u, fpscr);
    states[state]->gpr[4] = address;
    states[state]->gqr[0] = 0;
    states[state]->hid2 |= PPC_HID2_PSE | PPC_HID2_LSQE;
    states[state]->fpr[5] = widen_f32(values[4]);
    states[state]->ps1[5] = widen_f32(values[5]);
    for (u32 lane = 0; lane < 4; lane++)
      mem_write32(states[state], address + lane * 4u, values[lane]);
  }

  func_80003940(generated);
  if (!ppc_psq_load(reference, 2, address, false, 0, false, 0x80003940u) ||
      !ppc_psq_load(reference, 3, address + 8u, false, 0, false, 0x80003944u))
    return 0;
  ppc_ps_mul_op(reference, 1, 2, 3);
  ppc_ps_add_op(reference, 1, 1, 5);
  if (!ppc_psq_store(reference, 1, address + 16u, false, 0, false, 0x80003950u))
    return 0;

  if (mem_read32(generated, address + 16u) !=
          mem_read32(reference, address + 16u) ||
      mem_read32(generated, address + 20u) !=
          mem_read32(reference, address + 20u) ||
      !same_pair(generated, reference, 1) ||
      generated->fpscr != reference->fpscr) {
    fprintf(stderr, "PSQ chain mismatch iteration=%u fpscr=0x%08X\n", iteration,
            fpscr);
    fprintf(stderr, "  input=%08X/%08X %08X/%08X add=%08X/%08X\n", values[0],
            values[1], values[2], values[3], values[4], values[5]);
    fprintf(stderr, "  output generated=%08X/%08X reference=%08X/%08X\n",
            mem_read32(generated, address + 16u),
            mem_read32(generated, address + 20u),
            mem_read32(reference, address + 16u),
            mem_read32(reference, address + 20u));
    fprintf(stderr,
            "  f1 generated=%016llX/%016llX reference=%016llX/%016llX\n",
            (unsigned long long)to_bits(generated->fpr[1]),
            (unsigned long long)to_bits(generated->ps1[1]),
            (unsigned long long)to_bits(reference->fpr[1]),
            (unsigned long long)to_bits(reference->ps1[1]));
    return 0;
  }
  return 1;
}

int main(void) {
  static const u64 edge[] = {
      0x0000000000000000ull, 0x8000000000000000ull, 0x3FF0000000000000ull,
      0xBFF0000000000000ull, 0x7FF0000000000000ull, 0xFFF0000000000000ull,
      0x7FF8000000000001ull, 0x7FF0000000000001ull, 0x0000000000000001ull,
      0x000FFFFFFFFFFFFFull, 0x0010000000000000ull, 0x7FEFFFFFFFFFFFFFull,
      0x36A0000000000000ull, 0x3810000000000000ull, 0x3FF0000010000000ull,
      0x3FEFFFFFFFFFFFFFull,
  };
  static const u32 edge32[] = {
      0x00000000u, 0x80000000u, 0x3F800000u, 0xBF800000u,
      0x7F800000u, 0xFF800000u, 0x7FC00001u, 0x7F800001u,
      0x00000001u, 0x007FFFFFu, 0x00800000u, 0x7F7FFFFFu,
      0x3F800001u, 0x3F7FFFFFu, 0x4B000001u, 0xCB000001u,
  };
  CPUState generated;
  CPUState reference;
  CHECK(cpu_init(&generated));
  CHECK(cpu_init(&reference));

  const u64 tie_values[] = {
      to_bits(widen_f32(0x42480000u)), to_bits(widen_f32(0x42480000u)),
      to_bits(widen_f32(0xBC88CC38u)), to_bits(widen_f32(0xBC88CC38u)),
      to_bits(widen_f32(0x1B1C72A0u)), to_bits(widen_f32(0x1B1C72A0u)),
  };
  CHECK(check_fma(&generated, &reference, tie_values, 0, 0));
  CHECK(to_bits(generated.fpr[10]) == to_bits(widen_f32(0xBF55BF17u)));

  u32 iteration = 0;
  for (u32 control = 0; control < 8; control++) {
    u32 fpscr = (control & 3u) | ((control & 4u) ? 4u : 0u);
    for (u32 start = 0; start < sizeof(edge) / sizeof(edge[0]); start++) {
      u64 chain[10];
      u64 fma[6];
      u32 psq[6];
      u8 quantized[4];
      for (u32 i = 0; i < 10; i++)
        chain[i] = edge[(start + i * 3u) % (sizeof(edge) / sizeof(edge[0]))];
      for (u32 i = 0; i < 6; i++)
        fma[i] = edge[(start + i * 5u) % (sizeof(edge) / sizeof(edge[0]))];
      for (u32 i = 0; i < 6; i++)
        psq[i] =
            edge32[(start + i * 5u) % (sizeof(edge32) / sizeof(edge32[0]))];
      for (u32 i = 0; i < 4; i++)
        quantized[i] = (u8)psq[i];
      CHECK(check_chain(&generated, &reference, chain, fpscr, iteration));
      CHECK(check_fma(&generated, &reference, fma, fpscr, iteration));
      CHECK(check_ssa_chain(&generated, &reference, chain, fpscr, iteration));
      CHECK(check_psq_chain(&generated, &reference, psq, fpscr, iteration));
      CHECK(check_fma_chain(&generated, &reference, chain, fpscr, iteration));
      CHECK(check_psq_fma(&generated, &reference, psq, fpscr, iteration));
      CHECK(check_fixed_gqr_fma(&generated, &reference, quantized, fpscr,
                                iteration));
      iteration++;
    }
    for (u32 sample = 0; sample < 256; sample++) {
      u64 chain[10];
      u64 fma[6];
      u32 psq[6];
      u8 quantized[4];
      for (u32 i = 0; i < 10; i++)
        chain[i] = random_u64();
      for (u32 i = 0; i < 6; i++)
        fma[i] = random_u64();
      for (u32 i = 0; i < 6; i++)
        psq[i] = (u32)random_u64();
      for (u32 i = 0; i < 4; i++)
        quantized[i] = (u8)psq[i];
      CHECK(check_chain(&generated, &reference, chain, fpscr, iteration));
      CHECK(check_fma(&generated, &reference, fma, fpscr, iteration));
      CHECK(check_ssa_chain(&generated, &reference, chain, fpscr, iteration));
      CHECK(check_psq_chain(&generated, &reference, psq, fpscr, iteration));
      CHECK(check_fma_chain(&generated, &reference, chain, fpscr, iteration));
      CHECK(check_psq_fma(&generated, &reference, psq, fpscr, iteration));
      CHECK(check_fixed_gqr_fma(&generated, &reference, quantized, fpscr,
                                iteration));
      iteration++;
    }
  }

  cpu_free(&reference);
  cpu_free(&generated);
  return 0;
}
