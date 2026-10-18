#include "ir/dolir.h"

#include <string.h>

static void mark(u64* mask, DolIRStateSlot slot) {
    mask[(u32)slot / 64u] |= 1ull << ((u32)slot & 63u);
}

bool dolir_state_mask_test(const u64* mask, DolIRStateSlot slot) {
    return (mask[(u32)slot / 64u] & (1ull << ((u32)slot & 63u))) != 0;
}

static void use_pair(DolIRInstruction* instruction, u32 reg) {
    mark(instruction->state_uses,
         (DolIRStateSlot)(DOLIR_STATE_FPR0 + reg));
    mark(instruction->state_uses,
         (DolIRStateSlot)(DOLIR_STATE_PS1_0 + reg));
}

static void define_pair(DolIRInstruction* instruction, u32 reg) {
    mark(instruction->state_defs,
         (DolIRStateSlot)(DOLIR_STATE_FPR0 + reg));
    mark(instruction->state_defs,
         (DolIRStateSlot)(DOLIR_STATE_PS1_0 + reg));
}

static void exact_float(DolIRInstruction* instruction) {
    u64 descriptor = instruction->immediate;
    DolIRExactFloat op = (DolIRExactFloat)(descriptor & 0xffu);
    u32 d = (descriptor >> 8) & 0xffu;
    u32 a = (descriptor >> 16) & 0xffu;
    u32 b = (descriptor >> 24) & 0xffu;
    u32 c = (descriptor >> 32) & 0xffu;
    u32 crfd = (descriptor >> 40) & 0xffu;
    instruction->exact_fp = true;
    mark(instruction->state_uses, DOLIR_STATE_FPSCR);
    mark(instruction->state_defs, DOLIR_STATE_FPSCR);
    if (op == DOLIR_EXACT_FCMPU || op == DOLIR_EXACT_FCMPO) {
        mark(instruction->state_uses, (DolIRStateSlot)(DOLIR_STATE_FPR0 + a));
        mark(instruction->state_uses, (DolIRStateSlot)(DOLIR_STATE_FPR0 + b));
        mark(instruction->state_uses,
             (DolIRStateSlot)(DOLIR_STATE_CR0 + crfd));
        mark(instruction->state_defs,
             (DolIRStateSlot)(DOLIR_STATE_CR0 + crfd));
        return;
    }
    mark(instruction->state_defs, (DolIRStateSlot)(DOLIR_STATE_FPR0 + d));
    if (op == DOLIR_EXACT_FRES ||
        (op >= DOLIR_EXACT_FADDS && op <= DOLIR_EXACT_FDIVS) ||
        op == DOLIR_EXACT_FRSP ||
        (op >= DOLIR_EXACT_FMADDS && op <= DOLIR_EXACT_FNMSUBS))
        mark(instruction->state_defs,
             (DolIRStateSlot)(DOLIR_STATE_PS1_0 + d));
    if (op == DOLIR_EXACT_FRES || op == DOLIR_EXACT_FRSQRTE ||
        op == DOLIR_EXACT_FCTIW || op == DOLIR_EXACT_FCTIWZ ||
        op == DOLIR_EXACT_FRSP) {
        mark(instruction->state_uses,
             (DolIRStateSlot)(DOLIR_STATE_FPR0 + b));
    } else if (op == DOLIR_EXACT_FMULS || op == DOLIR_EXACT_FMUL) {
        mark(instruction->state_uses,
             (DolIRStateSlot)(DOLIR_STATE_FPR0 + a));
        mark(instruction->state_uses,
             (DolIRStateSlot)(DOLIR_STATE_FPR0 + c));
    } else {
        mark(instruction->state_uses,
             (DolIRStateSlot)(DOLIR_STATE_FPR0 + a));
        mark(instruction->state_uses,
             (DolIRStateSlot)(DOLIR_STATE_FPR0 + b));
        if (op >= DOLIR_EXACT_FMADD && op <= DOLIR_EXACT_FNMSUBS)
            mark(instruction->state_uses,
                 (DolIRStateSlot)(DOLIR_STATE_FPR0 + c));
    }
}

