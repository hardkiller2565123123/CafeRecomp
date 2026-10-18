#include "backend/variant_output.h"

static const char* feature_expression(DolLLVMTargetProfile profile) {
    switch (profile) {
    case DOLLLVM_TARGET_X86_64_V2:
        return "DOLRECOMP_FEATURE_X86_V2";
    case DOLLLVM_TARGET_X86_64_V3:
        return "DOLRECOMP_FEATURE_X86_V3";
    case DOLLLVM_TARGET_AARCH64_GENERIC:
        return "DOLRECOMP_FEATURE_ARMV8";
    case DOLLLVM_TARGET_AARCH64_A57:
        return "DOLRECOMP_FEATURE_CORTEX_A57";
    case DOLLLVM_TARGET_HOST:
    default:
        return "0";
    }
}

static void emit_dispatch(FILE* out, const char* suffix) {
    fprintf(out,
            "\nstatic inline int dolrecomp_call%s(CPUState* ctx, u32 address) {\n"
            "    u32 alias;\n"
            "    ctx->pc = address;\n"
            "    if (dolrecomp_dispatch_replacement(ctx, address)) return 1;\n"
            "    if (ctx->host_call && ppc_host_call(ctx, address)) return 1;\n"
            "    DolRecompFunction fn = dolrecomp_find_original%s(address);\n"
            "    if (fn) { fn(ctx); return 1; }\n"
            "    if (dolrecomp_physical_pc_alias(ctx, address, &alias)) {\n"
            "        ctx->pc = alias;\n"
            "        if (dolrecomp_dispatch_replacement(ctx, alias)) return 1;\n"
            "        if (ctx->host_call && ppc_host_call(ctx, alias)) return 1;\n"
            "        fn = dolrecomp_find_original%s(alias);\n"
            "        if (fn) { fn(ctx); return 1; }\n"
            "    }\n"
            "    return 0;\n"
            "}\n"
            "\nstatic inline int dolrecomp_run_blocks%s(CPUState* ctx, "
            "u32 max_blocks) {\n"
            "    u32 blocks = 0;\n"
            "    while (max_blocks == 0u || blocks < max_blocks) {\n"
            "        if (!dolrecomp_call%s(ctx, ctx->pc) || ctx->exception) "
            "return 0;\n"
            "        blocks++;\n"
            "    }\n"
            "    return 1;\n"
            "}\n",
            suffix, suffix, suffix, suffix, suffix);
}

