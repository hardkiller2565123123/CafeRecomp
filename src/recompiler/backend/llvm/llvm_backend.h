#ifndef DOLRECOMP_LLVM_BACKEND_H
#define DOLRECOMP_LLVM_BACKEND_H

#include "ir/dolir.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOLLLVM_FUNCTION_ABI_NATIVE = 1u << 0,
    DOLLLVM_FUNCTION_ABI_NATIVE_MEMORY = 1u << 1,
} DolLLVMFunctionABIFlag;

#define DOLLLVM_NATIVE_ABI_VERSION 2u

typedef struct {
    u32 start;
    u32 end;
    u64 input_state[DOLIR_STATE_MASK_WORDS];
    u64 output_state[DOLIR_STATE_MASK_WORDS];
    u64 escape_state[DOLIR_STATE_MASK_WORDS];
    u32 abi_flags;
} DolLLVMFunctionRange;

typedef struct {
    u32 caller_start;
    u32 callee_address;
} DolLLVMCallEdge;

typedef enum {
    DOLLLVM_TARGET_HOST,
    DOLLLVM_TARGET_X86_64_V2,
    DOLLLVM_TARGET_X86_64_V3,
    DOLLLVM_TARGET_AARCH64_GENERIC,
    DOLLLVM_TARGET_AARCH64_A57,
} DolLLVMTargetProfile;

typedef enum {
    DOLLLVM_SEMANTICS_EXACT,
    DOLLLVM_SEMANTICS_FAST,
} DolLLVMSemantics;

typedef enum {
    DOLLLVM_INSTRUMENTATION_NONE,
    DOLLLVM_INSTRUMENTATION_LOCKSTEP,
} DolLLVMInstrumentation;

typedef struct {
    const char* target_triple;
    DolLLVMTargetProfile target_profile;
    DolLLVMSemantics semantics;
    DolLLVMInstrumentation instrumentation;
    int optimization_level;
    int verify;
    int emit_ir;
    const char* ir_path;
    const char* symbol_suffix;
    const char* profile_generate_path;
    const char* profile_use_path;
    const char* thinlto_path;
    int emit_thinlto;
    int fixed_memory_layout;
    u32 ram_size;
    u32 mem2_size;
    u64 partition_seed;
    int state_in_memory;
    const DolLLVMFunctionRange* function_ranges;
    u32 function_range_count;
    const u32* entry_points;
    u32 entry_point_count;
} DolLLVMOptions;

bool dolllvm_emit_object(const DolIRModule* module, const char* object_path,
                         const DolLLVMOptions* options, FILE* diagnostics);
bool dolllvm_effective_triple(const DolLLVMOptions* options, char* out,
                              size_t size);
bool dolllvm_object_matches_options(const char* path,
                                    const DolLLVMOptions* options);
bool dolllvm_parse_target_profile(const char* name,
                                  DolLLVMTargetProfile* profile);
const char* dolllvm_target_profile_name(DolLLVMTargetProfile profile);
const char* dolllvm_target_profile_suffix(DolLLVMTargetProfile profile);

bool dolllvm_analyze_function_abi(const DolIRFunction *function,
                                  DolLLVMFunctionRange *range);
bool dolllvm_propagate_function_abis(DolLLVMFunctionRange *ranges,
                                     u32 range_count,
                                     const DolLLVMCallEdge *edges,
                                     u32 edge_count);
void dolllvm_report_abi_stats(const DolLLVMFunctionRange *ranges,
                              u32 range_count, const char *target_triple,
                              FILE *output);

// Profile-guided optimization for the LLVM backend.
//
// The C backend gets PGO for free: its chunks are C source, so clang's own
// -fprofile-generate / -fprofile-use reach them through CFLAGS. Nothing reaches
// these objects the same way, because this backend emits IR and codegens it
// in-process -- clang never sees a translation unit. Sample/AutoFDO is
// genuinely unavailable (the emitter attaches no DILocation, so a sample
// profile has nothing to bind to), but IR-level *instrumentation* PGO needs no
// debug info: it is two LLVM passes run over the module the backend builds.
//
//   DOLRECOMP_LLVM_PGO=gen              instrument
//   DOLRECOMP_LLVM_PGO=use              apply DOLRECOMP_LLVM_PROFILE
//   DOLRECOMP_LLVM_PROFILE=<file>       merged .profdata (use mode only)
//
// Both are placed at the very front of the pipeline, on the raw emitter output,
// so the CFG hashes PGOInstrumentationUse matches against are computed on
// exactly the IR PGOInstrumentationGen saw. The lowering pass runs last, as
// clang does it, so the counter intrinsics stay opaque to the optimizer and
// cannot be sunk, merged or eliminated into wrong counts.
//
// -fprofile-generate must still reach the module link line, which is where the
// profiling runtime comes from. A C half of the same module instrumented by
// clang through CFLAGS writes IR-level counters into the same profile, so the
// two merge without special handling.
typedef enum {
    DOLLLVM_PGO_OFF = 0,
    DOLLLVM_PGO_GEN = 1,
    DOLLLVM_PGO_USE = 2
} DolLLVMPGOMode;

int dolllvm_pgo_mode(void);

// The profile-vs-DOL staleness gate.
//
// A profile that no longer describes the emitted CFG does not fail. It
// degrades: PGOInstrumentationUse rejects the mismatched records one function
// at a time and leaves those functions unprofiled, so the build succeeds, the
// module looks trained, and the measurement is quietly against something
// between a profiled and an unprofiled arm. Clang's own warning for the C half
// (-Wprofile-instr-out-of-date) was suppressed on this project, and the LLVM
// half never had one at all.
//
// The gate is a POSITIVE check rather than a warning scrape: after
// PGOInstrumentationUse has run -- observed through a pass-instrumentation
// callback, so nothing about the pipeline moves -- every defined function that
// matched its profile record carries entry-count metadata, and every function
// that did not carries none. On a profile collected from this same module that
// second set is empty, because IR instrumentation records EVERY function at
// gen time, whether or not the scene ever executed it. So a non-zero count
// means the profile and the DOL have diverged, not that the scene was narrow.
//
//   DOLRECOMP_LLVM_PGO_STALE=error   fail the emit (the default)
//   DOLRECOMP_LLVM_PGO_STALE=warn    report and keep building
//   DOLRECOMP_LLVM_PGO_STALE=off     no check
typedef enum {
    DOLLLVM_PGO_STALE_OFF = 0,
    DOLLLVM_PGO_STALE_WARN = 1,
    DOLLLVM_PGO_STALE_ERROR = 2
} DolLLVMPGOStalePolicy;

int dolllvm_pgo_stale_policy(void);

// Every codegen-affecting input that is not already in the object cache key:
// LLVM version, target CPU and feature string, relocation and code model, and
// the pass pipeline. Hash this alongside the instruction words, or a codegen
// change silently reuses objects built with the old settings.
bool dolllvm_codegen_fingerprint(const DolLLVMOptions* options, char* out,
                                 size_t size);

#ifdef __cplusplus
}
#endif

#endif