static void exact_paired(DolIRInstruction* instruction) {
    u64 descriptor = instruction->immediate;
    DolIRExactPaired op = (DolIRExactPaired)(descriptor & 0xffu);
    u32 d = (descriptor >> 8) & 0xffu;
    u32 a = (descriptor >> 16) & 0xffu;
    u32 b = (descriptor >> 24) & 0xffu;
    u32 c = (descriptor >> 32) & 0xffu;
    u32 crfd = (descriptor >> 40) & 0xffu;
    instruction->exact_fp = true;
    mark(instruction->state_uses, DOLIR_STATE_FPSCR);
    mark(instruction->state_defs, DOLIR_STATE_FPSCR);
    if (op >= DOLIR_EXACT_PS_CMPU0) {
        use_pair(instruction, a);
        use_pair(instruction, b);
        mark(instruction->state_uses,
             (DolIRStateSlot)(DOLIR_STATE_CR0 + crfd));
        mark(instruction->state_defs,
             (DolIRStateSlot)(DOLIR_STATE_CR0 + crfd));
        return;
    }
    define_pair(instruction, d);
    if (op == DOLIR_EXACT_PS_RES || op == DOLIR_EXACT_PS_RSQRTE) {
        use_pair(instruction, b);
        return;
    }
    use_pair(instruction, a);
    if (op == DOLIR_EXACT_PS_MUL || op == DOLIR_EXACT_PS_MULS0 ||
        op == DOLIR_EXACT_PS_MULS1) {
        use_pair(instruction, c);
        return;
    }
    use_pair(instruction, b);
    if (op >= DOLIR_EXACT_PS_MADD && op <= DOLIR_EXACT_PS_SUM1)
        use_pair(instruction, c);
}

static void helper_effects(DolIRInstruction* instruction) {
    switch ((DolIRHelper)instruction->aux) {
    case DOLIR_HELPER_FP_AVAILABLE:
        mark(instruction->state_uses, DOLIR_STATE_MSR);
        break;
    case DOLIR_HELPER_EXACT_FLOAT:
        exact_float(instruction);
        break;
    case DOLIR_HELPER_EXACT_PAIRED:
        exact_paired(instruction);
        break;
    case DOLIR_HELPER_PSQ_LOAD:
    case DOLIR_HELPER_PSQ_STORE: {
        u32 reg = instruction->immediate & 0xffu;
        u32 gqr = (instruction->immediate >> 9) & 7u;
        mark(instruction->state_uses,
             (DolIRStateSlot)(DOLIR_STATE_GQR0 + gqr));
        mark(instruction->state_uses, DOLIR_STATE_HID2);
        use_pair(instruction, reg);
        if (instruction->aux == DOLIR_HELPER_PSQ_LOAD)
            define_pair(instruction, reg);
        else {
            mark(instruction->state_uses, DOLIR_STATE_RESERVE_ADDR);
            mark(instruction->state_uses, DOLIR_STATE_RESERVE_VALID);
            mark(instruction->state_defs, DOLIR_STATE_RESERVE_VALID);
        }
        break;
    }
    case DOLIR_HELPER_STORE_CONDITIONAL:
        mark(instruction->state_uses, DOLIR_STATE_CR0);
        mark(instruction->state_defs, DOLIR_STATE_CR0);
        mark(instruction->state_uses, DOLIR_STATE_RESERVE_ADDR);
        mark(instruction->state_uses, DOLIR_STATE_RESERVE_VALID);
        mark(instruction->state_defs, DOLIR_STATE_RESERVE_VALID);
        break;
    case DOLIR_HELPER_FPSCR_UPDATED:
    case DOLIR_HELPER_FPSCR_BIT:
        mark(instruction->state_uses, DOLIR_STATE_FPSCR);
        mark(instruction->state_defs, DOLIR_STATE_FPSCR);
        break;
    case DOLIR_HELPER_LSWX:
        mark(instruction->state_uses, DOLIR_STATE_XER);
        for (u32 reg = 0; reg < 32; reg++) {
            mark(instruction->state_uses,
                 (DolIRStateSlot)(DOLIR_STATE_GPR0 + reg));
            mark(instruction->state_defs,
                 (DolIRStateSlot)(DOLIR_STATE_GPR0 + reg));
        }
        break;
    default:
        break;
    }
}

void dolir_populate_effects(DolIRInstruction* instruction) {
    memset(instruction->state_uses, 0, sizeof(instruction->state_uses));
    memset(instruction->state_defs, 0, sizeof(instruction->state_defs));
    instruction->exact_fp = false;
    if (instruction->op == DOLIR_OP_STATE_READ)
        mark(instruction->state_uses, (DolIRStateSlot)instruction->aux);
    else if (instruction->op == DOLIR_OP_STATE_WRITE)
        mark(instruction->state_defs, (DolIRStateSlot)instruction->aux);
    else if (instruction->op == DOLIR_OP_HELPER_CALL)
        helper_effects(instruction);
    if (instruction->op == DOLIR_OP_GUEST_STORE) {
        mark(instruction->state_uses, DOLIR_STATE_RESERVE_ADDR);
        mark(instruction->state_uses, DOLIR_STATE_RESERVE_VALID);
        mark(instruction->state_defs, DOLIR_STATE_RESERVE_VALID);
    }
}
