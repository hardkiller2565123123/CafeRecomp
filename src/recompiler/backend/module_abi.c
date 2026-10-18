#include "backend/module_abi.h"

#include <string.h>
#include <stddef.h>

_Static_assert(offsetof(CPUState, gpr) == 0, "CPUState gpr ABI changed");
_Static_assert(offsetof(CPUState, fpr) == 128, "CPUState fpr ABI changed");
_Static_assert(offsetof(CPUState, ps1) == 384, "CPUState ps1 ABI changed");
_Static_assert(offsetof(CPUState, pc) == 640, "CPUState pc ABI changed");
_Static_assert(offsetof(CPUState, lr) == 644, "CPUState lr ABI changed");

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__amd64__)
#include <cpuid.h>
#endif

#if defined(__aarch64__) && defined(__linux__)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

static unsigned feature_score(u64 features) {
    unsigned count = 0;
    while (features) {
        features &= features - 1u;
        count++;
    }
    return count;
}

u64 dolrecomp_detect_host_features(void) {
#if defined(_MSC_VER) && defined(_M_X64)
    int leaf1[4];
    int leaf7[4];
    __cpuid(leaf1, 1);
    __cpuidex(leaf7, 7, 0);
    u64 result = DOLRECOMP_FEATURE_X86_64;
    if (leaf1[2] & (1 << 0)) result |= DOLRECOMP_FEATURE_SSE3;
    if (leaf1[2] & (1 << 9)) result |= DOLRECOMP_FEATURE_SSSE3;
    if (leaf1[2] & (1 << 13)) result |= DOLRECOMP_FEATURE_CX16;
    if (leaf1[2] & (1 << 19)) result |= DOLRECOMP_FEATURE_SSE41;
    if (leaf1[2] & (1 << 20)) result |= DOLRECOMP_FEATURE_SSE42;
    if (leaf1[2] & (1 << 22)) result |= DOLRECOMP_FEATURE_MOVBE;
    if (leaf1[2] & (1 << 23)) result |= DOLRECOMP_FEATURE_POPCNT;
    int extended[4];
    __cpuid(extended, 0x80000001);
    if (extended[2] & (1 << 0)) result |= DOLRECOMP_FEATURE_LAHF;
    if (extended[2] & (1 << 5)) result |= DOLRECOMP_FEATURE_LZCNT;
    int avx_os = (leaf1[2] & (1 << 27)) &&
                 ((_xgetbv(0) & 6u) == 6u);
    if (avx_os && (leaf1[2] & (1 << 28))) result |= DOLRECOMP_FEATURE_AVX;
    if (avx_os && (leaf1[2] & (1 << 12))) result |= DOLRECOMP_FEATURE_FMA;
    if (avx_os && (leaf7[1] & (1 << 5))) result |= DOLRECOMP_FEATURE_AVX2;
    if (leaf7[1] & (1 << 3)) result |= DOLRECOMP_FEATURE_BMI1;
    if (leaf7[1] & (1 << 8)) result |= DOLRECOMP_FEATURE_BMI2;
    return result;
#elif defined(__x86_64__) || defined(__amd64__)
    unsigned a, b, c, d;
    u64 result = DOLRECOMP_FEATURE_X86_64;
    if (__get_cpuid(1, &a, &b, &c, &d)) {
        if (c & bit_SSE3) result |= DOLRECOMP_FEATURE_SSE3;
        if (c & bit_SSSE3) result |= DOLRECOMP_FEATURE_SSSE3;
        if (c & bit_CMPXCHG16B) result |= DOLRECOMP_FEATURE_CX16;
        if (c & bit_SSE4_1) result |= DOLRECOMP_FEATURE_SSE41;
        if (c & bit_SSE4_2) result |= DOLRECOMP_FEATURE_SSE42;
        if (c & bit_MOVBE) result |= DOLRECOMP_FEATURE_MOVBE;
        if (c & bit_POPCNT) result |= DOLRECOMP_FEATURE_POPCNT;
        unsigned xcr0_low;
        unsigned xcr0_high;
        if ((c & bit_OSXSAVE) != 0)
            __asm__ volatile("xgetbv" : "=a"(xcr0_low), "=d"(xcr0_high)
                             : "c"(0));
        else
            xcr0_low = xcr0_high = 0;
        (void)xcr0_high;
        if ((xcr0_low & 6u) == 6u && (c & bit_AVX))
            result |= DOLRECOMP_FEATURE_AVX;
        if ((xcr0_low & 6u) == 6u && (c & bit_FMA))
            result |= DOLRECOMP_FEATURE_FMA;
    }
    if (__get_cpuid_count(7, 0, &a, &b, &c, &d)) {
        if ((result & DOLRECOMP_FEATURE_AVX) && (b & bit_AVX2))
            result |= DOLRECOMP_FEATURE_AVX2;
        if (b & bit_BMI) result |= DOLRECOMP_FEATURE_BMI1;
        if (b & bit_BMI2) result |= DOLRECOMP_FEATURE_BMI2;
    }
    if (__get_cpuid(0x80000001u, &a, &b, &c, &d)) {
        if (c & bit_LAHF_LM) result |= DOLRECOMP_FEATURE_LAHF;
        if (c & bit_LZCNT) result |= DOLRECOMP_FEATURE_LZCNT;
    }
    return result;
#elif defined(__aarch64__) || defined(_M_ARM64)
    u64 result = DOLRECOMP_FEATURE_AARCH64;
#if defined(__linux__) && defined(HWCAP_CRC32)
    if (getauxval(AT_HWCAP) & HWCAP_CRC32)
        result |= DOLRECOMP_FEATURE_ARM_CRC;
#elif defined(__SWITCH__) || defined(__ARM_FEATURE_CRC32)
    result |= DOLRECOMP_FEATURE_ARM_CRC;
#endif
    return result;
#else
    return 0;
#endif
}

