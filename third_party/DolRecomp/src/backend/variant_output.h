#ifndef DOLRECOMP_VARIANT_OUTPUT_H
#define DOLRECOMP_VARIANT_OUTPUT_H

#include "backend/dispatch.h"
#include "backend/llvm/llvm_backend.h"

void emit_llvm_variant_table(FILE* out, const FunctionList* functions,
                             const DolLLVMTargetProfile* profiles,
                             u32 profile_count, int fast_semantics);

#endif
