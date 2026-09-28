#include "wiiu_imports.h"
#include "wiiu_filesystem.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "lookup failed: %s\n", #x); return 1; } } while (0)

static const RPXSymbol* reference_symbol(const RPXFile* rpx, u32 address) {
    const RPXSymbol* ranged = NULL;
    for (u32 i = 0; i < rpx->symbol_count; ++i) {
        const RPXSymbol* sym = &rpx->symbols[i];
        if (sym->value == address) return sym;
        if (!ranged && sym->size && address >= sym->value &&
            address - sym->value < sym->size) ranged = sym;
    }
    return ranged;
}

int main(void) {
    static RPXFile rpx;
    rpx.symbol_count = 4;
    rpx.symbols[0].value = 0x1000; rpx.symbols[0].size = 0x100;
    rpx.symbols[1].value = 0x1010; rpx.symbols[1].size = 0x10;
    rpx.symbols[2].value = 0x1010; /* First exact match wins. */
    rpx.symbols[3].value = 0x2000;
    wiiu_imports_attach_rpx(&rpx);
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        CHECK(wiiu_imports_find_symbol(0x1010) == &rpx.symbols[1]);
        CHECK(wiiu_imports_find_symbol(0x1011) == &rpx.symbols[0]);
        CHECK(wiiu_imports_find_symbol(0x1100) == NULL);
        CHECK(wiiu_imports_find_symbol(0x2000) == &rpx.symbols[3]);
        CHECK(wiiu_imports_find_symbol(0) == NULL);
    }
    /* A colliding cached miss must not replace the meaning of an address. */
    CHECK(wiiu_imports_find_symbol(0x11014) == NULL);
    CHECK(wiiu_imports_find_symbol(0x1010) == &rpx.symbols[1]);
    rpx.symbols[1].value = rpx.symbols[2].value = 0x3000;
    wiiu_imports_attach_rpx(&rpx); /* Even the same RPX object invalidates. */
    CHECK(wiiu_imports_find_symbol(0x1010) == &rpx.symbols[0]);
    CHECK(wiiu_imports_find_symbol(0x3000) == &rpx.symbols[1]);
    /* Coreinit dispatch must not perform AOC disk I/O on its hot path. */
    rpx.symbol_count = 2;
    strcpy(rpx.symbols[0].name, "OSGetCurrentThread");
    rpx.symbols[0].value = 0x0434D000u;
    rpx.symbols[0].size = 0;
    rpx.symbols[0].section_index = RPX_SYMBOL_SECTION_ALIAS;
    strcpy(rpx.symbols[1].name, "AOC_ListTitle");
    rpx.symbols[1].value = 0x0434D008u;
    rpx.symbols[1].size = 0;
    rpx.symbols[1].section_index = RPX_SYMBOL_SECTION_ALIAS;
    wiiu_imports_attach_rpx(&rpx);
    CPUState cpu = {0};
    cpu.lr = 0x02001000u;
    u64 probes = wiiu_filesystem_dlc_probe_count();
    clock_t start = clock();
    for (unsigned i = 0; i < 50000; ++i) {
        cpu.pc = rpx.symbols[0].value;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
        CHECK(cpu.pc == cpu.lr && cpu.gpr[3] == WIIU_GUEST_THREAD);
    }
    printf("50000 coreinit calls: %.1f ms, DLC disk probes=%llu\n",
           1000.0 * (clock() - start) / CLOCKS_PER_SEC,
           (unsigned long long)(wiiu_filesystem_dlc_probe_count() - probes));
    CHECK(wiiu_filesystem_dlc_probe_count() == probes);
    /* Real AOC enumeration still observes the filesystem, not a stale cache. */
    cpu.pc = rpx.symbols[1].value;
    cpu.gpr[3] = cpu.gpr[4] = cpu.gpr[5] = 0;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    CHECK(cpu.pc == cpu.lr && cpu.gpr[3] == 0);
    CHECK(wiiu_filesystem_dlc_probe_count() == probes + 1);
    /* Match the real import-table size; uncached ordinary PCs are the common
       path during loading. Timing is diagnostic, never a flaky test limit. */
    memset(&rpx, 0, sizeof(rpx));
    rpx.symbol_count = 921;
    for (u32 i = 0; i < rpx.symbol_count; ++i)
        rpx.symbols[i].value = 0xC0000000u + i * 8u;
    wiiu_imports_attach_rpx(&rpx);
    start = clock();
    for (u32 i = 0; i < 50000; ++i)
        CHECK(wiiu_imports_find_symbol(0x03000000u + i * 4u) == NULL);
    printf("50000 uncached game PCs / 921 symbols: %.1f ms\n",
           1000.0 * (clock() - start) / CLOCKS_PER_SEC);

    /* Cross-page ranges, top-of-address-space saturation, overlapping exact
       entries and same-object reattachment retain the original semantics. */
    rpx.symbol_count = 6;
    rpx.symbols[0].value = 0x0000FFF0u; rpx.symbols[0].size = 0x20030u;
    rpx.symbols[1].value = 0xFFFFFFFFu; rpx.symbols[1].size = 0xFFFFFFFFu;
    rpx.symbols[2].value = 0xFFFFFFF0u; rpx.symbols[2].size = 0x100u;
    rpx.symbols[3].value = 0x00010000u; rpx.symbols[3].size = 1;
    rpx.symbols[4] = rpx.symbols[3];
    rpx.symbols[5].value = 0; rpx.symbols[5].size = 0;
    wiiu_imports_attach_rpx(&rpx);
    const u32 edges[] = {0, 1, 0xFFF0, 0xFFFF, 0x10000, 0x10001,
                        0x3001F, 0x30020, 0xFFFFFFEF, 0xFFFFFFF0, 0xFFFFFFFF};
    for (u32 i = 0; i < sizeof(edges)/sizeof(edges[0]); ++i)
        CHECK(wiiu_imports_find_symbol(edges[i]) == reference_symbol(&rpx, edges[i]));
    u32 seed = 1;
    for (u32 i = 0; i < 10000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        CHECK(wiiu_imports_find_symbol(seed) == reference_symbol(&rpx, seed));
    }
    wiiu_imports_attach_rpx(NULL);
    CHECK(wiiu_imports_find_symbol(0x3000) == NULL);
    puts("Import lookup cache preserves exact, range, missing and reattach results");
    return 0;
}
