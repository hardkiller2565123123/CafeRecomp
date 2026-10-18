#ifndef DOLRECOMP_BACKEND_CODEGEN_H
#define DOLRECOMP_BACKEND_CODEGEN_H

#include "common/types.h"
#include "frontend/decoder.h"
#include "backend/emitter.h"
#include "analysis/code_section.h"
#include <stddef.h>

#define EMIT_CHUNK_INSTRUCTIONS 4096u

typedef struct {
    const PPCInst* insts;
    u32 count;
    u32 func_addr;
    char path[1200];
    char include_name[512];
} ChunkJob;

typedef int (*ParallelJobFunction)(const void* job, void* user);

int run_chunk_jobs(const ChunkJob* jobs, u32 job_count, u32 requested_jobs);
int run_parallel_jobs(const void* jobs, size_t job_size, u32 job_count,
                      u32 requested_jobs, ParallelJobFunction function,
                      void* user);
u32 effective_chunk_jobs(u32 job_count, u32 requested_jobs);

#endif
