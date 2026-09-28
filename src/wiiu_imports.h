#ifndef BOTW_WIIU_IMPORTS_H
#define BOTW_WIIU_IMPORTS_H

#include "cpu/cpu.h"
#include "rpx_runtime.h"
#include "wiiu_memory.h"

/* Cafe initializes these quantization formats for every new guest thread. */
void wiiu_imports_init_cpu_context(CPUState* cpu);

void wiiu_imports_attach_rpx(const RPXFile* rpx);
void wiiu_imports_set_title_paths(const char* base_title_dir,
                                  const char* update_title_dir,
                                  const char* dlc_title_dir);
void wiiu_imports_reset_stats(void);
void wiiu_imports_shutdown(void);
void wiiu_imports_dump_threads(CPUState* main_cpu);
u32 wiiu_imports_unknown_count(void);
void wiiu_imports_seed_data_imports(WiiUMemory* memory);
const RPXSymbol* wiiu_imports_find_symbol(u32 address);
const RPXSymbol* wiiu_imports_find_symbol_name(const char* name);
bool wiiu_imports_host_call(CPUState* cpu, u32 address);
void wiiu_imports_advance_timebase(u64 ticks);
void wiiu_imports_run_alarms(CPUState* main_cpu, u32 callback_blocks);
void wiiu_imports_run_resumed_threads(CPUState* main_cpu,
                                      u32 blocks_per_thread);
void wiiu_imports_run_audio(CPUState* main_cpu, u32 callback_blocks);

#endif
