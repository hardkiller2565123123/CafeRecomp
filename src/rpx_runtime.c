// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 ExpansionPak

#include "rpx_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DOLRECOMP_HAVE_ZLIB
#include <zlib.h>
#endif

#if defined(_WIN32) && !defined(DOLRECOMP_HAVE_ZLIB)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef unsigned long DynZlibULong;
typedef int (__cdecl *DynZlibUncompressFn)(unsigned char* dest,
                                           DynZlibULong* dest_len,
                                           const unsigned char* source,
                                           DynZlibULong source_len);

static HMODULE dyn_zlib_module = NULL;
static DynZlibUncompressFn dyn_zlib_uncompress = NULL;
static int dyn_zlib_searched = 0;

static int accept_dyn_zlib(HMODULE module) {
    if (!module)
        return 0;

    dyn_zlib_uncompress =
        (DynZlibUncompressFn)GetProcAddress(module, "uncompress");
    if (!dyn_zlib_uncompress) {
        FreeLibrary(module);
        return 0;
    }

    dyn_zlib_module = module;
    return 1;
}

static int try_load_dyn_zlib_path(const char* path) {
    return path && path[0] != '\0' && accept_dyn_zlib(LoadLibraryA(path));
}

static int try_load_git_dyn_zlib(const char* root) {
    static const char* suffixes[] = {
        "\\Git\\mingw64\\bin\\zlib1.dll",
        "\\Git\\mingw64\\libexec\\git-core\\zlib1.dll",
        "\\Git\\usr\\bin\\zlib1.dll",
    };

    if (!root || root[0] == '\0')
        return 0;

    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        char path[MAX_PATH];
        int written = snprintf(path, sizeof(path), "%s%s", root, suffixes[i]);
        if (written > 0 && (size_t)written < sizeof(path) &&
            try_load_dyn_zlib_path(path)) {
            return 1;
        }
    }

    return 0;
}

static int try_load_env_dyn_zlib_path(const char* name) {
    char value[MAX_PATH];
    DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    if (length == 0 || length >= sizeof(value))
        return 0;

    return try_load_dyn_zlib_path(value);
}

static int try_load_env_git_dyn_zlib(const char* name) {
    char value[MAX_PATH];
    DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    if (length == 0 || length >= sizeof(value))
        return 0;

    return try_load_git_dyn_zlib(value);
}

static int load_dyn_zlib(void) {
    if (dyn_zlib_searched)
        return dyn_zlib_uncompress != NULL;

    dyn_zlib_searched = 1;

    if (try_load_env_dyn_zlib_path("DOLRECOMP_ZLIB_DLL") ||
        accept_dyn_zlib(LoadLibraryA("zlib1.dll")) ||
        try_load_env_git_dyn_zlib("ProgramFiles") ||
        try_load_env_git_dyn_zlib("ProgramFiles(x86)")) {
        return 1;
    }

    return 0;
}
#endif

enum {
    ELF_HEADER_SIZE = 52,
    ELF_SECTION_SIZE = 40,
    ELF_CLASS_32 = 1,
    ELF_DATA_BE = 2,
    ELF_VERSION_CURRENT = 1,
    ELF_MACHINE_PPC = 20,
    ELF_TYPE_RPL = 0xfe01,
    SHF_ALLOC = 0x00000002,
    SHF_EXECINSTR = 0x00000004,
    SHF_RPL_ZLIB = 0x08000000,
    SHT_PROGBITS = 1,
    SHT_SYMTAB = 2,
    SHT_STRTAB = 3,
    SHT_RELA = 4,
    SHT_NOBITS = 8,
    R_PPC_NONE = 0,
    R_PPC_ADDR32 = 1,
    R_PPC_ADDR16_LO = 4,
    R_PPC_ADDR16_HI = 5,
    R_PPC_ADDR16_HA = 6,
    R_PPC_REL24 = 10,
    R_PPC_REL14 = 11,
    R_PPC_DTPMOD32 = 68,
    R_PPC_DTPREL32 = 78,
    R_PPC_EMB_SDA21 = 109,
    R_PPC_REL16_HA = 251,
    R_PPC_REL16_HI = 252,
    R_PPC_REL16_LO = 253,
};