void emit_llvm_variant_table(FILE* out, const FunctionList* functions,
                             const DolLLVMTargetProfile* profiles,
                             u32 profile_count, int fast_semantics) {
    fprintf(out,
            "\n#ifndef DOLRECOMP_MODULE_ABI_TYPES\n"
            "#define DOLRECOMP_MODULE_ABI_TYPES\n"
            "#define DOLRECOMP_MODULE_ABI_V3 3u\n"
            "#define DOLRECOMP_MODULE_ABI_V4 4u\n"
            "typedef enum { DOLRECOMP_SEMANTICS_EXACT, "
            "DOLRECOMP_SEMANTICS_FAST } DolRecompSemanticMode;\n"
            "enum {\n"
            " DOLRECOMP_FEATURE_X86_64=1ull<<0, "
            "DOLRECOMP_FEATURE_SSE42=1ull<<1,\n"
            " DOLRECOMP_FEATURE_POPCNT=1ull<<2, "
            "DOLRECOMP_FEATURE_AVX=1ull<<3,\n"
            " DOLRECOMP_FEATURE_AVX2=1ull<<4, "
            "DOLRECOMP_FEATURE_FMA=1ull<<5,\n"
            " DOLRECOMP_FEATURE_BMI1=1ull<<6, "
            "DOLRECOMP_FEATURE_BMI2=1ull<<7,\n"
            " DOLRECOMP_FEATURE_SSE3=1ull<<8, "
            "DOLRECOMP_FEATURE_SSSE3=1ull<<9,\n"
            " DOLRECOMP_FEATURE_SSE41=1ull<<10, "
            "DOLRECOMP_FEATURE_CX16=1ull<<11,\n"
            " DOLRECOMP_FEATURE_LAHF=1ull<<12, "
            "DOLRECOMP_FEATURE_MOVBE=1ull<<13,\n"
            " DOLRECOMP_FEATURE_LZCNT=1ull<<14, "
            "DOLRECOMP_FEATURE_AARCH64=1ull<<32,\n"
            " DOLRECOMP_FEATURE_ARM_CRC=1ull<<33 };\n"
            "#define DOLRECOMP_FEATURE_X86_V2 "
            "(DOLRECOMP_FEATURE_X86_64|DOLRECOMP_FEATURE_SSE3|"
            "DOLRECOMP_FEATURE_SSSE3|DOLRECOMP_FEATURE_SSE41|"
            "DOLRECOMP_FEATURE_SSE42|DOLRECOMP_FEATURE_POPCNT|"
            "DOLRECOMP_FEATURE_CX16|DOLRECOMP_FEATURE_LAHF)\n"
            "#define DOLRECOMP_FEATURE_X86_V3 "
            "(DOLRECOMP_FEATURE_X86_V2|DOLRECOMP_FEATURE_AVX|"
            "DOLRECOMP_FEATURE_AVX2|DOLRECOMP_FEATURE_FMA|"
            "DOLRECOMP_FEATURE_BMI1|DOLRECOMP_FEATURE_BMI2|"
            "DOLRECOMP_FEATURE_MOVBE|DOLRECOMP_FEATURE_LZCNT)\n"
            "#define DOLRECOMP_FEATURE_ARMV8 DOLRECOMP_FEATURE_AARCH64\n"
            "#define DOLRECOMP_FEATURE_CORTEX_A57 "
            "(DOLRECOMP_FEATURE_AARCH64|DOLRECOMP_FEATURE_ARM_CRC)\n"
            "typedef int (*DolRecompDispatch)(CPUState*,u32);\n"
            "typedef struct { u64 required_features; "
            "DolRecompSemanticMode semantics; DolRecompDispatch dispatch; "
            "const char* name; } DolRecompVariantV4;\n"
            "typedef struct { u32 abi_version; u32 cpu_state_size; "
            "u32 variant_count; const DolRecompVariantV4* variants; "
            "DolRecompDispatch legacy_dispatch; } DolRecompModule;\n"
            "#endif\n");
    for (u32 i = 1; i < profile_count; i++) {
        char suffix[48];
        snprintf(suffix, sizeof(suffix), "__%s",
                 dolllvm_target_profile_suffix(profiles[i]));
        emit_function_lookup(out, functions, suffix);
        emit_dispatch(out, suffix);
    }
    fprintf(out,
            "\nstatic const DolRecompVariantV4 dolrecomp_variants_v4[] = {\n");
    for (u32 i = 0; i < profile_count; i++) {
        const char* suffix = i ? dolllvm_target_profile_suffix(profiles[i]) : "";
        fprintf(out, "    {%s, %s, dolrecomp_run_blocks%s%s, \"%s-%s\"},\n",
                feature_expression(profiles[i]),
                fast_semantics ? "DOLRECOMP_SEMANTICS_FAST"
                               : "DOLRECOMP_SEMANTICS_EXACT",
                i ? "__" : "", suffix,
                dolllvm_target_profile_name(profiles[i]),
                fast_semantics ? "fast" : "exact");
    }
    fprintf(out,
            "};\n"
            "static const DolRecompModule dolrecomp_module_v4 = {\n"
            "    DOLRECOMP_MODULE_ABI_V4, sizeof(CPUState),\n"
            "    (u32)(sizeof(dolrecomp_variants_v4) / "
            "sizeof(dolrecomp_variants_v4[0])),\n"
            "    dolrecomp_variants_v4, dolrecomp_run_blocks,\n"
            "};\n");
}
// most elegant code ever written you can trust me on this one :)
