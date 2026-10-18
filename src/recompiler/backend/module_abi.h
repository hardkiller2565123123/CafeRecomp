#ifndef DOLRECOMP_MODULE_ABI_H
#define DOLRECOMP_MODULE_ABI_H

#include "cpu/cpu.h"
#include <stdio.h>

#ifndef DOLRECOMP_MODULE_ABI_TYPES
#define DOLRECOMP_MODULE_ABI_TYPES

#define DOLRECOMP_MODULE_ABI_V3 3u
#define DOLRECOMP_MODULE_ABI_V4 4u
#define DOLRECOMP_CPUSTATE_ABI_VERSION 1u

typedef enum {
    DOLRECOMP_SEMANTICS_EXACT,
    DOLRECOMP_SEMANTICS_FAST,
} DolRecompSemanticMode;

#define DOLRECOMP_FEATURE_X86_64  (1ULL << 0)
#define DOLRECOMP_FEATURE_SSE42   (1ULL << 1)
#define DOLRECOMP_FEATURE_POPCNT  (1ULL << 2)
#define DOLRECOMP_FEATURE_AVX     (1ULL << 3)
#define DOLRECOMP_FEATURE_AVX2    (1ULL << 4)
#define DOLRECOMP_FEATURE_FMA     (1ULL << 5)
#define DOLRECOMP_FEATURE_BMI1    (1ULL << 6)
#define DOLRECOMP_FEATURE_BMI2    (1ULL << 7)
#define DOLRECOMP_FEATURE_SSE3    (1ULL << 8)
#define DOLRECOMP_FEATURE_SSSE3   (1ULL << 9)
#define DOLRECOMP_FEATURE_SSE41   (1ULL << 10)
#define DOLRECOMP_FEATURE_CX16    (1ULL << 11)
#define DOLRECOMP_FEATURE_LAHF    (1ULL << 12)
#define DOLRECOMP_FEATURE_MOVBE   (1ULL << 13)
#define DOLRECOMP_FEATURE_LZCNT   (1ULL << 14)
#define DOLRECOMP_FEATURE_AARCH64 (1ULL << 32)
#define DOLRECOMP_FEATURE_ARM_CRC (1ULL << 33)

#define DOLRECOMP_FEATURE_X86_V2 \
    (DOLRECOMP_FEATURE_X86_64 | DOLRECOMP_FEATURE_SSE3 | \
     DOLRECOMP_FEATURE_SSSE3 | DOLRECOMP_FEATURE_SSE41 | \
     DOLRECOMP_FEATURE_SSE42 | DOLRECOMP_FEATURE_POPCNT | \
     DOLRECOMP_FEATURE_CX16 | DOLRECOMP_FEATURE_LAHF)
#define DOLRECOMP_FEATURE_X86_V3 \
    (DOLRECOMP_FEATURE_X86_V2 | DOLRECOMP_FEATURE_AVX | \
     DOLRECOMP_FEATURE_AVX2 | DOLRECOMP_FEATURE_FMA | \
     DOLRECOMP_FEATURE_BMI1 | DOLRECOMP_FEATURE_BMI2 | \
     DOLRECOMP_FEATURE_MOVBE | DOLRECOMP_FEATURE_LZCNT)
#define DOLRECOMP_FEATURE_ARMV8 DOLRECOMP_FEATURE_AARCH64
#define DOLRECOMP_FEATURE_CORTEX_A57 \
    (DOLRECOMP_FEATURE_AARCH64 | DOLRECOMP_FEATURE_ARM_CRC)

typedef int (*DolRecompDispatch)(CPUState* state, u32 max_blocks);

typedef struct {
    u64 required_features;
    DolRecompSemanticMode semantics;
    DolRecompDispatch dispatch;
    const char* name;
} DolRecompVariantV4;

typedef struct {
    u32 abi_version;
    u32 cpu_state_size;
    u32 variant_count;
    const DolRecompVariantV4* variants;
    DolRecompDispatch legacy_dispatch;
} DolRecompModule;

typedef struct {
    DolRecompDispatch dispatch;
    const char* variant_name;
    u64 host_features;
    DolRecompSemanticMode semantics;
} DolRecompLoadedModule;

#endif

u64 dolrecomp_detect_host_features(void);
int dolrecomp_select_module(const DolRecompModule* module, u64 host_features,
                            DolRecompSemanticMode semantics,
                            DolRecompLoadedModule* loaded, FILE* diagnostics);
int dolrecomp_initialize_module(const DolRecompModule* module,
                                DolRecompSemanticMode semantics,
                                DolRecompLoadedModule* loaded,
                                FILE* diagnostics);

static inline int dolrecomp_run_loaded(const DolRecompLoadedModule* loaded,
                                       CPUState* state, u32 max_blocks) {
    return loaded && loaded->dispatch
               ? loaded->dispatch(state, max_blocks)
               : 0;
}

#endif