typedef struct {
    u32 name_offset;
    u32 type;
    u32 flags;
    u32 address;
    u32 offset;
    u32 size;
    u32 link;
    u32 info;
    u32 alignment;
    u32 entry_size;
} RPXSectionHeader;

static int range_fits(u32 offset, u32 size, u32 total) {
    return offset <= total && size <= total - offset;
}

static int address_in_range(u32 address, u32 base, u32 size) {
    return size != 0 && address >= base && address - base < size;
}

static const char* section_name_at(const u8* names, u32 names_size, u32 offset) {
    if (offset >= names_size)
        return NULL;

    for (u32 i = offset; i < names_size; i++) {
        if (names[i] == '\0')
            return (const char*)names + offset;
    }

    return NULL;
}

static void copy_section_name(char* out, size_t out_size, const char* name) {
    if (!name || name[0] == '\0')
        name = "<anonymous>";

    snprintf(out, out_size, "%s", name);
}

static int decode_section_bytes(const RPXFile* rpx, const RPXSectionHeader* section,
                                const char* name, const u8** out_data,
                                u8** out_owned, u32* out_size) {
    *out_data = NULL;
    *out_owned = NULL;
    *out_size = 0;

    if (section->type == SHT_NOBITS) {
        *out_size = section->size;
        return 1;
    }

    if (!range_fits(section->offset, section->size, rpx->file_size)) {
        fprintf(stderr, "error: RPX section '%s' is outside the file\n",
                name ? name : "<unknown>");
        return 0;
    }

    if ((section->flags & SHF_RPL_ZLIB) == 0) {
        *out_data = rpx->file_data + section->offset;
        *out_size = section->size;
        return 1;
    }

    if (section->size < 4) {
        fprintf(stderr, "error: compressed RPX section '%s' is missing its size word\n",
                name ? name : "<unknown>");
        return 0;
    }

    u32 decoded_size = read_be32(rpx->file_data + section->offset);
    u8* decoded = (u8*)malloc(decoded_size ? decoded_size : 1);
    if (!decoded) {
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }

#ifdef DOLRECOMP_HAVE_ZLIB
    uLongf dest_len = (uLongf)decoded_size;
    int status = uncompress(decoded, &dest_len,
                            rpx->file_data + section->offset + 4,
                            (uLong)section->size - 4u);
    if (status != Z_OK || dest_len != decoded_size) {
        free(decoded);
        fprintf(stderr, "error: can't decompress RPX section '%s' (zlib error %d)\n",
                name ? name : "<unknown>", status);
        return 0;
    }

    *out_data = decoded;
    *out_owned = decoded;
    *out_size = decoded_size;
    return 1;
#elif defined(_WIN32)
    if (!load_dyn_zlib()) {
        free(decoded);
        fprintf(stderr,
                "error: RPX section '%s' is compressed; rebuild with zlib, "
                "install zlib1.dll, or set DOLRECOMP_ZLIB_DLL\n",
                name ? name : "<unknown>");
        return 0;
    }

    DynZlibULong dest_len = (DynZlibULong)decoded_size;
    int status = dyn_zlib_uncompress(decoded, &dest_len,
                                     rpx->file_data + section->offset + 4,
                                     (DynZlibULong)section->size - 4u);
    if (status != 0 || dest_len != decoded_size) {
        free(decoded);
        fprintf(stderr,
                "error: can't decompress RPX section '%s' (zlib1.dll error %d)\n",
                name ? name : "<unknown>", status);
        return 0;
    }

    *out_data = decoded;
    *out_owned = decoded;
    *out_size = decoded_size;
    return 1;
#else
    free(decoded);
    fprintf(stderr,
            "error: RPX section '%s' is compressed; rebuild with zlib or decompress the RPX first\n",
            name ? name : "<unknown>");
    return 0;
#endif
}

