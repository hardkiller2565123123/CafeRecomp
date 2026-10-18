// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DOLRECOMP_C_CFG_H
#define DOLRECOMP_C_CFG_H

#include "../common/types.h"
#include "../frontend/decoder.h"

typedef struct {
    u32 count;
    u8* leaders;
    u32* block_cycles;
    u8* materialize_pc;
    u8* return_targets;
    u32* loop_ends;
} CFunctionCFG;

bool c_function_cfg_build(CFunctionCFG* cfg, const PPCInst* insts, u32 count,
                          u32 function_address);
void c_function_cfg_destroy(CFunctionCFG* cfg);

bool c_function_cfg_contains(const CFunctionCFG* cfg, u32 function_address,
                             u32 address);
bool c_function_cfg_can_loop_directly(const CFunctionCFG* cfg,
                                      const PPCInst* insts,
                                      u32 function_address,
                                      u32 branch_index);

#endif
