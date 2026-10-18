#ifndef DOLRECOMP_DOLIR_BUILDER_H
#define DOLRECOMP_DOLIR_BUILDER_H

#include "frontend/decoder.h"
#include "ir/dolir.h"

#ifdef __cplusplus
extern "C" {
#endif

bool dolir_build_chunk(DolIRModule* module, const PPCInst* insts, u32 count,
                       u32 guest_start);
u32 dolir_instruction_cycle_cost(const PPCInst* inst);

#ifdef __cplusplus
}
#endif

#endif