static int read_section_header(const RPXFile* rpx, u32 table_offset,
                               u16 entry_size, u16 index,
                               RPXSectionHeader* out) {
    const u8* header = rpx->file_data + table_offset + (u32)index * entry_size;
    out->name_offset = read_be32(header + 0);
    out->type = read_be32(header + 4);
    out->flags = read_be32(header + 8);
    out->address = read_be32(header + 12);
    out->offset = read_be32(header + 16);
    out->size = read_be32(header + 20);
    out->link = read_be32(header + 24);
    out->info = read_be32(header + 28);
    out->alignment = read_be32(header + 32);
    out->entry_size = read_be32(header + 36);
    return 1;
}

static int rpx_section_is_loadable(const RPXSectionHeader* section) {
    return (section->flags & SHF_ALLOC) != 0 && section->size != 0;
}

static int add_load_section(RPXFile* rpx, const RPXSectionHeader* section,
                            const char* name) {
    if (!rpx_section_is_loadable(section))
        return 1;

    if (rpx->load_section_count >= RPX_MAX_LOAD_SECTIONS) {
        fprintf(stderr, "error: too many RPX loadable sections\n");
        return 0;
    }

    const u8* data = NULL;
    u8* owned = NULL;
    u32 decoded_size = 0;
    if (!decode_section_bytes(rpx, section, name, &data, &owned,
                              &decoded_size)) {
        return 0;
    }

    if (data && !owned && decoded_size != 0) {
        u8* copy = (u8*)malloc(decoded_size);
        if (!copy) {
            fprintf(stderr, "error: out of memory\n");
            return 0;
        }
        memcpy(copy, data, decoded_size);
        data = copy;
        owned = copy;
    }

    RPXLoadSection* load = &rpx->load_sections[rpx->load_section_count++];
    copy_section_name(load->name, sizeof(load->name), name);
    load->type = section->type;
    load->offset = section->offset;
    load->address = section->address;
    load->size = decoded_size;
    load->flags = section->flags;
    load->compressed = (section->flags & SHF_RPL_ZLIB) != 0;
    load->executable = (section->flags & SHF_EXECINSTR) != 0;
    load->nobits = section->type == SHT_NOBITS;
    load->data = data;
    load->owned_data = owned;
    return 1;
}

static RPXLoadSection* find_load_section(RPXFile* rpx, u32 address,
                                         u32 size) {
    for (u32 i = 0; i < rpx->load_section_count; i++) {
        RPXLoadSection* load = &rpx->load_sections[i];
        if (size != 0 && address >= load->address &&
            address - load->address < load->size &&
            size <= load->size - (address - load->address)) {
            return load;
        }
    }

    return NULL;
}

static u8* load_section_ptr(RPXLoadSection* load, u32 address) {
    return load->owned_data + (address - load->address);
}

static u32 branch_target(u32 instruction, u32 address) {
    u32 displacement = instruction & 0x03FFFFFCu;
    if ((displacement & 0x02000000u) != 0)
        displacement |= 0xFC000000u;

    if ((instruction & 0x00000002u) != 0)
        return displacement;
    return address + displacement;
}

static int branch_target_encodable(u32 branch_address, u32 target) {
    s64 delta = (s64)target - (s64)branch_address;
    return (target & 3u) == 0 && delta >= -0x02000000ll &&
           delta <= 0x01FFFFFCll;
}

static void patch_rel24(u8* place, u32 branch_address, u32 target) {
    u32 instruction = read_be32(place);
    u32 displacement = (target - branch_address) & 0x03FFFFFCu;
    write_be32(place, (instruction & ~0x03FFFFFCu) | displacement);
}

static int add_import_alias(RPXFile* rpx, u32 alias, const RPXSymbol* target) {
    if (!target || alias == 0)
        return 1;

    for (u32 i = 0; i < rpx->symbol_count; i++) {
        const RPXSymbol* existing = &rpx->symbols[i];
        if (existing->value == alias && strcmp(existing->name, target->name) == 0)
            return 1;
    }

    if (rpx->symbol_count >= RPX_MAX_SYMBOLS) {
        fprintf(stderr, "error: too many RPX symbols/import aliases\n");
        return 0;
    }

    RPXSymbol* alias_sym = &rpx->symbols[rpx->symbol_count++];
    snprintf(alias_sym->name, sizeof(alias_sym->name), "%s", target->name);
    alias_sym->value = alias;
    alias_sym->size = 0;
    alias_sym->section_index = RPX_SYMBOL_SECTION_ALIAS;
    alias_sym->info = target->info;
    alias_sym->other = target->other;
    rpx->import_alias_count++;
    return 1;
}

