#ifndef BOTW_RPX_RUNTIME_H
#define BOTW_RPX_RUNTIME_H

#include "common/types.h"

#define RPX_MAX_CODE_SECTIONS 64
#define RPX_MAX_LOAD_SECTIONS 128
#define RPX_MAX_SYMBOLS 2048
#define RPX_SYMBOL_SECTION_ALIAS 0xFFFFu

typedef struct {
    char name[128];
    u32 value;
    u32 size;
    u16 section_index;
    u8 info;
    u8 other;
} RPXSymbol;

typedef struct {
    char name[64];
    u32 type;
    u32 offset;
    u32 address;
    u32 size;
    u32 flags;
    bool compressed;
    bool executable;
    bool nobits;
    const u8* data;
    u8* owned_data;
} RPXLoadSection;

typedef struct {
    char name[64];
    u32 offset;
    u32 address;
    u32 size;
    u32 flags;
    bool compressed;
    const u8* data;
    u8* owned_data;
} RPXCodeSection;

typedef struct {
    u8* file_data;
    u32 file_size;
    u32 entry_point;
    u16 section_count;
    u32 load_section_count;
    u32 code_section_count;
    u32 symbol_count;
    u32 relocation_count;
    u32 applied_relocation_count;
    u32 import_alias_count;
    RPXLoadSection load_sections[RPX_MAX_LOAD_SECTIONS];
    RPXCodeSection code_sections[RPX_MAX_CODE_SECTIONS];
    RPXSymbol symbols[RPX_MAX_SYMBOLS];
} RPXFile;

bool rpx_load(RPXFile* rpx, const char* path);
void rpx_free(RPXFile* rpx);
void rpx_print_info(const RPXFile* rpx, const char* game_name);

#endif /* BOTW_RPX_RUNTIME_H */
