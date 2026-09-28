#include "backend/module_abi.h"

#include <stddef.h>
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #x); return 1; } } while (0)

static int v2(CPUState* state, u32 blocks) {
    (void)state;
    return (int)blocks + 2;
}

static int v3(CPUState* state, u32 blocks) {
    (void)state;
    return (int)blocks + 3;
}

static int arm(CPUState* state, u32 blocks) {
    (void)state;
    return (int)blocks + 4;
}

int main(void) {
#if UINTPTR_MAX == UINT64_MAX
    CHECK(sizeof(CPUState) == 3536u);
    CHECK(offsetof(CPUState, downcount) == 3480u);
    CHECK(offsetof(CPUState, cycle_budget) == 3488u);
    CHECK(offsetof(CPUState, exram) == 3496u);
    CHECK(offsetof(CPUState, exram_size) == 3504u);
    CHECK(offsetof(CPUState, spr_read) == 3512u);
    CHECK(offsetof(CPUState, spr_write) == 3520u);
    CHECK(offsetof(CPUState, cache_control) == 3528u);
#endif
    const DolRecompVariantV4 variants[] = {
        {DOLRECOMP_FEATURE_X86_V2, DOLRECOMP_SEMANTICS_EXACT, v2, "v2"},
        {DOLRECOMP_FEATURE_X86_V3, DOLRECOMP_SEMANTICS_EXACT, v3, "v3"},
        {DOLRECOMP_FEATURE_ARMV8, DOLRECOMP_SEMANTICS_EXACT, arm, "arm"},
        {DOLRECOMP_FEATURE_CORTEX_A57, DOLRECOMP_SEMANTICS_EXACT, v3, "a57"},
    };
    DolRecompModule module = {
        DOLRECOMP_MODULE_ABI_V4, sizeof(CPUState), 4, variants, NULL,
    };
    DolRecompLoadedModule loaded;
    CHECK(dolrecomp_select_module(&module, DOLRECOMP_FEATURE_X86_V3,
                                  DOLRECOMP_SEMANTICS_EXACT, &loaded, stderr));
    CHECK(loaded.dispatch == v3);
    CHECK(dolrecomp_select_module(&module, DOLRECOMP_FEATURE_X86_V2,
                                  DOLRECOMP_SEMANTICS_EXACT, &loaded, stderr));
    CHECK(loaded.dispatch == v2);
    CPUState state = {0};
    CHECK(dolrecomp_run_loaded(&loaded, &state, 7) == 9);
    CHECK(dolrecomp_select_module(&module, DOLRECOMP_FEATURE_ARMV8,
                                  DOLRECOMP_SEMANTICS_EXACT, &loaded, stderr));
    CHECK(loaded.dispatch == arm);
    CHECK(dolrecomp_select_module(&module, DOLRECOMP_FEATURE_CORTEX_A57,
                                  DOLRECOMP_SEMANTICS_EXACT, &loaded, stderr));
    CHECK(loaded.dispatch == v3);
    CHECK(!dolrecomp_select_module(&module, DOLRECOMP_FEATURE_X86_V3,
                                   DOLRECOMP_SEMANTICS_FAST, &loaded, NULL));
    CHECK(!dolrecomp_select_module(&module, 0, DOLRECOMP_SEMANTICS_EXACT,
                                   &loaded, NULL));
    module.abi_version = DOLRECOMP_MODULE_ABI_V3;
    module.legacy_dispatch = v2;
    CHECK(dolrecomp_select_module(&module, 0, DOLRECOMP_SEMANTICS_EXACT,
                                  &loaded, stderr));
    CHECK(loaded.dispatch == v2);
    return 0;
}
