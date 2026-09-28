#include "wiiu_cpu.h"

void wiiu_instruction_fallback(CPUState* cpu, u32 raw, u32 cia) {
    u32 xo = (raw >> 1u) & 0x3FFu;
    if ((raw >> 26u) != 31u || (raw & 1u)) {
        ppc_program_exception(cpu, PPC_PROGRAM_ILLEGAL, cia);
        return;
    }
    if (xo == 467u || xo == 339u) {
        u32 spr = ((raw >> 16u) & 31u) | ((raw >> 6u) & 992u);
        u32 reg = (raw >> 21u) & 31u;
        /* Espresso UGQR0..7 (896..903) are the user-accessible quantization
           registers consumed by paired-single loads/stores. The standalone
           CPU's gqr[] backs those operations; a separate SPR shadow would
           silently leave vertex unpacking in the wrong numeric format.
           See the local Cemu Espresso Interpreter SPR_UGQR0..7 definitions. */
        if (spr >= 896u && spr <= 903u) {
            if (xo == 467u) cpu->gqr[spr - 896u] = cpu->gpr[reg];
            else cpu->gpr[reg] = cpu->gqr[spr - 896u];
            cpu->pc = cia + 4u;
            return;
        }
    }
    u32 ra = (raw >> 16u) & 31u, rb = (raw >> 11u) & 31u;
    u32 ea = (ra ? cpu->gpr[ra] : 0u) + cpu->gpr[rb];
    u8 operation;
    switch (xo) {
    case 54u: operation = PPC_CACHE_DCBST; break;
    case 86u: operation = PPC_CACHE_DCBF; break;
    case 470u: operation = PPC_CACHE_DCBI; break;
    case 982u: operation = PPC_CACHE_ICBI; break;
    default:
        ppc_program_exception(cpu, PPC_PROGRAM_ILLEGAL, cia);
        return;
    }
    ppc_cache_control(cpu, operation, ea, cia);
    if (!cpu->exception) cpu->pc = cia + 4u;
}
