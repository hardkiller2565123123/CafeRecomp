#include "backend/codegen.h"
#include "analysis/embedded_data.h"
#include "frontend/container/dol.h"
#include "ir/dolir_builder.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: dolir_stats <main.dol> [unknown.csv]\n");
        return 1;
    }
    FILE* unknown_report = NULL;
    if (argc == 3) {
        unknown_report = fopen(argv[2], "w");
        if (!unknown_report) {
            fprintf(stderr, "error: can't open output '%s'\n", argv[2]);
            return 1;
        }
        fprintf(unknown_report, "address,raw,primary\n");
    }
    DOLFile dol;
    if (!dol_load(&dol, argv[1])) {
        if (unknown_report)
            fclose(unknown_report);
        return 1;
    }
    u64 opcode_count[PPC_OP_COUNT] = {0};
    u64 fallback_count[PPC_OP_COUNT] = {0};
    u64 mfspr_count[1024] = {0};
    u64 mtspr_count[1024] = {0};
    u64 total = 0;
    u64 fallback = 0;
    u64 embedded = 0;
    u64 unknown = 0;
    for (u32 section = 0; section < DOL_NUM_TEXT; section++) {
        const u8* data = dol_get_text_section(&dol, section);
        u32 size = dol.header.text_sizes[section];
        if (!data || !size)
            continue;
        u32 count = size / 4u;
        PPCInst* insts = (PPCInst*)malloc((size_t)count * sizeof(*insts));
        if (!insts) {
            dol_free(&dol);
            return 1;
        }
        for (u32 i = 0; i < count; i++) {
            insts[i] = ppc_decode(read_be32(data + i * 4u),
                                  dol.header.text_addresses[section] + i * 4u);
            if (insts[i].op == PPC_OP_UNKNOWN &&
                dol_embedded_data_word(insts[i].raw))
                insts[i].embedded_data = true;
            if (insts[i].embedded_data) {
                embedded++;
            } else if (insts[i].op == PPC_OP_UNKNOWN) {
                unknown++;
                if (unknown_report)
                    fprintf(unknown_report, "%08X,%08X,%u\n",
                            insts[i].address, insts[i].raw,
                            insts[i].raw >> 26);
            }
        }
        for (u32 start = 0; start < count; start += EMIT_CHUNK_INSTRUCTIONS) {
            u32 chunk = count - start;
            if (chunk > EMIT_CHUNK_INSTRUCTIONS)
                chunk = EMIT_CHUNK_INSTRUCTIONS;
            DolIRModule module;
            dolir_module_init(&module);
            if (!dolir_build_chunk(&module, insts + start, chunk,
                                   insts[start].address)) {
                free(insts);
                dol_free(&dol);
                return 1;
            }
            for (u32 i = 0; i < chunk; i++) {
                PPCOpcode op = insts[start + i].op;
                if (op >= PPC_OP_COUNT || insts[start + i].embedded_data)
                    continue;
                total++;
                opcode_count[op]++;
                if (op == PPC_OP_MFSPR && insts[start + i].spr < 1024)
                    mfspr_count[insts[start + i].spr]++;
                if (op == PPC_OP_MTSPR && insts[start + i].spr < 1024)
                    mtspr_count[insts[start + i].spr]++;
                if (module.functions[0].blocks[i].terminator.kind == DOLIR_TERM_FALLBACK) {
                    fallback++;
                    fallback_count[op]++;
                }
            }
            dolir_module_free(&module);
        }
        free(insts);
    }
    printf("instructions: %llu\n", (unsigned long long)total);
    printf("embedded data: %llu\n", (unsigned long long)embedded);
    printf("unknown: %llu\n", (unsigned long long)unknown);
    printf("native: %llu (%.2f%%)\n", (unsigned long long)(total - fallback),
           total ? 100.0 * (double)(total - fallback) / (double)total : 0.0);
    printf("fallback: %llu (%.2f%%)\n", (unsigned long long)fallback,
           total ? 100.0 * (double)fallback / (double)total : 0.0);
    for (u32 op = 0; op < PPC_OP_COUNT; op++)
        if (fallback_count[op])
            printf("%-12s %10llu / %10llu\n", ppc_op_name((PPCOpcode)op),
                   (unsigned long long)fallback_count[op],
                   (unsigned long long)opcode_count[op]);
    for (u32 spr = 0; spr < 1024; spr++)
        if (mfspr_count[spr] || mtspr_count[spr])
            printf("spr %-4u read=%-8llu write=%-8llu\n", spr,
                   (unsigned long long)mfspr_count[spr],
                   (unsigned long long)mtspr_count[spr]);
    if (unknown_report && fclose(unknown_report) != 0) {
        fprintf(stderr, "error: failed writing '%s'\n", argv[2]);
        dol_free(&dol);
        return 1;
    }
    dol_free(&dol);
    return 0;
}
