#include <stdio.h>

#include "../src/backend/c_cfg.h"
#include "../src/frontend/decoder.h"

#define BASE 0x80004000u

static int failures;

static void check(bool condition, const char* message) {
    if (condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    failures++;
}

static void test_integer_loop(void) {
    static const u32 words[] = {
        0x3863FFFFu,
        0x2C030000u,
        0x4082FFF8u,
        0x4E800020u,
    };
    PPCInst insts[sizeof(words) / sizeof(words[0])];
    for (u32 i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        insts[i] = ppc_decode(words[i], BASE + i * 4u);

    CFunctionCFG cfg;
    check(c_function_cfg_build(&cfg, insts, 4, BASE), "build integer loop CFG");
    check(insts[2].op == PPC_OP_BC && insts[2].branch_target == BASE,
          "decode integer loop backedge");
    check(cfg.leaders[0] && cfg.leaders[3], "discover loop blocks");
    check(cfg.block_cycles[0] == 3 && cfg.block_cycles[3] == 1,
          "charge loop blocks");
    check(c_function_cfg_can_loop_directly(&cfg, insts, BASE, 2),
          "keep integer backedge native");
    check(!cfg.materialize_pc[0] && !cfg.materialize_pc[1] &&
              !cfg.materialize_pc[2] && cfg.materialize_pc[3],
          "elide PC stores inside pure integer loop");
    check(cfg.loop_ends[0] == 2, "extract straight-line integer loop");
    c_function_cfg_destroy(&cfg);
}

static void test_timebase_loop(void) {
    static const u32 words[] = {
        0x7C6C42E6u,
        0x4BFFFFFCu,
    };
    PPCInst insts[sizeof(words) / sizeof(words[0])];
    for (u32 i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        insts[i] = ppc_decode(words[i], BASE + i * 4u);

    CFunctionCFG cfg;
    check(c_function_cfg_build(&cfg, insts, 2, BASE), "build timebase loop CFG");
    check(insts[0].op == PPC_OP_MFTB, "decode timebase read");
    check(!c_function_cfg_can_loop_directly(&cfg, insts, BASE, 1),
          "yield before repeating a timebase read");
    check(cfg.materialize_pc[0] && cfg.materialize_pc[1],
          "materialize PC around timebase loop");
    c_function_cfg_destroy(&cfg);
}

static void test_local_call(void) {
    static const u32 words[] = {
        0x48000009u,
        0x60000000u,
        0x4E800020u,
    };
    PPCInst insts[sizeof(words) / sizeof(words[0])];
    for (u32 i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        insts[i] = ppc_decode(words[i], BASE + i * 4u);

    CFunctionCFG cfg;
    check(c_function_cfg_build(&cfg, insts, 3, BASE), "build local call CFG");
    check(insts[0].op == PPC_OP_B && insts[0].lk &&
              insts[0].branch_target == BASE + 8,
          "decode local call");
    check(cfg.return_targets[1], "record local call continuation");
    c_function_cfg_destroy(&cfg);
}

static void test_memory_loop(void) {
    static const u32 words[] = {
        0x80850000u,
        0x38A50004u,
        0x3863FFFFu,
        0x2C030000u,
        0x4082FFF0u,
        0x4E800020u,
    };
    PPCInst insts[sizeof(words) / sizeof(words[0])];
    for (u32 i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        insts[i] = ppc_decode(words[i], BASE + i * 4u);

    CFunctionCFG cfg;
    check(c_function_cfg_build(&cfg, insts, 6, BASE), "build memory loop CFG");
    check(cfg.loop_ends[0] == 4, "extract ordinary RAM loop");
    check(cfg.materialize_pc[0] && !cfg.materialize_pc[1] &&
              !cfg.materialize_pc[2] && !cfg.materialize_pc[3] &&
              !cfg.materialize_pc[4],
          "materialize PC only for memory slow path");
    c_function_cfg_destroy(&cfg);
}

int main(void) {
    test_integer_loop();
    test_timebase_loop();
    test_local_call();
    test_memory_loop();
    return failures != 0;
}
