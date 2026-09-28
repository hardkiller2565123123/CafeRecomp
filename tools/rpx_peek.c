#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rpx_runtime.h"

static const RPXLoadSection* find_section(const RPXFile* rpx, u32 address) {
    for (u32 i = 0; i < rpx->load_section_count; i++) {
        const RPXLoadSection* section = &rpx->load_sections[i];
        if (address >= section->address &&
            address - section->address < section->size) {
            return section;
        }
    }
    return NULL;
}

static int parse_address(const char* value, u32* address) {
    char* end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(value, &end, 0);
    if (errno != 0 || !end || *end != '\0' || parsed > 0xFFFFFFFFul)
        return 0;
    *address = (u32)parsed;
    return 1;
}

static void print_address(const RPXFile* rpx, u32 address) {
    const RPXLoadSection* section = find_section(rpx, address);
    if (!section || !section->data) {
        printf("0x%08X: unmapped\n", address);
        return;
    }

    u32 offset = address - section->address;
    u32 available = section->size - offset;
    u32 count = available < 64u ? available : 64u;
    printf("0x%08X %s+0x%X:", address, section->name, offset);
    for (u32 i = 0; i < count; i++)
        printf(" %02X", section->data[offset + i]);
    printf("\n  text: ");
    for (u32 i = 0; i < available && i < 256u; i++) {
        u8 ch = section->data[offset + i];
        if (ch == 0)
            break;
        putchar(ch >= 32u && ch < 127u ? (int)ch : '.');
    }
    putchar('\n');
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <game.rpx> <address> [address ...]\n"
                "       %s <game.rpx> --symbols [name-filter]\n",
                argv[0], argv[0]);
        return 2;
    }

    RPXFile rpx;
    if (!rpx_load(&rpx, argv[1]))
        return 1;

    if (strcmp(argv[2], "--symbols") == 0) {
        const char* filter = argc >= 4 ? argv[3] : NULL;
        for (u32 i = 0; i < rpx.symbol_count; i++) {
            const RPXSymbol* symbol = &rpx.symbols[i];
            if (filter && !strstr(symbol->name, filter))
                continue;
            printf("0x%08X section=0x%04X size=0x%X %s\n",
                   symbol->value, symbol->section_index, symbol->size,
                   symbol->name);
        }
        rpx_free(&rpx);
        return 0;
    }

    int result = 0;
    for (int i = 2; i < argc; i++) {
        u32 address = 0;
        if (!parse_address(argv[i], &address)) {
            fprintf(stderr, "invalid address: %s\n", argv[i]);
            result = 2;
            continue;
        }
        print_address(&rpx, address);
    }

    rpx_free(&rpx);
    return result;
}
