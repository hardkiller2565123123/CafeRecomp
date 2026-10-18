#ifndef DOLRECOMP_EMITTER_H
#define DOLRECOMP_EMITTER_H

#include "../common/types.h"
#include "../frontend/decoder.h"
#include <stdio.h>

typedef enum {
    DOLRECOMP_CPU_GEKKO,
    DOLRECOMP_CPU_BROADWAY,
    DOLRECOMP_CPU_ESPRESSO,
} DolRecompCPU;

// Split C emitter used by the command-line recompiler.

// emit the boilerplate header (includes, typedefs, etc)
void emit_header(FILE* out);
void emit_header_for_cpu(FILE* out, DolRecompCPU cpu);

// Register chunk entries for the opt-in cross-chunk direct-call experiment.
// Direct calls bypass chassis dispatch checks and must not be enabled by a
// runtime that validates mutable guest code there. Call before worker emission;
// passing count == 0 restores the safe return-to-chassis form.
void emit_set_chunk_table(const u32* starts, u32 count);

// emit a single recompiled function as C code
bool emit_function(FILE* out, const PPCInst* insts, u32 count, u32 func_addr);

// emit a single instruction as C code
void emit_instruction(FILE* out, const PPCInst* inst);

// emit the boilerplate footer
void emit_footer(FILE* out);

#endif /* DOLRECOMP_EMITTER_H */

// i would just like to mention, that those comments have been here since the beginning of time.
// do not judge old me