static int parse_symbol_table(RPXFile* rpx, const RPXSectionHeader* sections,
                              u16 section_count, const u8* names,
                              u32 names_size) {
    for (u16 i = 0; i < section_count; i++) {
        const RPXSectionHeader* sym_section = &sections[i];
        const char* sym_name =
            section_name_at(names, names_size, sym_section->name_offset);
        if (sym_section->type != SHT_SYMTAB)
            continue;

        if (sym_section->link >= section_count) {
            fprintf(stderr, "error: RPX symbol table has invalid string table link\n");
            return 0;
        }
        if (sym_section->entry_size != 16) {
            fprintf(stderr, "error: unsupported RPX symbol entry size %u\n",
                    sym_section->entry_size);
            return 0;
        }

        const RPXSectionHeader* str_section = &sections[sym_section->link];
        const char* str_name =
            section_name_at(names, names_size, str_section->name_offset);

        const u8* sym_data = NULL;
        u8* owned_sym_data = NULL;
        u32 sym_size = 0;
        if (!decode_section_bytes(rpx, sym_section, sym_name, &sym_data,
                                  &owned_sym_data, &sym_size)) {
            return 0;
        }

        const u8* str_data = NULL;
        u8* owned_str_data = NULL;
        u32 str_size = 0;
        if (!decode_section_bytes(rpx, str_section, str_name, &str_data,
                                  &owned_str_data, &str_size)) {
            free(owned_sym_data);
            return 0;
        }

        u32 count = sym_size / 16u;
        if (count > RPX_MAX_SYMBOLS)
            count = RPX_MAX_SYMBOLS;

        for (u32 s = 0; s < count; s++) {
            const u8* entry = sym_data + s * 16u;
            if (rpx->symbol_count >= RPX_MAX_SYMBOLS)
                break;
            RPXSymbol* sym = &rpx->symbols[rpx->symbol_count++];
            const char* name =
                section_name_at(str_data, str_size, read_be32(entry + 0));
            copy_section_name(sym->name, sizeof(sym->name), name);
            sym->value = read_be32(entry + 4);
            sym->size = read_be32(entry + 8);
            sym->info = entry[12];
            sym->other = entry[13];
            sym->section_index = read_be16(entry + 14);
        }

        free(owned_str_data);
        free(owned_sym_data);
        return 1;
    }

    return 1;
}