int dolrecomp_select_module(const DolRecompModule* module, u64 host_features,
                            DolRecompSemanticMode semantics,
                            DolRecompLoadedModule* loaded, FILE* diagnostics) {
    if (!module || !loaded || module->cpu_state_size != sizeof(CPUState)) {
        if (diagnostics)
            fprintf(diagnostics, "dolrecomp: incompatible CPUState layout\n");
        return 0;
    }
    memset(loaded, 0, sizeof(*loaded));
    loaded->host_features = host_features;
    loaded->semantics = semantics;
    if (module->abi_version == DOLRECOMP_MODULE_ABI_V3) {
        loaded->dispatch = module->legacy_dispatch;
        loaded->variant_name = "legacy-v3";
        return loaded->dispatch != NULL;
    }
    if (module->abi_version != DOLRECOMP_MODULE_ABI_V4) {
        if (diagnostics)
            fprintf(diagnostics, "dolrecomp: unsupported module ABI v%u\n",
                    module->abi_version);
        return 0;
    }
    unsigned best_score = 0;
    for (u32 i = 0; i < module->variant_count; i++) {
        const DolRecompVariantV4* variant = &module->variants[i];
        if (!variant->dispatch || variant->semantics != semantics ||
            (variant->required_features & host_features) !=
                variant->required_features)
            continue;
        unsigned score = feature_score(variant->required_features);
        if (!loaded->dispatch || score > best_score) {
            loaded->dispatch = variant->dispatch;
            loaded->variant_name = variant->name;
            best_score = score;
        }
    }
    if (!loaded->dispatch && diagnostics)
        fprintf(diagnostics,
                "dolrecomp: no compatible %s module variant (features=%llx)\n",
                semantics == DOLRECOMP_SEMANTICS_EXACT ? "exact" : "fast",
                (unsigned long long)host_features);
    return loaded->dispatch != NULL;
}

int dolrecomp_initialize_module(const DolRecompModule* module,
                                DolRecompSemanticMode semantics,
                                DolRecompLoadedModule* loaded,
                                FILE* diagnostics) {
    return dolrecomp_select_module(module, dolrecomp_detect_host_features(),
                                   semantics, loaded, diagnostics);
}
