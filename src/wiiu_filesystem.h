#ifndef BOTW_WIIU_FILESYSTEM_H
#define BOTW_WIIU_FILESYSTEM_H

#include <stddef.h>

#include "cpu/cpu.h"

void wiiu_filesystem_set_title_paths(const char* base_title_dir,
                                     const char* update_title_dir,
                                     const char* dlc_title_dir);
void wiiu_filesystem_reset(void);
void wiiu_filesystem_shutdown(void);
bool wiiu_filesystem_handle_import(CPUState* cpu, const char* name);
bool wiiu_filesystem_read_last_file(u32 expected_size, u8** data_out,
                                    size_t* size_out);
bool wiiu_filesystem_has_dlc(void);
/* Diagnostic count: unrelated imports must never probe the DLC directory. */
u64 wiiu_filesystem_dlc_probe_count(void);

#endif