static int apply_one_relocation(RPXFile* rpx, u32 offset, u32 info,
                                u32 addend) {
    u32 type = info & 0xFFu;
    u32 symbol_index = info >> 8;
    if (type == R_PPC_NONE)
        return 1;

    if (symbol_index >= rpx->symbol_count) {
        fprintf(stderr, "error: RPX relocation references missing symbol %u\n",
                symbol_index);
        return 0;
    }

    const RPXSymbol* sym = &rpx->symbols[symbol_index];
    u32 target = sym->value + addend;
    u32 access_size = type == R_PPC_ADDR16_LO || type == R_PPC_ADDR16_HI ||
                              type == R_PPC_ADDR16_HA ||
                              type == R_PPC_REL16_LO ||
                              type == R_PPC_REL16_HI ||
                              type == R_PPC_REL16_HA
                          ? 2u
                          : 4u;
    RPXLoadSection* load = find_load_section(rpx, offset, access_size);
    if (!load || !load->owned_data) {
        fprintf(stderr,
                "error: RPX relocation at 0x%08X does not target a loaded section\n",
                offset);
        return 0;
    }

    u8* place = load_section_ptr(load, offset);
    switch (type) {
    case R_PPC_ADDR32:
        write_be32(place, target);
        rpx->applied_relocation_count++;
        return 1;
    case R_PPC_ADDR16_LO:
        write_be16(place, (u16)target);
        rpx->applied_relocation_count++;
        return 1;
    case R_PPC_ADDR16_HI:
        write_be16(place, (u16)(target >> 16));
        rpx->applied_relocation_count++;
        return 1;
    case R_PPC_ADDR16_HA:
        write_be16(place, (u16)((target + 0x8000u) >> 16));
        rpx->applied_relocation_count++;
        return 1;
    case R_PPC_REL24:
        if (branch_target_encodable(offset, target)) {
            patch_rel24(place, offset, target);
            rpx->applied_relocation_count++;
            return 1;
        }

        return add_import_alias(rpx, branch_target(read_be32(place), offset), sym);
    case R_PPC_REL14: {
        s64 delta = (s64)target - (s64)offset;
        if ((target & 3u) != 0 || delta < -0x8000ll || delta > 0x7FFCLL) {
            fprintf(stderr,
                    "error: RPX REL14 target 0x%08X is out of range at 0x%08X\n",
                    target, offset);
            return 0;
        }
        u32 instruction = read_be32(place);
        write_be32(place, (instruction & ~0x0000FFFCu) |
                              ((u32)delta & 0x0000FFFCu));
        rpx->applied_relocation_count++;
        return 1;
    }
    case R_PPC_DTPMOD32:
        /* U-King is the only TLS module in this standalone runtime. */
        write_be32(place, 0u);
        rpx->applied_relocation_count++;
        return 1;
    case R_PPC_DTPREL32:
        write_be32(place, target);
        rpx->applied_relocation_count++;
        return 1;
    case R_PPC_REL16_LO:
        write_be16(place, (u16)(target - offset));
        rpx->applied_relocation_count++;
        return 1;
    case R_PPC_REL16_HI:
        write_be16(place, (u16)((target - offset) >> 16));
        rpx->applied_relocation_count++;
        return 1;
    case R_PPC_REL16_HA:
        write_be16(place, (u16)(((target - offset) + 0x8000u) >> 16));
        rpx->applied_relocation_count++;
        return 1;
    case R_PPC_EMB_SDA21: {
        u32 instruction = read_be32(place);
        u32 register_index = (instruction >> 16) & 0x1Fu;
        u32 base = 0u;
        const char* base_name = register_index == 2u ? "_SDA2_BASE_" :
                                register_index == 13u ? "_SDA_BASE_" : NULL;
        if (!base_name) {
            fprintf(stderr, "error: RPX SDA21 uses r%u at 0x%08X\n",
                    register_index, offset);
            return 0;
        }
        for (u32 i = 0; i < rpx->symbol_count; i++) {
            if (strcmp(rpx->symbols[i].name, base_name) == 0) {
                base = rpx->symbols[i].value;
                break;
            }
        }
        if (base == 0u) {
            fprintf(stderr, "error: RPX is missing %s for SDA21\n", base_name);
            return 0;
        }
        write_be32(place, (instruction & 0xFFE00000u) |
                              (register_index << 16) |
                              ((target - base) & 0xFFFFu));
        rpx->applied_relocation_count++;
        return 1;
    }
    default:
        fprintf(stderr, "error: unsupported RPX relocation type %u\n", type);
        return 0;
    }
}

static int apply_relocations(RPXFile* rpx, const RPXSectionHeader* sections,
                             u16 section_count, const u8* names,
                             u32 names_size) {
    for (u16 i = 0; i < section_count; i++) {
        const RPXSectionHeader* rela_section = &sections[i];
        if (rela_section->type != SHT_RELA)
            continue;

        const char* rela_name =
            section_name_at(names, names_size, rela_section->name_offset);
        if (rela_section->entry_size != 12) {
            fprintf(stderr, "error: unsupported RPX relocation entry size %u\n",
                    rela_section->entry_size);
            return 0;
        }

        const u8* rela_data = NULL;
        u8* owned_rela_data = NULL;
        u32 rela_size = 0;
        if (!decode_section_bytes(rpx, rela_section, rela_name, &rela_data,
                                  &owned_rela_data, &rela_size)) {
            return 0;
        }

        for (u32 offset = 0; offset + 12u <= rela_size; offset += 12u) {
            const u8* entry = rela_data + offset;
            if (!apply_one_relocation(rpx, read_be32(entry + 0),
                                      read_be32(entry + 4),
                                      read_be32(entry + 8))) {
                free(owned_rela_data);
                return 0;
            }
            rpx->relocation_count++;
        }

        free(owned_rela_data);
    }

    return 1;
}

