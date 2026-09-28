#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/cpu/cpu.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        failures++; \
    } \
} while (0)

static f64 inline_fmuls(f64 a, f64 c) {
    return (f64)(f32)(a * c);
}

static u32 lcg_state = 0x13579BDFu;
static u32 lcg_next(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

static f64 sample_single(void) {
    u32 bits = (lcg_next() & 0x807FFFFFu) | (((lcg_next() % 40u) + 105u) << 23);
    f32 value;
    memcpy(&value, &bits, sizeof(value));
    return (f64)value;
}

static f64 sample_double(void) {
    u64 bits = ((u64)(lcg_next() & 0x000FFFFFu) << 32) | lcg_next();
    bits |= (u64)((lcg_next() % 40u) + 1002u) << 52;
    f64 value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static u32 count_divergent(CPUState* cpu, f64 (*sample_c)(void), u32 trials) {
    u32 differing = 0;
    for (u32 i = 0; i < trials; ++i) {
        f64 a = sample_single();
        f64 c = sample_c();
        cpu->fpr[1] = a;
        cpu->fpr[2] = c;
        ppc_fmuls(cpu, 0, 1, 2);
        if (cpu->fpr[0] != inline_fmuls(a, c))
            differing++;
    }
    return differing;
}

int main(void) {
    CPUState cpu;
    if (!cpu_init(&cpu))
        return 1;

    const u32 trials = 100000u;
    u32 diff_single = count_divergent(&cpu, sample_single, trials);
    u32 diff_double = count_divergent(&cpu, sample_double, trials);
    printf("25-bit C operand : C from lfs   %u/%u differ (%.1f%%)\n",
           diff_single, trials, 100.0 * (double)diff_single / (double)trials);
    printf("                 : C full f64   %u/%u differ (%.1f%%)\n",
           diff_double, trials, 100.0 * (double)diff_double / (double)trials);
    CHECK(diff_single == 0,
          "a single-representable C operand should survive truncation intact, "
          "got %u/%u differing", diff_single, trials);
    CHECK(diff_double == 12545u,
          "expected 12545/%u divergent full-mantissa products, got %u",
          trials, diff_double);

    const f64 poison = -12345.678;
    cpu.fpr[1] = 3.5;
    cpu.fpr[2] = 1.25;
    cpu.ps1[0] = poison;
    cpu.fpr[0] = inline_fmuls(cpu.fpr[1], cpu.fpr[2]);
    int inline_left_ps1_stale = cpu.ps1[0] == poison;

    cpu.ps1[0] = poison;
    ppc_fmuls(&cpu, 0, 1, 2);
    int helper_wrote_ps1 = cpu.ps1[0] == cpu.fpr[0];

    printf("ps1              : inline leaves it stale=%s, helper writes it=%s\n",
           inline_left_ps1_stale ? "yes" : "no",
           helper_wrote_ps1 ? "yes" : "no");
    CHECK(inline_left_ps1_stale, "the inline form should leave ps1 untouched");
    CHECK(helper_wrote_ps1, "ppc_fmuls should mirror the result into ps1");

    cpu.fpscr = 0;
    cpu.fpr[0] = inline_fmuls(cpu.fpr[1], cpu.fpr[2]);
    u32 fprf_after_inline = (cpu.fpscr >> 12) & 0x1Fu;
    cpu.fpscr = 0;
    ppc_fmuls(&cpu, 0, 1, 2);
    u32 fprf_after_helper = (cpu.fpscr >> 12) & 0x1Fu;
    printf("FPRF             : inline 0x%02X, helper 0x%02X\n",
           fprf_after_inline, fprf_after_helper);
    CHECK(fprf_after_inline == 0, "the inline form should leave FPRF clear");
    CHECK(fprf_after_helper != 0, "ppc_fmuls should classify its result");

    const f64 quiet_nan = (f64)NAN;
    cpu.fpscr = 0;
    ppc_fcmp(&cpu, 0, quiet_nan, 1.0, false);
    u32 vxvc_unordered = cpu.fpscr & 0x00080000u;
    cpu.fpscr = 0;
    ppc_fcmp(&cpu, 0, quiet_nan, 1.0, true);
    u32 vxvc_ordered = cpu.fpscr & 0x00080000u;
    printf("fcmpo vs fcmpu   : VXVC ordered=0x%08X unordered=0x%08X\n",
           vxvc_ordered, vxvc_unordered);
    CHECK(vxvc_unordered == 0, "fcmpu should not signal on a quiet NaN");
    CHECK(vxvc_ordered != 0, "fcmpo should signal on a quiet NaN");

    cpu.fpr[2] = 3.0;
    cpu.fpr[3] = 4.0;
    cpu.ps1[1] = poison;
    ppc_fmuls(&cpu, 1, 2, 3);
    cpu.fpr[4] = 1.0;
    cpu.ps1[4] = 10.0;
    ppc_ps_add_op(&cpu, 5, 1, 4);

    printf("ps_* consumer    : ps0 %g (want 13), ps1 %g (want 22)\n",
           cpu.fpr[5], cpu.ps1[5]);
    CHECK(cpu.fpr[5] == 13.0, "ps_add ps0 lane got %g", cpu.fpr[5]);
    CHECK(cpu.ps1[5] == 22.0,
          "ps_add ps1 lane got %g -- fmuls left ps1 stale and this is where "
          "that surfaces", cpu.ps1[5]);

    cpu.ps1[1] = poison;
    cpu.fpr[1] = inline_fmuls(cpu.fpr[2], cpu.fpr[3]);
    ppc_ps_add_op(&cpu, 6, 1, 4);
    CHECK(cpu.fpr[6] == 13.0, "inline chain ps0 lane should still be right");
    CHECK(cpu.ps1[6] != 22.0,
          "inline chain should corrupt only the ps1 lane, got %g", cpu.ps1[6]);

    cpu_free(&cpu);
    return failures != 0;
}