bool rpx_load(RPXFile* rpx, const char* path) {
    memset(rpx, 0, sizeof(*rpx));

    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "error: can't open '%s'\n", path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        fprintf(stderr, "error: can't measure '%s'\n", path);
        return false;
    }

    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        fprintf(stderr, "error: can't measure '%s'\n", path);
        return false;
    }

    if (size < ELF_HEADER_SIZE) {
        fclose(file);
        fprintf(stderr, "error: file too small to be an RPX (%ld bytes)\n", size);
        return false;
    }

    rpx->file_data = (u8*)malloc((size_t)size);
    if (!rpx->file_data) {
        fclose(file);
        fprintf(stderr, "error: out of memory\n");
        return false;
    }

    rpx->file_size = (u32)size;
    if (fread(rpx->file_data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        rpx_free(rpx);
        fprintf(stderr, "error: failed to read '%s'\n", path);
        return false;
    }
    fclose(file);

    const u8* h = rpx->file_data;
    if (memcmp(h, "\x7f" "ELF", 4) != 0 ||
        h[4] != ELF_CLASS_32 ||
        h[5] != ELF_DATA_BE ||
        h[6] != ELF_VERSION_CURRENT) {
        fprintf(stderr, "error: '%s' is not a big-endian ELF32 RPX\n", path);
        rpx_free(rpx);
        return false;
    }

    if (read_be16(h + 16) != ELF_TYPE_RPL ||
        read_be16(h + 18) != ELF_MACHINE_PPC) {
        fprintf(stderr, "error: '%s' is not a Wii U PowerPC RPX/RPL\n", path);
        rpx_free(rpx);
        return false;
    }

    rpx->entry_point = read_be32(h + 24);
    if ((rpx->entry_point & 3u) != 0) {
        fprintf(stderr, "error: RPX entry point is not instruction-aligned\n");
        rpx_free(rpx);
        return false;
    }

    u32 section_offset = read_be32(h + 32);
    u16 section_entry_size = read_be16(h + 46);
    rpx->section_count = read_be16(h + 48);
    u16 section_names_index = read_be16(h + 50);

    if (section_entry_size != ELF_SECTION_SIZE ||
        section_names_index >= rpx->section_count) {
        fprintf(stderr, "error: unsupported RPX section table\n");
        rpx_free(rpx);
        return false;
    }

    u32 table_size = (u32)section_entry_size * rpx->section_count;
    if (!range_fits(section_offset, table_size, rpx->file_size)) {
        fprintf(stderr, "error: RPX section table is outside the file\n");
        rpx_free(rpx);
        return false;
    }

    RPXSectionHeader* sections =
        (RPXSectionHeader*)calloc(rpx->section_count, sizeof(RPXSectionHeader));
    if (!sections) {
        fprintf(stderr, "error: out of memory\n");
        rpx_free(rpx);
        return false;
    }

    for (u16 i = 0; i < rpx->section_count; i++) {
        read_section_header(rpx, section_offset, section_entry_size, i, &sections[i]);
    }

    const u8* names = NULL;
    u8* owned_names = NULL;
    u32 names_size = 0;
    if (!decode_section_bytes(rpx, &sections[section_names_index],
                              "<section-names>", &names, &owned_names,
                              &names_size)) {
        free(sections);
        rpx_free(rpx);
        return false;
    }

    for (u16 i = 0; i < rpx->section_count; i++) {
        RPXSectionHeader* section = &sections[i];
        const char* name = section_name_at(names, names_size, section->name_offset);

        if (section->type != SHT_NOBITS && section->size &&
            !range_fits(section->offset, section->size, rpx->file_size)) {
            fprintf(stderr, "error: RPX section %u is outside the file\n", i);
            free(owned_names);
            free(sections);
            rpx_free(rpx);
            return false;
        }

        if (!add_load_section(rpx, section, name)) {
            free(owned_names);
            free(sections);
            rpx_free(rpx);
            return false;
        }

        if (section->type == SHT_PROGBITS &&
            (section->flags & SHF_EXECINSTR) &&
            section->size != 0) {
            if (rpx->code_section_count >= RPX_MAX_CODE_SECTIONS) {
                fprintf(stderr, "error: too many RPX executable sections\n");
                free(owned_names);
                free(sections);
                rpx_free(rpx);
                return false;
            }

            RPXLoadSection* load = &rpx->load_sections[rpx->load_section_count - 1u];
            const u8* data = load->data;
            u32 decoded_size = load->size;

            if ((decoded_size & 3u) != 0) {
                fprintf(stderr,
                        "error: executable RPX section '%s' size is not instruction-aligned\n",
                        name ? name : "<unknown>");
                free(owned_names);
                free(sections);
                rpx_free(rpx);
                return false;
            }

            RPXCodeSection* code = &rpx->code_sections[rpx->code_section_count++];
            copy_section_name(code->name, sizeof(code->name), name);
            code->offset = section->offset;
            code->address = section->address;
            code->size = decoded_size;
            code->flags = section->flags;
            code->compressed = (section->flags & SHF_RPL_ZLIB) != 0;
            code->data = data;
            code->owned_data = NULL;
        }
    }

    if (!parse_symbol_table(rpx, sections, rpx->section_count, names, names_size)) {
        free(owned_names);
        free(sections);
        rpx_free(rpx);
        return false;
    }

    if (!apply_relocations(rpx, sections, rpx->section_count, names,
                           names_size)) {
        free(owned_names);
        free(sections);
        rpx_free(rpx);
        return false;
    }

    free(owned_names);
    free(sections);

    if (rpx->code_section_count == 0) {
        fprintf(stderr, "error: RPX has no executable code sections\n");
        rpx_free(rpx);
        return false;
    }

    int entry_in_code = 0;
    for (u32 i = 0; i < rpx->code_section_count; i++) {
        const RPXCodeSection* section = &rpx->code_sections[i];
        if (address_in_range(rpx->entry_point, section->address, section->size)) {
            entry_in_code = 1;
            break;
        }
    }
    if (!entry_in_code) {
        fprintf(stderr,
                "error: RPX entry point 0x%08X is not inside an executable section\n",
                rpx->entry_point);
        rpx_free(rpx);
        return false;
    }

    return true;
}

void rpx_free(RPXFile* rpx) {
    for (u32 i = 0; i < rpx->code_section_count; i++) {
        free(rpx->code_sections[i].owned_data);
    }
    for (u32 i = 0; i < rpx->load_section_count; i++) {
        free(rpx->load_sections[i].owned_data);
    }
    free(rpx->file_data);
    memset(rpx, 0, sizeof(*rpx));
}

void rpx_print_info(const RPXFile* rpx, const char* game_name) {
    printf("=== RPX Info ===\n");
    printf("entry point: 0x%08X\n", rpx->entry_point);
    if (game_name && game_name[0] != '\0')
        printf("game: %s\n", game_name);
    printf("sections: %u\n", rpx->section_count);
    printf("loadable sections: %u\n", rpx->load_section_count);
    printf("symbols: %u\n", rpx->symbol_count);
    printf("relocations: %u applied, %u import aliases\n",
           rpx->applied_relocation_count, rpx->import_alias_count);
    printf("\n");

    printf("code sections:\n");
    for (u32 i = 0; i < rpx->code_section_count; i++) {
        const RPXCodeSection* section = &rpx->code_sections[i];
        printf("  [%u] %-20s file:0x%08X -> addr:0x%08X  size:0x%08X%s\n",
               i, section->name, section->offset, section->address,
               section->size, section->compressed ? " compressed" : "");
    }
}
