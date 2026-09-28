#include "ir/dolir_builder.h"

#include <string.h>

typedef struct {
    DolIRFunction* function;
    DolIRBlock* block;
    const PPCInst* inst;
} Builder;

static DolIRValue c32(Builder* b, u32 value) {
    return dolir_constant(b->function, b->block, DOLIR_TYPE_I32, value,
                          b->inst->address);
}

static DolIRValue c64(Builder* b, u64 value) {
    return dolir_constant(b->function, b->block, DOLIR_TYPE_I64, value,
                          b->inst->address);
}

static DolIRValue c1(Builder* b, bool value) {
    return dolir_constant(b->function, b->block, DOLIR_TYPE_I1, value,
                          b->inst->address);
}

static DolIRValue read_slot(Builder* b, DolIRStateSlot slot) {
    return dolir_state_read(b->function, b->block, slot, b->inst->address);
}

static void write_slot(Builder* b, DolIRStateSlot slot, DolIRValue value) {
    dolir_state_write(b->function, b->block, slot, value, b->inst->address);
}

static DolIRValue gpr(Builder* b, u8 reg) {
    return read_slot(b, (DolIRStateSlot)(DOLIR_STATE_GPR0 + reg));
}

static void set_gpr(Builder* b, u8 reg, DolIRValue value) {
    write_slot(b, (DolIRStateSlot)(DOLIR_STATE_GPR0 + reg), value);
}

static DolIRValue fpr(Builder* b, u8 reg) {
    return read_slot(b, (DolIRStateSlot)(DOLIR_STATE_FPR0 + reg));
}

static DolIRValue ps1(Builder* b, u8 reg) {
    return read_slot(b, (DolIRStateSlot)(DOLIR_STATE_PS1_0 + reg));
}

static void set_fpr(Builder* b, u8 reg, DolIRValue value) {
    write_slot(b, (DolIRStateSlot)(DOLIR_STATE_FPR0 + reg), value);
}

static void set_ps1(Builder* b, u8 reg, DolIRValue value) {
    write_slot(b, (DolIRStateSlot)(DOLIR_STATE_PS1_0 + reg), value);
}

static DolIRValue emit_op(Builder* b, DolIROp op, DolIRType type,
                          DolIRValue a, DolIRValue c) {
    DolIRValue operands[2] = {a, c};
    return dolir_append(b->function, b->block, op, type, operands,
                        c ? 2 : 1, 0, 0, b->inst->address, DOLIR_EFFECT_NONE);
}

static DolIRValue binary(Builder* b, DolIROp op, DolIRType type,
                         DolIRValue a, DolIRValue c) {
    return emit_op(b, op, type, a, c);
}

static DolIRValue unary(Builder* b, DolIROp op, DolIRType type, DolIRValue a) {
    return emit_op(b, op, type, a, 0);
}

static DolIRValue select_value(Builder* b, DolIRType type, DolIRValue condition,
                               DolIRValue yes, DolIRValue no) {
    DolIRValue operands[3] = {condition, yes, no};
    return dolir_append(b->function, b->block, DOLIR_OP_SELECT, type, operands, 3,
                        0, 0, b->inst->address, DOLIR_EFFECT_NONE);
}

static DolIRValue trunc_value(Builder* b, DolIRType type, DolIRValue value) {
    return unary(b, DOLIR_OP_TRUNC, type, value);
}

static DolIRValue zext_value(Builder* b, DolIRType type, DolIRValue value) {
    return unary(b, DOLIR_OP_ZEXT, type, value);
}

static DolIRValue sext_value(Builder* b, DolIRType type, DolIRValue value) {
    return unary(b, DOLIR_OP_SEXT, type, value);
}

static DolIRValue insert_masked(Builder* b, DolIRValue original,
                                DolIRValue value, u32 mask) {
    DolIRValue kept = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32, original, c32(b, ~mask));
    DolIRValue incoming = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32, value, c32(b, mask));
    return binary(b, DOLIR_OP_OR, DOLIR_TYPE_I32, kept, incoming);
}

static u32 ppc_mask32(u8 mb, u8 me) {
    u32 mask = 0;
    u8 bit = mb;
    for (;;) {
        mask |= 0x80000000u >> bit;
        if (bit == me)
            return mask;
        bit = (u8)((bit + 1u) & 31u);
    }
}

static DolIRStateSlot cr_field_slot(u8 field) {
    return (DolIRStateSlot)(DOLIR_STATE_CR0 + field);
}

static DolIRValue read_cr_field(Builder* b, u8 field) {
    return read_slot(b, cr_field_slot(field));
}

static void write_cr_field(Builder* b, u8 field, DolIRValue value) {
    write_slot(b, cr_field_slot(field),
               binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32, value, c32(b, 0xFu)));
}

static DolIRValue pack_cr(Builder* b) {
    DolIRValue packed = c32(b, 0);
    for (u8 field = 0; field < 8; field++) {
        DolIRValue value = binary(b, DOLIR_OP_SHL, DOLIR_TYPE_I32,
                                  read_cr_field(b, field),
                                  c32(b, 28u - 4u * field));
        packed = binary(b, DOLIR_OP_OR, DOLIR_TYPE_I32, packed, value);
    }
    return packed;
}

static DolIRValue pack_xer(Builder* b) {
    DolIRValue packed = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32,
                               read_slot(b, DOLIR_STATE_XER),
                               c32(b, ~0xE0000000u));
    const DolIRStateSlot slots[3] = {
        DOLIR_STATE_XER_CA, DOLIR_STATE_XER_OV, DOLIR_STATE_XER_SO,
    };
    const u32 shifts[3] = {29, 30, 31};
    for (u32 index = 0; index < 3; index++) {
        DolIRValue bit = zext_value(b, DOLIR_TYPE_I32,
                                    read_slot(b, slots[index]));
        bit = binary(b, DOLIR_OP_SHL, DOLIR_TYPE_I32, bit,
                     c32(b, shifts[index]));
        packed = binary(b, DOLIR_OP_OR, DOLIR_TYPE_I32, packed, bit);
    }
    return packed;
}

static void unpack_xer(Builder* b, DolIRValue packed) {
    write_slot(b, DOLIR_STATE_XER,
               binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32, packed,
                      c32(b, ~0xE0000000u)));
    write_slot(b, DOLIR_STATE_XER_CA,
               trunc_value(b, DOLIR_TYPE_I1,
                           binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I32, packed,
                                  c32(b, 29))));
    write_slot(b, DOLIR_STATE_XER_OV,
               trunc_value(b, DOLIR_TYPE_I1,
                           binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I32, packed,
                                  c32(b, 30))));
    write_slot(b, DOLIR_STATE_XER_SO,
               trunc_value(b, DOLIR_TYPE_I1,
                           binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I32, packed,
                                  c32(b, 31))));
}

static void set_cr_field(Builder* b, u8 field, DolIRValue lhs,
                         DolIRValue rhs, bool is_signed) {
    DolIROp lt_op = is_signed ? DOLIR_OP_ICMP_SLT : DOLIR_OP_ICMP_ULT;
    DolIRValue lt = binary(b, lt_op, DOLIR_TYPE_I1, lhs, rhs);
    DolIRValue gt = binary(b, lt_op, DOLIR_TYPE_I1, rhs, lhs);
    DolIRValue eq = binary(b, DOLIR_OP_ICMP_EQ, DOLIR_TYPE_I1, lhs, rhs);
    DolIRValue bits = select_value(b, DOLIR_TYPE_I32, lt, c32(b, 8), c32(b, 0));
    bits = binary(b, DOLIR_OP_OR, DOLIR_TYPE_I32, bits,
                  select_value(b, DOLIR_TYPE_I32, gt, c32(b, 4), c32(b, 0)));
    bits = binary(b, DOLIR_OP_OR, DOLIR_TYPE_I32, bits,
                  select_value(b, DOLIR_TYPE_I32, eq, c32(b, 2), c32(b, 0)));
    bits = binary(b, DOLIR_OP_OR, DOLIR_TYPE_I32, bits,
                  zext_value(b, DOLIR_TYPE_I32,
                             read_slot(b, DOLIR_STATE_XER_SO)));
    write_cr_field(b, field, bits);
}

static void set_cr0(Builder* b, DolIRValue value) {
    set_cr_field(b, 0, value, c32(b, 0), true);
}

static void set_cr1_from_fpscr(Builder* b) {
    DolIRValue bits = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32,
        binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I32,
               read_slot(b, DOLIR_STATE_FPSCR), c32(b, 28)),
        c32(b, 0xFu));
    write_cr_field(b, 1, bits);
}

static void fpscr_updated(Builder* b) {
    dolir_append(b->function, b->block, DOLIR_OP_HELPER_CALL,
                 DOLIR_TYPE_VOID, NULL, 0, 0,
                 DOLIR_HELPER_FPSCR_UPDATED, b->inst->address,
                 DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                 DOLIR_EFFECT_BARRIER);
}

static void require_supervisor(Builder* b) {
    DolIRValue user = binary(b, DOLIR_OP_ICMP_NE, DOLIR_TYPE_I1,
        binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32,
               read_slot(b, DOLIR_STATE_MSR), c32(b, 1u << 14)),
        c32(b, 0));
    dolir_append(b->function, b->block, DOLIR_OP_HELPER_CALL,
                 DOLIR_TYPE_VOID, &user, 1, DOLIR_PROGRAM_PRIV,
                 DOLIR_HELPER_PROGRAM_EXCEPTION, b->inst->address,
                 DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                 DOLIR_EFFECT_MAY_EXIT | DOLIR_EFFECT_MAY_RAISE |
                 DOLIR_EFFECT_BARRIER);
}

static void record_if_needed(Builder* b, DolIRValue value) {
    if (b->inst->rc)
        set_cr0(b, value);
}

static void set_xer_ca(Builder* b, DolIRValue carry) {
    write_slot(b, DOLIR_STATE_XER_CA, carry);
}

static DolIRValue xer_ca(Builder* b) {
    return zext_value(b, DOLIR_TYPE_I32,
                      read_slot(b, DOLIR_STATE_XER_CA));
}

static DolIRValue add_wide_with_carry(Builder* b, DolIRValue a,
                                      DolIRValue c, DolIRValue carry) {
    DolIRValue wide = binary(b, DOLIR_OP_ADD, DOLIR_TYPE_I64,
                             zext_value(b, DOLIR_TYPE_I64, a),
                             zext_value(b, DOLIR_TYPE_I64, c));
    if (carry)
        wide = binary(b, DOLIR_OP_ADD, DOLIR_TYPE_I64, wide,
                      zext_value(b, DOLIR_TYPE_I64, carry));
    return wide;
}

static void write_carry_result(Builder* b, u8 reg, DolIRValue wide,
                               bool record) {
    DolIRValue result = trunc_value(b, DOLIR_TYPE_I32, wide);
    DolIRValue carry = trunc_value(b, DOLIR_TYPE_I1,
        binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I64, wide, c64(b, 32)));
    set_gpr(b, reg, result);
    set_xer_ca(b, carry);
    if (record)
        set_cr0(b, result);
}

static DolIRValue branch_condition(Builder* b) {
    DolIRValue ctr_ok = c1(b, true);
    DolIRValue cr_ok = c1(b, true);
    if ((b->inst->bo & 0x04u) == 0) {
        DolIRValue ctr = binary(b, DOLIR_OP_SUB, DOLIR_TYPE_I32,
                                read_slot(b, DOLIR_STATE_CTR), c32(b, 1));
        write_slot(b, DOLIR_STATE_CTR, ctr);
        DolIRValue nz = binary(b, DOLIR_OP_ICMP_NE, DOLIR_TYPE_I1, ctr, c32(b, 0));
        ctr_ok = ((b->inst->bo >> 1) & 1u) ? unary(b, DOLIR_OP_NOT, DOLIR_TYPE_I1, nz) : nz;
    }
    if ((b->inst->bo & 0x10u) == 0) {
        DolIRValue field = read_cr_field(b, b->inst->bi / 4u);
        DolIRValue bit = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32, field,
                                c32(b, 8u >> (b->inst->bi & 3u)));
        DolIRValue set = binary(b, DOLIR_OP_ICMP_NE, DOLIR_TYPE_I1, bit, c32(b, 0));
        cr_ok = ((b->inst->bo >> 3) & 1u) ? set : unary(b, DOLIR_OP_NOT, DOLIR_TYPE_I1, set);
    }
    return binary(b, DOLIR_OP_AND, DOLIR_TYPE_I1, ctr_ok, cr_ok);
}

static DolIRValue effective_address(Builder* b, bool indexed, bool update) {
    DolIRValue base = (b->inst->rA == 0 && !update) ? c32(b, 0) : gpr(b, b->inst->rA);
    DolIRValue offset = indexed ? gpr(b, b->inst->rB) : c32(b, (u32)(s32)b->inst->simm);
    return binary(b, DOLIR_OP_ADD, DOLIR_TYPE_I32, base, offset);
}

static DolIRValue guest_load_typed(Builder* b, DolIRValue address,
                                   DolIRType type, u8 width, bool sign) {
    return dolir_append(b->function, b->block, DOLIR_OP_GUEST_LOAD,
                        type, &address, 1, 0,
                        (u32)width | (sign ? 0x100u : 0u), b->inst->address,
                        DOLIR_EFFECT_READ_MEMORY | DOLIR_EFFECT_MAY_EXIT);
}

static DolIRValue guest_load(Builder* b, DolIRValue address, u8 width, bool sign) {
    return guest_load_typed(b, address, DOLIR_TYPE_I32, width, sign);
}

static void guest_store(Builder* b, DolIRValue address, DolIRValue value, u8 width) {
    DolIRValue operands[2] = {address, value};
    dolir_append(b->function, b->block, DOLIR_OP_GUEST_STORE,
                 DOLIR_TYPE_VOID, operands, 2, 0, width, b->inst->address,
                 DOLIR_EFFECT_WRITE_MEMORY | DOLIR_EFFECT_MAY_EXIT);
}

static DolIRValue pair_value(Builder* b, u8 reg) {
    return binary(b, DOLIR_OP_VECTOR_BUILD, DOLIR_TYPE_V2F64,
                  fpr(b, reg), ps1(b, reg));
}

static void set_pair(Builder* b, u8 reg, DolIRValue pair) {
    DolIRValue a_ops[1] = {pair};
    DolIRValue a = dolir_append(b->function, b->block, DOLIR_OP_VECTOR_EXTRACT,
                                DOLIR_TYPE_F64, a_ops, 1, 0, 0,
                                b->inst->address, DOLIR_EFFECT_NONE);
    DolIRValue c = dolir_append(b->function, b->block, DOLIR_OP_VECTOR_EXTRACT,
                                DOLIR_TYPE_F64, a_ops, 1, 0, 1,
                                b->inst->address, DOLIR_EFFECT_NONE);
    set_fpr(b, reg, a);
    set_ps1(b, reg, c);
}

u32 dolir_instruction_cycle_cost(const PPCInst* inst) {
    if (inst->embedded_data)
        return 0;
    switch (inst->op) {
    case PPC_OP_UNKNOWN: case PPC_OP_DCBST: case PPC_OP_DCBF:
    case PPC_OP_DCBI: case PPC_OP_ICBI: return 0;
    case PPC_OP_MULLI: return 3;
    case PPC_OP_SC: case PPC_OP_RFI: case PPC_OP_TW: return 2;
    case PPC_OP_LMW: case PPC_OP_STMW: return 11;
    case PPC_OP_MULLW: case PPC_OP_MULLWO: case PPC_OP_MULHW:
    case PPC_OP_MULHWU: return 5;
    case PPC_OP_DIVW: case PPC_OP_DIVWO: case PPC_OP_DIVWU:
    case PPC_OP_DIVWUO: return 40;
    case PPC_OP_DCBZ: return 5;
    case PPC_OP_DCBTST: case PPC_OP_DCBT: return 2;
    case PPC_OP_MFSR: case PPC_OP_MFSRIN: return 3;
    case PPC_OP_MTSPR: return 2;
    case PPC_OP_SYNC: return 3;
    case PPC_OP_MTFSB0: case PPC_OP_MTFSB1: case PPC_OP_MTFSF:
    case PPC_OP_MTFSFI: return 3;
    case PPC_OP_FDIVS: return 17;
    case PPC_OP_FDIV: return 31;
    case PPC_OP_PS_DIV: return 17;
    case PPC_OP_PS_RSQRTE: return 2;
    default: return 1;
    }
}

static void normal_fallthrough(Builder* b, u32 index, u32 count) {
    b->block->terminator.kind = DOLIR_TERM_FALLTHROUGH;
    b->block->terminator.targets[0] = index + 1u < count ? index + 1u : DOLIR_NO_BLOCK;
    b->block->terminator.target_addresses[0] = b->inst->address + 4u;
    b->block->terminator.guest_pc = b->inst->address;
}

static DolIRValue trap_condition(Builder* b, DolIRValue a, DolIRValue c) {
    DolIRValue condition = c1(b, false);
    if (b->inst->to & 0x10u)
        condition = binary(b, DOLIR_OP_OR, DOLIR_TYPE_I1, condition,
                           binary(b, DOLIR_OP_ICMP_SLT, DOLIR_TYPE_I1, a, c));
    if (b->inst->to & 0x08u)
        condition = binary(b, DOLIR_OP_OR, DOLIR_TYPE_I1, condition,
                           binary(b, DOLIR_OP_ICMP_SLT, DOLIR_TYPE_I1, c, a));
    if (b->inst->to & 0x04u)
        condition = binary(b, DOLIR_OP_OR, DOLIR_TYPE_I1, condition,
                           binary(b, DOLIR_OP_ICMP_EQ, DOLIR_TYPE_I1, a, c));
    if (b->inst->to & 0x02u)
        condition = binary(b, DOLIR_OP_OR, DOLIR_TYPE_I1, condition,
                           binary(b, DOLIR_OP_ICMP_ULT, DOLIR_TYPE_I1, a, c));
    if (b->inst->to & 0x01u)
        condition = binary(b, DOLIR_OP_OR, DOLIR_TYPE_I1, condition,
                           binary(b, DOLIR_OP_ICMP_ULT, DOLIR_TYPE_I1, c, a));
    return condition;
}

static bool lower_integer(Builder* b) {
    const PPCInst* i = b->inst;
    DolIRValue a, c, result;
    switch (i->op) {
    case PPC_OP_TWI:
    case PPC_OP_TW: {
        a = gpr(b, i->rA);
        c = i->op == PPC_OP_TWI ? c32(b, (u32)(s32)i->simm) : gpr(b, i->rB);
        DolIRValue condition = trap_condition(b, a, c);
        dolir_append(b->function, b->block, DOLIR_OP_HELPER_CALL,
                     DOLIR_TYPE_VOID, &condition, 1, DOLIR_PROGRAM_TRAP,
                     DOLIR_HELPER_PROGRAM_EXCEPTION, i->address,
                     DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                     DOLIR_EFFECT_MAY_EXIT | DOLIR_EFFECT_MAY_RAISE |
                     DOLIR_EFFECT_BARRIER);
        return true;
    }
    case PPC_OP_MULLI:
        a = sext_value(b, DOLIR_TYPE_I64, gpr(b, i->rA));
        c = c64(b, (u64)(s64)(s32)i->simm);
        result = trunc_value(b, DOLIR_TYPE_I32,
                             binary(b, DOLIR_OP_MUL, DOLIR_TYPE_I64, a, c));
        set_gpr(b, i->rD, result);
        return true;
    case PPC_OP_SUBFIC:
        a = unary(b, DOLIR_OP_NOT, DOLIR_TYPE_I32, gpr(b, i->rA));
        c = c32(b, (u32)(s32)i->simm);
        write_carry_result(b, i->rD,
                           add_wide_with_carry(b, a, c, c32(b, 1)), false);
        return true;
    case PPC_OP_ADDIC:
    case PPC_OP_ADDIC_DOT:
        write_carry_result(b, i->rD,
                           add_wide_with_carry(b, gpr(b, i->rA),
                                               c32(b, (u32)(s32)i->simm), 0),
                           i->op == PPC_OP_ADDIC_DOT);
        return true;
    case PPC_OP_ADDI:
    case PPC_OP_ADDIS:
        a = i->rA ? gpr(b, i->rA) : c32(b, 0);
        c = c32(b, i->op == PPC_OP_ADDIS ? ((u32)(s32)i->simm << 16) : (u32)(s32)i->simm);
        set_gpr(b, i->rD, binary(b, DOLIR_OP_ADD, DOLIR_TYPE_I32, a, c));
        return true;
    case PPC_OP_ORI: case PPC_OP_ORIS: case PPC_OP_XORI: case PPC_OP_XORIS:
    case PPC_OP_ANDI: case PPC_OP_ANDIS: {
        u32 imm = (i->op == PPC_OP_ORIS || i->op == PPC_OP_XORIS || i->op == PPC_OP_ANDIS)
                      ? ((u32)i->uimm << 16) : i->uimm;
        DolIROp op = (i->op == PPC_OP_ORI || i->op == PPC_OP_ORIS) ? DOLIR_OP_OR :
                     (i->op == PPC_OP_XORI || i->op == PPC_OP_XORIS) ? DOLIR_OP_XOR : DOLIR_OP_AND;
        result = binary(b, op, DOLIR_TYPE_I32, gpr(b, i->rS), c32(b, imm));
        set_gpr(b, i->rA, result);
        if (op == DOLIR_OP_AND)
            set_cr0(b, result);
        return true;
    }
    case PPC_OP_CMPI: case PPC_OP_CMPLI: case PPC_OP_CMP: case PPC_OP_CMPL:
        a = gpr(b, i->rA);
        c = (i->op == PPC_OP_CMPI) ? c32(b, (u32)(s32)i->simm) :
            (i->op == PPC_OP_CMPLI) ? c32(b, i->uimm) : gpr(b, i->rB);
        set_cr_field(b, i->crfD, a, c, i->op == PPC_OP_CMPI || i->op == PPC_OP_CMP);
        return true;
    case PPC_OP_ADD: case PPC_OP_ADDC: case PPC_OP_ADDE:
    case PPC_OP_ADDCO: case PPC_OP_ADDEO:
        if (i->oe)
            return false;
        a = gpr(b, i->rA);
        c = gpr(b, i->rB);
        if (i->op == PPC_OP_ADDC || i->op == PPC_OP_ADDCO ||
            i->op == PPC_OP_ADDE || i->op == PPC_OP_ADDEO) {
            DolIRValue wide = binary(b, DOLIR_OP_ADD, DOLIR_TYPE_I64,
                                     zext_value(b, DOLIR_TYPE_I64, a),
                                     zext_value(b, DOLIR_TYPE_I64, c));
            if (i->op == PPC_OP_ADDE || i->op == PPC_OP_ADDEO) {
                DolIRValue ca = xer_ca(b);
                wide = binary(b, DOLIR_OP_ADD, DOLIR_TYPE_I64, wide,
                              zext_value(b, DOLIR_TYPE_I64, ca));
            }
            result = trunc_value(b, DOLIR_TYPE_I32, wide);
            DolIRValue carry = trunc_value(b, DOLIR_TYPE_I1,
                                           binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I64, wide, c64(b, 32)));
            set_xer_ca(b, carry);
        } else {
            result = binary(b, DOLIR_OP_ADD, DOLIR_TYPE_I32, a, c);
        }
        set_gpr(b, i->rD, result);
        record_if_needed(b, result);
        return true;
    case PPC_OP_SUBF: case PPC_OP_SUBFC: case PPC_OP_SUBFE:
    case PPC_OP_SUBFO: case PPC_OP_SUBFCO: case PPC_OP_SUBFEO:
        if (i->oe)
            return false;
        a = gpr(b, i->rA);
        c = gpr(b, i->rB);
        if (i->op == PPC_OP_SUBFC || i->op == PPC_OP_SUBFCO ||
            i->op == PPC_OP_SUBFE || i->op == PPC_OP_SUBFEO) {
            DolIRValue not_a = unary(b, DOLIR_OP_NOT, DOLIR_TYPE_I32, a);
            DolIRValue wide = binary(b, DOLIR_OP_ADD, DOLIR_TYPE_I64,
                                     zext_value(b, DOLIR_TYPE_I64, not_a),
                                     zext_value(b, DOLIR_TYPE_I64, c));
            DolIRValue carry_in = c32(b, (i->op == PPC_OP_SUBFC || i->op == PPC_OP_SUBFCO) ? 1 : 0);
            if (i->op == PPC_OP_SUBFE || i->op == PPC_OP_SUBFEO)
                carry_in = xer_ca(b);
            wide = binary(b, DOLIR_OP_ADD, DOLIR_TYPE_I64, wide,
                          zext_value(b, DOLIR_TYPE_I64, carry_in));
            result = trunc_value(b, DOLIR_TYPE_I32, wide);
            set_xer_ca(b, trunc_value(b, DOLIR_TYPE_I1,
                binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I64, wide, c64(b, 32))));
        } else {
            result = binary(b, DOLIR_OP_SUB, DOLIR_TYPE_I32, c, a);
        }
        set_gpr(b, i->rD, result);
        record_if_needed(b, result);
        return true;
    case PPC_OP_NEG:
        result = binary(b, DOLIR_OP_SUB, DOLIR_TYPE_I32, c32(b, 0), gpr(b, i->rA));
        set_gpr(b, i->rD, result);
        record_if_needed(b, result);
        return true;
    case PPC_OP_ADDME: case PPC_OP_ADDZE:
    case PPC_OP_SUBFME: case PPC_OP_SUBFZE: {
        a = gpr(b, i->rA);
        if (i->op == PPC_OP_SUBFME || i->op == PPC_OP_SUBFZE)
            a = unary(b, DOLIR_OP_NOT, DOLIR_TYPE_I32, a);
        c = (i->op == PPC_OP_ADDME || i->op == PPC_OP_SUBFME)
                ? c32(b, 0xFFFFFFFFu) : c32(b, 0);
        write_carry_result(b, i->rD,
                           add_wide_with_carry(b, a, c, xer_ca(b)), i->rc);
        return true;
    }
    case PPC_OP_AND: case PPC_OP_ANDC: case PPC_OP_OR: case PPC_OP_ORC:
    case PPC_OP_XOR: case PPC_OP_NAND: case PPC_OP_NOR: case PPC_OP_EQV: {
        a = gpr(b, i->rS);
        c = gpr(b, i->rB);
        if (i->op == PPC_OP_ANDC || i->op == PPC_OP_ORC)
            c = unary(b, DOLIR_OP_NOT, DOLIR_TYPE_I32, c);
        DolIROp op = (i->op == PPC_OP_AND || i->op == PPC_OP_ANDC || i->op == PPC_OP_NAND) ? DOLIR_OP_AND :
                     (i->op == PPC_OP_OR || i->op == PPC_OP_ORC || i->op == PPC_OP_NOR) ? DOLIR_OP_OR : DOLIR_OP_XOR;
        result = binary(b, op, DOLIR_TYPE_I32, a, c);
        if (i->op == PPC_OP_NAND || i->op == PPC_OP_NOR || i->op == PPC_OP_EQV)
            result = unary(b, DOLIR_OP_NOT, DOLIR_TYPE_I32, result);
        set_gpr(b, i->rA, result);
        record_if_needed(b, result);
        return true;
    }
    case PPC_OP_EXTSB: case PPC_OP_EXTSH:
        a = trunc_value(b, i->op == PPC_OP_EXTSB ? DOLIR_TYPE_I8 : DOLIR_TYPE_I16, gpr(b, i->rS));
        result = sext_value(b, DOLIR_TYPE_I32, a);
        set_gpr(b, i->rA, result);
        record_if_needed(b, result);
        return true;
    case PPC_OP_CNTLZW:
        result = unary(b, DOLIR_OP_CLZ, DOLIR_TYPE_I32, gpr(b, i->rS));
        set_gpr(b, i->rA, result);
        record_if_needed(b, result);
        return true;
    case PPC_OP_RLWINM: case PPC_OP_RLWNM: case PPC_OP_RLWIMI: {
        DolIRValue shift = i->op == PPC_OP_RLWNM ? gpr(b, i->rB) : c32(b, i->sh);
        result = binary(b, DOLIR_OP_ROTL, DOLIR_TYPE_I32, gpr(b, i->rS), shift);
        u32 mask = ppc_mask32(i->mb, i->me);
        result = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32, result, c32(b, mask));
        if (i->op == PPC_OP_RLWIMI)
            result = binary(b, DOLIR_OP_OR, DOLIR_TYPE_I32,
                            binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32, gpr(b, i->rA), c32(b, ~mask)), result);
        set_gpr(b, i->rA, result);
        record_if_needed(b, result);
        return true;
    }
    case PPC_OP_SLW: case PPC_OP_SRW: {
        DolIRValue sh = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32, gpr(b, i->rB), c32(b, 0x3F));
        DolIRValue too_big = binary(b, DOLIR_OP_ICMP_ULT, DOLIR_TYPE_I1, c32(b, 31), sh);
        result = binary(b, i->op == PPC_OP_SLW ? DOLIR_OP_SHL : DOLIR_OP_LSHR,
                        DOLIR_TYPE_I32, gpr(b, i->rS),
                        binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32, sh, c32(b, 31)));
        result = select_value(b, DOLIR_TYPE_I32, too_big, c32(b, 0), result);
        set_gpr(b, i->rA, result);
        record_if_needed(b, result);
        return true;
    }
    case PPC_OP_SRAW: case PPC_OP_SRAWI: {
        DolIRValue value = gpr(b, i->rS);
        DolIRValue sh = i->op == PPC_OP_SRAWI
                ? c32(b, i->sh)
                : binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32,
                         gpr(b, i->rB), c32(b, 0x3F));
        DolIRValue safe_sh = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32,
                                    sh, c32(b, 31));
        DolIRValue sign = binary(b, DOLIR_OP_ICMP_SLT, DOLIR_TYPE_I1,
                                 value, c32(b, 0));
        DolIRValue too_big = binary(b, DOLIR_OP_ICMP_ULT, DOLIR_TYPE_I1,
                                    c32(b, 31), sh);
        DolIRValue shifted = binary(b, DOLIR_OP_ASHR, DOLIR_TYPE_I32,
                                    value, safe_sh);
        result = select_value(b, DOLIR_TYPE_I32, too_big,
                              select_value(b, DOLIR_TYPE_I32, sign,
                                           c32(b, 0xFFFFFFFFu), c32(b, 0)),
                              shifted);
        DolIRValue low_mask = binary(b, DOLIR_OP_SUB, DOLIR_TYPE_I32,
            binary(b, DOLIR_OP_SHL, DOLIR_TYPE_I32, c32(b, 1), safe_sh),
            c32(b, 1));
        DolIRValue lost = binary(b, DOLIR_OP_ICMP_NE, DOLIR_TYPE_I1,
            binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32, value, low_mask),
            c32(b, 0));
        DolIRValue ca = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I1, sign, lost);
        ca = select_value(b, DOLIR_TYPE_I1, too_big, sign, ca);
        set_gpr(b, i->rA, result);
        set_xer_ca(b, ca);
        record_if_needed(b, result);
        return true;
    }
    case PPC_OP_MULLW: case PPC_OP_MULHW: case PPC_OP_MULHWU:
        a = (i->op == PPC_OP_MULHWU) ? zext_value(b, DOLIR_TYPE_I64, gpr(b, i->rA)) :
                                       sext_value(b, DOLIR_TYPE_I64, gpr(b, i->rA));
        c = (i->op == PPC_OP_MULHWU) ? zext_value(b, DOLIR_TYPE_I64, gpr(b, i->rB)) :
                                       sext_value(b, DOLIR_TYPE_I64, gpr(b, i->rB));
        result = binary(b, DOLIR_OP_MUL, DOLIR_TYPE_I64, a, c);
        if (i->op != PPC_OP_MULLW)
            result = binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I64, result, c64(b, 32));
        result = trunc_value(b, DOLIR_TYPE_I32, result);
        set_gpr(b, i->rD, result);
        record_if_needed(b, result);
        return true;
    case PPC_OP_DIVW: case PPC_OP_DIVWU: {
        a = gpr(b, i->rA);
        c = gpr(b, i->rB);
        DolIRValue zero = binary(b, DOLIR_OP_ICMP_EQ, DOLIR_TYPE_I1,
                                 c, c32(b, 0));
        DolIRValue invalid = zero;
        if (i->op == PPC_OP_DIVW) {
            DolIRValue min = binary(b, DOLIR_OP_ICMP_EQ, DOLIR_TYPE_I1,
                                    a, c32(b, 0x80000000u));
            DolIRValue neg_one = binary(b, DOLIR_OP_ICMP_EQ, DOLIR_TYPE_I1,
                                        c, c32(b, 0xFFFFFFFFu));
            invalid = binary(b, DOLIR_OP_OR, DOLIR_TYPE_I1, zero,
                             binary(b, DOLIR_OP_AND, DOLIR_TYPE_I1,
                                    min, neg_one));
        }
        DolIRValue safe_divisor = select_value(b, DOLIR_TYPE_I32, invalid,
                                               c32(b, 1), c);
        result = binary(b, i->op == PPC_OP_DIVW ? DOLIR_OP_SDIV : DOLIR_OP_UDIV,
                        DOLIR_TYPE_I32, a, safe_divisor);
        if (i->op == PPC_OP_DIVW) {
            DolIRValue negative = binary(b, DOLIR_OP_ICMP_SLT,
                                         DOLIR_TYPE_I1, a, c32(b, 0));
            result = select_value(b, DOLIR_TYPE_I32, invalid,
                                  select_value(b, DOLIR_TYPE_I32, negative,
                                               c32(b, 0xFFFFFFFFu), c32(b, 0)),
                                  result);
        } else {
            result = select_value(b, DOLIR_TYPE_I32, invalid, c32(b, 0), result);
        }
        set_gpr(b, i->rD, result);
        record_if_needed(b, result);
        return true;
    }
    default:
        return false;
    }
}

static bool lower_memory(Builder* b) {
    const PPCInst* i = b->inst;
    bool indexed = false;
    bool update = false;
    bool load = false;
    bool sign = false;
    bool reverse = false;
    u8 width = 0;

    switch (i->op) {
    case PPC_OP_LWZ: load = true; width = 4; break;
    case PPC_OP_LWZU: load = true; width = 4; update = true; break;
    case PPC_OP_LBZ: load = true; width = 1; break;
    case PPC_OP_LBZU: load = true; width = 1; update = true; break;
    case PPC_OP_LHZ: load = true; width = 2; break;
    case PPC_OP_LHZU: load = true; width = 2; update = true; break;
    case PPC_OP_LHA: load = true; width = 2; sign = true; break;
    case PPC_OP_LHAU: load = true; width = 2; sign = true; update = true; break;
    case PPC_OP_LWZX: load = true; width = 4; indexed = true; break;
    case PPC_OP_LWZUX: load = true; width = 4; indexed = true; update = true; break;
    case PPC_OP_LBZX: load = true; width = 1; indexed = true; break;
    case PPC_OP_LBZUX: load = true; width = 1; indexed = true; update = true; break;
    case PPC_OP_LHZX: load = true; width = 2; indexed = true; break;
    case PPC_OP_LHZUX: load = true; width = 2; indexed = true; update = true; break;
    case PPC_OP_LHAX: load = true; width = 2; sign = true; indexed = true; break;
    case PPC_OP_LHAUX: load = true; width = 2; sign = true; indexed = true; update = true; break;
    case PPC_OP_LWBRX: load = true; width = 4; reverse = true; indexed = true; break;
    case PPC_OP_LHBRX: load = true; width = 2; reverse = true; indexed = true; break;
    case PPC_OP_STW: width = 4; break;
    case PPC_OP_STWU: width = 4; update = true; break;
    case PPC_OP_STB: width = 1; break;
    case PPC_OP_STBU: width = 1; update = true; break;
    case PPC_OP_STH: width = 2; break;
    case PPC_OP_STHU: width = 2; update = true; break;
    case PPC_OP_STWX: width = 4; indexed = true; break;
    case PPC_OP_STWUX: width = 4; indexed = true; update = true; break;
    case PPC_OP_STBX: width = 1; indexed = true; break;
    case PPC_OP_STBUX: width = 1; indexed = true; update = true; break;
    case PPC_OP_STHX: width = 2; indexed = true; break;
    case PPC_OP_STHUX: width = 2; indexed = true; update = true; break;
    case PPC_OP_STWBRX: width = 4; reverse = true; indexed = true; break;
    case PPC_OP_STHBRX: width = 2; reverse = true; indexed = true; break;
    default: break;
    }

    if (width) {
        DolIRValue address = effective_address(b, indexed, update);
        if (load) {
            DolIRValue value = guest_load(b, address, width, sign);
            if (reverse)
                value = unary(b, DOLIR_OP_BSWAP, DOLIR_TYPE_I32, value);
            set_gpr(b, i->rD, value);
        } else {
            DolIRValue value = gpr(b, i->rS);
            if (reverse)
                value = unary(b, DOLIR_OP_BSWAP, DOLIR_TYPE_I32, value);
            guest_store(b, address, value, width);
        }
        if (update)
            set_gpr(b, i->rA, address);
        return true;
    }

    if (i->op == PPC_OP_LMW || i->op == PPC_OP_STMW) {
        DolIRValue address = effective_address(b, false, false);
        u8 first = i->op == PPC_OP_LMW ? i->rD : i->rS;
        for (u8 reg = first; reg < 32; reg++) {
            DolIRValue ea = binary(b, DOLIR_OP_ADD, DOLIR_TYPE_I32, address,
                                   c32(b, 4u * (reg - first)));
            if (i->op == PPC_OP_LMW)
                set_gpr(b, reg, guest_load(b, ea, 4, false));
            else
                guest_store(b, ea, gpr(b, reg), 4);
        }
        return true;
    }

    if (i->op == PPC_OP_LWARX) {
        DolIRValue address = effective_address(b, true, false);
        set_gpr(b, i->rD, guest_load(b, address, 4, false));
        write_slot(b, DOLIR_STATE_RESERVE_ADDR, address);
        write_slot(b, DOLIR_STATE_RESERVE_VALID, c1(b, true));
        return true;
    }

    if (i->op == PPC_OP_STWCX) {
        DolIRValue operands[2] = {effective_address(b, true, false),
                                  gpr(b, i->rS)};
        dolir_append(b->function, b->block, DOLIR_OP_HELPER_CALL,
                     DOLIR_TYPE_VOID, operands, 2, i->rS,
                     DOLIR_HELPER_STORE_CONDITIONAL, i->address,
                     DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                     DOLIR_EFFECT_WRITE_MEMORY | DOLIR_EFFECT_MAY_EXIT |
                     DOLIR_EFFECT_MAY_RAISE | DOLIR_EFFECT_BARRIER);
        return true;
    }

    if (i->op == PPC_OP_DCBZ) {
        DolIRValue address = effective_address(b, true, false);
        address = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32, address, c32(b, ~31u));
        for (u32 offset = 0; offset < 32; offset += 4)
            guest_store(b, binary(b, DOLIR_OP_ADD, DOLIR_TYPE_I32,
                                  address, c32(b, offset)), c32(b, 0), 4);
        return true;
    }

    if (i->op == PPC_OP_LSWX) {
        u64 descriptor = i->rD | ((u64)i->rA << 8) | ((u64)i->rB << 16);
        dolir_append(b->function, b->block, DOLIR_OP_HELPER_CALL,
                     DOLIR_TYPE_VOID, NULL, 0, descriptor,
                     DOLIR_HELPER_LSWX, i->address,
                     DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                     DOLIR_EFFECT_READ_MEMORY | DOLIR_EFFECT_MAY_EXIT |
                     DOLIR_EFFECT_MAY_RAISE | DOLIR_EFFECT_BARRIER);
        return true;
    }

    if (i->op == PPC_OP_DCBZ_L) {
        DolIRValue address = effective_address(b, true, false);
        dolir_append(b->function, b->block, DOLIR_OP_HELPER_CALL,
                     DOLIR_TYPE_VOID, &address, 1, 0,
                     DOLIR_HELPER_DCBZ_L, i->address,
                     DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                     DOLIR_EFFECT_WRITE_MEMORY | DOLIR_EFFECT_MAY_EXIT |
                     DOLIR_EFFECT_MAY_RAISE | DOLIR_EFFECT_BARRIER);
        return true;
    }

    if (i->op == PPC_OP_ECIWX || i->op == PPC_OP_ECOWX) {
        DolIRValue operands[2] = {effective_address(b, true, false), 0};
        u8 operand_count = 1;
        if (i->op == PPC_OP_ECOWX) {
            operands[1] = gpr(b, i->rS);
            operand_count = 2;
        }
        DolIRValue value = dolir_append(b->function, b->block,
            DOLIR_OP_HELPER_CALL,
            i->op == PPC_OP_ECIWX ? DOLIR_TYPE_I32 : DOLIR_TYPE_VOID,
            operands, operand_count, 0,
            i->op == PPC_OP_ECIWX ? DOLIR_HELPER_ECIWX : DOLIR_HELPER_ECOWX,
            i->address,
            DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
            DOLIR_EFFECT_READ_MEMORY | DOLIR_EFFECT_WRITE_MEMORY |
            DOLIR_EFFECT_MAY_EXIT | DOLIR_EFFECT_MAY_RAISE |
            DOLIR_EFFECT_BARRIER);
        if (i->op == PPC_OP_ECIWX)
            set_gpr(b, i->rD, value);
        return true;
    }

    if (i->op == PPC_OP_DCBST || i->op == PPC_OP_DCBF ||
        i->op == PPC_OP_DCBI || i->op == PPC_OP_ICBI) {
        u32 operation = i->op == PPC_OP_DCBST ? 0u :
                        i->op == PPC_OP_DCBF ? 1u :
                        i->op == PPC_OP_DCBI ? 2u : 3u;
        DolIRValue address = effective_address(b, true, false);
        dolir_append(b->function, b->block, DOLIR_OP_HELPER_CALL,
                     DOLIR_TYPE_VOID, &address, 1, operation,
                     DOLIR_HELPER_CACHE_CONTROL, i->address,
                     DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                     DOLIR_EFFECT_READ_MEMORY | DOLIR_EFFECT_WRITE_MEMORY |
                     DOLIR_EFFECT_MAY_EXIT | DOLIR_EFFECT_MAY_RAISE |
                     DOLIR_EFFECT_BARRIER);
        return true;
    }

    return i->op == PPC_OP_DCBT || i->op == PPC_OP_DCBTST;
}

static bool lower_float_memory(Builder* b) {
    const PPCInst* i = b->inst;
    bool indexed = false;
    bool update = false;
    bool load = false;
    bool single = false;

    if (i->op == PPC_OP_STFIWX) {
        DolIRValue address = effective_address(b, true, false);
        DolIRValue bits = unary(b, DOLIR_OP_BITCAST, DOLIR_TYPE_I64,
                                fpr(b, i->rS));
        guest_store(b, address, trunc_value(b, DOLIR_TYPE_I32, bits), 4);
        return true;
    }

    switch (i->op) {
    case PPC_OP_LFS: load = true; single = true; break;
    case PPC_OP_LFSU: load = true; single = true; update = true; break;
    case PPC_OP_LFD: load = true; break;
    case PPC_OP_LFDU: load = true; update = true; break;
    case PPC_OP_LFSX: load = true; single = true; indexed = true; break;
    case PPC_OP_LFSUX: load = true; single = true; indexed = true; update = true; break;
    case PPC_OP_LFDX: load = true; indexed = true; break;
    case PPC_OP_LFDUX: load = true; indexed = true; update = true; break;
    case PPC_OP_STFS: single = true; break;
    case PPC_OP_STFSU: single = true; update = true; break;
    case PPC_OP_STFD: break;
    case PPC_OP_STFDU: update = true; break;
    case PPC_OP_STFSX: single = true; indexed = true; break;
    case PPC_OP_STFSUX: single = true; indexed = true; update = true; break;
    case PPC_OP_STFDX: indexed = true; break;
    case PPC_OP_STFDUX: indexed = true; update = true; break;
    default: return false;
    }

    DolIRValue address = effective_address(b, indexed, update);
    if (load) {
        DolIRType bits_type = single ? DOLIR_TYPE_I32 : DOLIR_TYPE_I64;
        DolIRValue bits = guest_load_typed(b, address, bits_type, single ? 4 : 8, false);
        DolIRValue value = unary(b, DOLIR_OP_BITCAST,
                                 single ? DOLIR_TYPE_F32 : DOLIR_TYPE_F64, bits);
        if (single)
            value = unary(b, DOLIR_OP_FPEXT, DOLIR_TYPE_F64, value);
        set_fpr(b, i->rD, value);
        // Gekko splits the float loads: lfs fills BOTH slots of the pair
        // (Interpreter::lfs -> Fill), while lfd writes ps0 only and leaves
        // ps1 architecturally intact (Interpreter::lfd -> SetPS0) for a
        // later ps_ op or psq_st to read. Splatting unconditionally left
        // the int-to-double bias (0x4330000000000000) from the guest's
        // stw/stw/lfd conversion idiom sitting in ps1. The C emitter has
        // always had this guard -- see emit_fload/emit_floadx.
        if (single)
            set_ps1(b, i->rD, value);
    } else {
        DolIRValue value = fpr(b, i->rS);
        if (single)
            value = unary(b, DOLIR_OP_FPTRUNC, DOLIR_TYPE_F32, value);
        DolIRValue bits = unary(b, DOLIR_OP_BITCAST,
                                single ? DOLIR_TYPE_I32 : DOLIR_TYPE_I64, value);
        guest_store(b, address, bits, single ? 4 : 8);
    }
    if (update)
        set_gpr(b, i->rA, address);
    return true;
}

static bool lower_psq(Builder* b) {
    const PPCInst* i = b->inst;
    bool load;
    bool indexed;
    bool update;
    switch (i->op) {
    case PPC_OP_PSQ_L: load = true; indexed = false; update = false; break;
    case PPC_OP_PSQ_LU: load = true; indexed = false; update = true; break;
    case PPC_OP_PSQ_LX: load = true; indexed = true; update = false; break;
    case PPC_OP_PSQ_LUX: load = true; indexed = true; update = true; break;
    case PPC_OP_PSQ_ST: load = false; indexed = false; update = false; break;
    case PPC_OP_PSQ_STU: load = false; indexed = false; update = true; break;
    case PPC_OP_PSQ_STX: load = false; indexed = true; update = false; break;
    case PPC_OP_PSQ_STUX: load = false; indexed = true; update = true; break;
    default: return false;
    }
    DolIRValue address = effective_address(b, indexed, update);
    u32 reg = load ? i->rD : i->rS;
    u64 descriptor = reg | ((u64)i->w << 8) | ((u64)i->i << 9) |
                     ((u64)indexed << 12);
    DolIRValue success = dolir_append(b->function, b->block,
        DOLIR_OP_HELPER_CALL, DOLIR_TYPE_I1, &address, 1, descriptor,
        load ? DOLIR_HELPER_PSQ_LOAD : DOLIR_HELPER_PSQ_STORE, i->address,
        DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
        DOLIR_EFFECT_READ_MEMORY | DOLIR_EFFECT_WRITE_MEMORY |
        DOLIR_EFFECT_MAY_EXIT | DOLIR_EFFECT_MAY_RAISE |
        DOLIR_EFFECT_BARRIER);
    if (update)
        set_gpr(b, i->rA, select_value(b, DOLIR_TYPE_I32, success,
                                      address, gpr(b, i->rA)));
    return true;
}

static bool lower_float(Builder* b) {
    const PPCInst* i = b->inst;
    switch (i->op) {
    case PPC_OP_MTFSB0:
    case PPC_OP_MTFSB1:
        dolir_append(b->function, b->block, DOLIR_OP_HELPER_CALL,
                     DOLIR_TYPE_VOID, NULL, 0,
                     i->rD | ((u64)(i->op == PPC_OP_MTFSB1) << 8),
                     DOLIR_HELPER_FPSCR_BIT, i->address,
                     DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                     DOLIR_EFFECT_BARRIER);
        if (i->rc)
            set_cr1_from_fpscr(b);
        return true;
    case PPC_OP_MFFS: {
        DolIRValue bits = binary(b, DOLIR_OP_OR, DOLIR_TYPE_I64,
            c64(b, 0xFFF8000000000000ull),
            zext_value(b, DOLIR_TYPE_I64, read_slot(b, DOLIR_STATE_FPSCR)));
        set_fpr(b, i->rD, unary(b, DOLIR_OP_BITCAST, DOLIR_TYPE_F64, bits));
        if (i->rc)
            set_cr1_from_fpscr(b);
        return true;
    }
    case PPC_OP_MCRFS: {
        u32 source_shift = 4u * (7u - i->crfS);
        DolIRValue fpscr = read_slot(b, DOLIR_STATE_FPSCR);
        DolIRValue field = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32,
            binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I32, fpscr,
                   c32(b, source_shift)), c32(b, 0xFu));
        write_slot(b, DOLIR_STATE_FPSCR,
            binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32, fpscr,
                   c32(b, ~((0xFu << source_shift) & 0x83F80700u))));
        fpscr_updated(b);
        write_cr_field(b, i->crfD, field);
        return true;
    }
    case PPC_OP_MTFSFI: {
        u32 shift = 4u * (7u - i->crfD);
        DolIRValue value = c32(b, (u32)i->imm << shift);
        write_slot(b, DOLIR_STATE_FPSCR,
            insert_masked(b, read_slot(b, DOLIR_STATE_FPSCR), value,
                          0xFu << shift));
        fpscr_updated(b);
        if (i->rc)
            set_cr1_from_fpscr(b);
        return true;
    }
    case PPC_OP_MTFSF: {
        u32 mask = 0;
        for (u32 field = 0; field < 8; field++)
            if (i->fm & (1u << field))
                mask |= 0xFu << (field * 4u);
        DolIRValue source = trunc_value(b, DOLIR_TYPE_I32,
            unary(b, DOLIR_OP_BITCAST, DOLIR_TYPE_I64, fpr(b, i->rB)));
        write_slot(b, DOLIR_STATE_FPSCR,
            insert_masked(b, read_slot(b, DOLIR_STATE_FPSCR), source, mask));
        fpscr_updated(b);
        if (i->rc)
            set_cr1_from_fpscr(b);
        return true;
    }
    default:
        break;
    }

    DolIRExactFloat exact;
    bool exact_op = true;
    switch (i->op) {
    case PPC_OP_FRES: exact = DOLIR_EXACT_FRES; break;
    case PPC_OP_FRSQRTE: exact = DOLIR_EXACT_FRSQRTE; break;
    case PPC_OP_FMADD: exact = DOLIR_EXACT_FMADD; break;
    case PPC_OP_FMSUB: exact = DOLIR_EXACT_FMSUB; break;
    case PPC_OP_FNMADD: exact = DOLIR_EXACT_FNMADD; break;
    case PPC_OP_FNMSUB: exact = DOLIR_EXACT_FNMSUB; break;
    case PPC_OP_FMADDS: exact = DOLIR_EXACT_FMADDS; break;
    case PPC_OP_FMSUBS: exact = DOLIR_EXACT_FMSUBS; break;
    case PPC_OP_FNMADDS: exact = DOLIR_EXACT_FNMADDS; break;
    case PPC_OP_FNMSUBS: exact = DOLIR_EXACT_FNMSUBS; break;
    case PPC_OP_FCTIW: exact = DOLIR_EXACT_FCTIW; break;
    case PPC_OP_FCTIWZ: exact = DOLIR_EXACT_FCTIWZ; break;
    case PPC_OP_FCMPU: exact = DOLIR_EXACT_FCMPU; break;
    case PPC_OP_FCMPO: exact = DOLIR_EXACT_FCMPO; break;
    case PPC_OP_FADDS: exact = DOLIR_EXACT_FADDS; break;
    case PPC_OP_FSUBS: exact = DOLIR_EXACT_FSUBS; break;
    case PPC_OP_FMULS: exact = DOLIR_EXACT_FMULS; break;
    case PPC_OP_FDIVS: exact = DOLIR_EXACT_FDIVS; break;
    case PPC_OP_FADD: exact = DOLIR_EXACT_FADD; break;
    case PPC_OP_FSUB: exact = DOLIR_EXACT_FSUB; break;
    case PPC_OP_FMUL: exact = DOLIR_EXACT_FMUL; break;
    case PPC_OP_FDIV: exact = DOLIR_EXACT_FDIV; break;
    case PPC_OP_FRSP: exact = DOLIR_EXACT_FRSP; break;
    default: exact_op = false; break;
    }
    if (exact_op) {
        u64 descriptor = (u64)exact | ((u64)i->rD << 8) |
                         ((u64)i->rA << 16) | ((u64)i->rB << 24) |
                         ((u64)i->rC << 32) | ((u64)i->crfD << 40);
        dolir_append(b->function, b->block, DOLIR_OP_HELPER_CALL,
                     DOLIR_TYPE_VOID, NULL, 0, descriptor,
                     DOLIR_HELPER_EXACT_FLOAT, i->address,
                     DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                     DOLIR_EFFECT_BARRIER);
        if (i->rc)
            set_cr1_from_fpscr(b);
        return true;
    }
    DolIRValue value;
    switch (i->op) {
    case PPC_OP_FMR: value = fpr(b, i->rB); break;
    case PPC_OP_FNEG: value = unary(b, DOLIR_OP_FNEG, DOLIR_TYPE_F64, fpr(b, i->rB)); break;
    case PPC_OP_FABS: value = unary(b, DOLIR_OP_FABS, DOLIR_TYPE_F64, fpr(b, i->rB)); break;
    case PPC_OP_FNABS:
        value = unary(b, DOLIR_OP_FNEG, DOLIR_TYPE_F64,
                      unary(b, DOLIR_OP_FABS, DOLIR_TYPE_F64, fpr(b, i->rB)));
        break;
    case PPC_OP_FSEL: {
        DolIRValue condition = binary(b, DOLIR_OP_FCMP_OGE, DOLIR_TYPE_I1,
                                      fpr(b, i->rA),
                                      unary(b, DOLIR_OP_BITCAST, DOLIR_TYPE_F64, c64(b, 0)));
        value = select_value(b, DOLIR_TYPE_F64, condition, fpr(b, i->rC), fpr(b, i->rB));
        break;
    }
    default: return false;
    }
    // fmr, fneg, fabs, fnabs and fsel are all ps0-only on Gekko --
    // Interpreter_FloatMisc uses SetPS0 for every one of them -- so ps1 is
    // preserved across them. The C emitter never wrote ps1 here either.
    set_fpr(b, i->rD, value);
    if (i->rc)
        set_cr1_from_fpscr(b);
    return true;
}

static DolIRValue vector_lane(Builder* b, DolIRValue pair, u32 lane) {
    DolIRValue operands[1] = {pair};
    return dolir_append(b->function, b->block, DOLIR_OP_VECTOR_EXTRACT,
                        DOLIR_TYPE_F64, operands, 1, 0, lane,
                        b->inst->address, DOLIR_EFFECT_NONE);
}

static bool lower_paired(Builder* b) {
    const PPCInst* i = b->inst;
    DolIRExactPaired exact;
    bool exact_op = true;
    switch (i->op) {
    case PPC_OP_PS_ADD: exact = DOLIR_EXACT_PS_ADD; break;
    case PPC_OP_PS_SUB: exact = DOLIR_EXACT_PS_SUB; break;
    case PPC_OP_PS_MUL: exact = DOLIR_EXACT_PS_MUL; break;
    case PPC_OP_PS_DIV: exact = DOLIR_EXACT_PS_DIV; break;
    case PPC_OP_PS_MADD: exact = DOLIR_EXACT_PS_MADD; break;
    case PPC_OP_PS_MSUB: exact = DOLIR_EXACT_PS_MSUB; break;
    case PPC_OP_PS_NMADD: exact = DOLIR_EXACT_PS_NMADD; break;
    case PPC_OP_PS_NMSUB: exact = DOLIR_EXACT_PS_NMSUB; break;
    case PPC_OP_PS_MADDS0: exact = DOLIR_EXACT_PS_MADDS0; break;
    case PPC_OP_PS_MADDS1: exact = DOLIR_EXACT_PS_MADDS1; break;
    case PPC_OP_PS_SUM0: exact = DOLIR_EXACT_PS_SUM0; break;
    case PPC_OP_PS_SUM1: exact = DOLIR_EXACT_PS_SUM1; break;
    case PPC_OP_PS_MULS0: exact = DOLIR_EXACT_PS_MULS0; break;
    case PPC_OP_PS_MULS1: exact = DOLIR_EXACT_PS_MULS1; break;
    case PPC_OP_PS_RES: exact = DOLIR_EXACT_PS_RES; break;
    case PPC_OP_PS_RSQRTE: exact = DOLIR_EXACT_PS_RSQRTE; break;
    case PPC_OP_PS_CMPU0: exact = DOLIR_EXACT_PS_CMPU0; break;
    case PPC_OP_PS_CMPO0: exact = DOLIR_EXACT_PS_CMPO0; break;
    case PPC_OP_PS_CMPU1: exact = DOLIR_EXACT_PS_CMPU1; break;
    case PPC_OP_PS_CMPO1: exact = DOLIR_EXACT_PS_CMPO1; break;
    default: exact_op = false; break;
    }
    if (exact_op) {
        u64 descriptor = (u64)exact | ((u64)i->rD << 8) |
                         ((u64)i->rA << 16) | ((u64)i->rB << 24) |
                         ((u64)i->rC << 32) | ((u64)i->crfD << 40);
        dolir_append(b->function, b->block, DOLIR_OP_HELPER_CALL,
                     DOLIR_TYPE_VOID, NULL, 0, descriptor,
                     DOLIR_HELPER_EXACT_PAIRED, i->address,
                     DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                     DOLIR_EFFECT_BARRIER);
        if (i->rc)
            set_cr1_from_fpscr(b);
        return true;
    }

    DolIRValue value;
    switch (i->op) {
    case PPC_OP_PS_NEG: value = unary(b, DOLIR_OP_FNEG, DOLIR_TYPE_V2F64, pair_value(b, i->rB)); break;
    case PPC_OP_PS_ABS: value = unary(b, DOLIR_OP_FABS, DOLIR_TYPE_V2F64, pair_value(b, i->rB)); break;
    case PPC_OP_PS_NABS:
        value = unary(b, DOLIR_OP_FNEG, DOLIR_TYPE_V2F64,
                      unary(b, DOLIR_OP_FABS, DOLIR_TYPE_V2F64, pair_value(b, i->rB)));
        break;
    case PPC_OP_PS_MR: value = pair_value(b, i->rB); break;
    case PPC_OP_PS_MERGE00: case PPC_OP_PS_MERGE01:
    case PPC_OP_PS_MERGE10: case PPC_OP_PS_MERGE11: {
        u32 left = (i->op == PPC_OP_PS_MERGE10 || i->op == PPC_OP_PS_MERGE11);
        u32 right = (i->op == PPC_OP_PS_MERGE01 || i->op == PPC_OP_PS_MERGE11);
        DolIRValue operands[2] = {pair_value(b, i->rA), pair_value(b, i->rB)};
        value = dolir_append(b->function, b->block, DOLIR_OP_VECTOR_SHUFFLE,
                             DOLIR_TYPE_V2F64, operands, 2, 0,
                             left | ((right + 2u) << 8), i->address,
                             DOLIR_EFFECT_NONE);
        break;
    }
    case PPC_OP_PS_SEL: {
        DolIRValue pa = pair_value(b, i->rA);
        DolIRValue pb = pair_value(b, i->rB);
        DolIRValue pc = pair_value(b, i->rC);
        DolIRValue zero = unary(b, DOLIR_OP_BITCAST, DOLIR_TYPE_F64, c64(b, 0));
        DolIRValue lo = select_value(b, DOLIR_TYPE_F64,
            binary(b, DOLIR_OP_FCMP_OGE, DOLIR_TYPE_I1,
                   vector_lane(b, pa, 0), zero),
            vector_lane(b, pc, 0), vector_lane(b, pb, 0));
        DolIRValue hi = select_value(b, DOLIR_TYPE_F64,
            binary(b, DOLIR_OP_FCMP_OGE, DOLIR_TYPE_I1,
                   vector_lane(b, pa, 1), zero),
            vector_lane(b, pc, 1), vector_lane(b, pb, 1));
        value = binary(b, DOLIR_OP_VECTOR_BUILD, DOLIR_TYPE_V2F64, lo, hi);
        break;
    }
    default: return false;
    }
    set_pair(b, i->rD, value);
    if (i->rc)
        set_cr1_from_fpscr(b);
    return true;
}

static bool spr_slot(u16 spr, DolIRStateSlot* slot) {
    switch (spr) {
    case 8: *slot = DOLIR_STATE_LR; return true;
    case 9: *slot = DOLIR_STATE_CTR; return true;
    case 18: *slot = DOLIR_STATE_DSISR; return true;
    case 19: *slot = DOLIR_STATE_DAR; return true;
    case 26: *slot = DOLIR_STATE_SRR0; return true;
    case 27: *slot = DOLIR_STATE_SRR1; return true;
    case 282: *slot = DOLIR_STATE_EAR; return true;
    case 920: *slot = DOLIR_STATE_HID2; return true;
    default:
        if (spr >= 912 && spr <= 919) {
            *slot = (DolIRStateSlot)(DOLIR_STATE_GQR0 + spr - 912);
            return true;
        }
        return false;
    }
}

static bool lower_state(Builder* b) {
    const PPCInst* i = b->inst;
    switch (i->op) {
    case PPC_OP_MFCR:
        set_gpr(b, i->rD, pack_cr(b));
        return true;
    case PPC_OP_MTCRF: {
        for (u32 field = 0; field < 8; field++)
            if (i->crm & (0x80u >> field)) {
                DolIRValue value = binary(
                    b, DOLIR_OP_LSHR, DOLIR_TYPE_I32, gpr(b, i->rS),
                    c32(b, 28u - 4u * field));
                write_cr_field(b, (u8)field, value);
            }
        return true;
    }
    case PPC_OP_MCRF: {
        write_cr_field(b, i->crfD, read_cr_field(b, i->crfS));
        return true;
    }
    case PPC_OP_MCRXR: {
        DolIRValue field = binary(
            b, DOLIR_OP_SHL, DOLIR_TYPE_I32,
            zext_value(b, DOLIR_TYPE_I32,
                       read_slot(b, DOLIR_STATE_XER_SO)), c32(b, 3));
        field = binary(
            b, DOLIR_OP_OR, DOLIR_TYPE_I32, field,
            binary(b, DOLIR_OP_SHL, DOLIR_TYPE_I32,
                   zext_value(b, DOLIR_TYPE_I32,
                              read_slot(b, DOLIR_STATE_XER_OV)), c32(b, 2)));
        field = binary(
            b, DOLIR_OP_OR, DOLIR_TYPE_I32, field,
            binary(b, DOLIR_OP_SHL, DOLIR_TYPE_I32,
                   zext_value(b, DOLIR_TYPE_I32,
                              read_slot(b, DOLIR_STATE_XER_CA)), c32(b, 1)));
        write_cr_field(b, i->crfD, field);
        write_slot(b, DOLIR_STATE_XER_CA, c1(b, false));
        write_slot(b, DOLIR_STATE_XER_OV, c1(b, false));
        write_slot(b, DOLIR_STATE_XER_SO, c1(b, false));
        return true;
    }
    case PPC_OP_CRAND: case PPC_OP_CRANDC: case PPC_OP_CREQV: case PPC_OP_CRNAND:
    case PPC_OP_CRNOR: case PPC_OP_CROR: case PPC_OP_CRORC: case PPC_OP_CRXOR: {
        DolIRValue av = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32,
                               binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I32,
                                      read_cr_field(b, i->rA / 4u),
                                      c32(b, 3u - (i->rA & 3u))), c32(b, 1));
        DolIRValue bv = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32,
                               binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I32,
                                      read_cr_field(b, i->rB / 4u),
                                      c32(b, 3u - (i->rB & 3u))), c32(b, 1));
        if (i->op == PPC_OP_CRANDC || i->op == PPC_OP_CRORC)
            bv = binary(b, DOLIR_OP_XOR, DOLIR_TYPE_I32, bv, c32(b, 1));
        DolIROp op = (i->op == PPC_OP_CRAND || i->op == PPC_OP_CRANDC || i->op == PPC_OP_CRNAND) ? DOLIR_OP_AND :
                     (i->op == PPC_OP_CROR || i->op == PPC_OP_CRORC || i->op == PPC_OP_CRNOR) ? DOLIR_OP_OR : DOLIR_OP_XOR;
        DolIRValue bit = binary(b, op, DOLIR_TYPE_I32, av, bv);
        if (i->op == PPC_OP_CREQV || i->op == PPC_OP_CRNAND || i->op == PPC_OP_CRNOR)
            bit = binary(b, DOLIR_OP_XOR, DOLIR_TYPE_I32, bit, c32(b, 1));
        u32 field = i->rD / 4u;
        u32 shift = 3u - (i->rD & 3u);
        bit = binary(b, DOLIR_OP_SHL, DOLIR_TYPE_I32, bit, c32(b, shift));
        write_cr_field(b, (u8)field,
                       insert_masked(b, read_cr_field(b, (u8)field), bit,
                                     1u << shift));
        return true;
    }
    case PPC_OP_MFSPR: {
        if (i->spr != 1 && i->spr != 8 && i->spr != 9 &&
            i->spr != 268 && i->spr != 269)
            require_supervisor(b);
        if (i->spr == 1) {
            set_gpr(b, i->rD, pack_xer(b));
            return true;
        }
        DolIRStateSlot slot;
        if (spr_slot(i->spr, &slot)) {
            set_gpr(b, i->rD, read_slot(b, slot));
            return true;
        }
        DolIRValue value = dolir_append(b->function, b->block,
            DOLIR_OP_HELPER_CALL, DOLIR_TYPE_I32, NULL, 0, i->spr,
            DOLIR_HELPER_SPR_READ, i->address,
            DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
            DOLIR_EFFECT_MAY_EXIT | DOLIR_EFFECT_MAY_RAISE |
            DOLIR_EFFECT_BARRIER);
        set_gpr(b, i->rD, value);
        return true;
    }
    case PPC_OP_MTSPR: {
        if (i->spr != 1 && i->spr != 8 && i->spr != 9)
            require_supervisor(b);
        if (i->spr == 1) {
            unpack_xer(b, gpr(b, i->rS));
            return true;
        }
        if (i->spr == 284 || i->spr == 285) {
            DolIRValue old = read_slot(b, DOLIR_STATE_TIMEBASE);
            DolIRValue value = zext_value(b, DOLIR_TYPE_I64, gpr(b, i->rS));
            if (i->spr == 284) {
                old = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I64, old,
                             c64(b, 0xFFFFFFFF00000000ull));
            } else {
                old = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I64, old,
                             c64(b, 0xFFFFFFFFull));
                value = binary(b, DOLIR_OP_SHL, DOLIR_TYPE_I64,
                               value, c64(b, 32));
            }
            write_slot(b, DOLIR_STATE_TIMEBASE,
                       binary(b, DOLIR_OP_OR, DOLIR_TYPE_I64, old, value));
            return true;
        }
        DolIRStateSlot slot;
        if (spr_slot(i->spr, &slot)) {
            write_slot(b, slot, gpr(b, i->rS));
            return true;
        }
        DolIRValue value = gpr(b, i->rS);
        dolir_append(b->function, b->block, DOLIR_OP_HELPER_CALL,
                     DOLIR_TYPE_VOID, &value, 1, i->spr,
                     DOLIR_HELPER_SPR_WRITE, i->address,
                     DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                     DOLIR_EFFECT_MAY_EXIT | DOLIR_EFFECT_MAY_RAISE |
                     DOLIR_EFFECT_BARRIER);
        return true;
    }
    case PPC_OP_MFMSR:
        require_supervisor(b);
        set_gpr(b, i->rD, read_slot(b, DOLIR_STATE_MSR));
        return true;
    case PPC_OP_MTMSR:
        require_supervisor(b);
        write_slot(b, DOLIR_STATE_MSR, gpr(b, i->rS));
        return true;
    case PPC_OP_MFSR:
        require_supervisor(b);
        set_gpr(b, i->rD,
                read_slot(b, (DolIRStateSlot)(DOLIR_STATE_SR0 + i->sr)));
        return true;
    case PPC_OP_MTSR:
        require_supervisor(b);
        write_slot(b, (DolIRStateSlot)(DOLIR_STATE_SR0 + i->sr),
                   gpr(b, i->rS));
        return true;
    case PPC_OP_MFSRIN: {
        require_supervisor(b);
        DolIRValue index = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32,
            binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I32, gpr(b, i->rB), c32(b, 28)),
            c32(b, 15));
        DolIRValue value = read_slot(b, DOLIR_STATE_SR0);
        for (u32 sr = 1; sr < 16; sr++)
            value = select_value(b, DOLIR_TYPE_I32,
                binary(b, DOLIR_OP_ICMP_EQ, DOLIR_TYPE_I1, index, c32(b, sr)),
                read_slot(b, (DolIRStateSlot)(DOLIR_STATE_SR0 + sr)), value);
        set_gpr(b, i->rD, value);
        return true;
    }
    case PPC_OP_MTSRIN: {
        require_supervisor(b);
        DolIRValue index = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32,
            binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I32, gpr(b, i->rB), c32(b, 28)),
            c32(b, 15));
        DolIRValue value = gpr(b, i->rS);
        for (u32 sr = 0; sr < 16; sr++) {
            DolIRStateSlot slot = (DolIRStateSlot)(DOLIR_STATE_SR0 + sr);
            write_slot(b, slot, select_value(b, DOLIR_TYPE_I32,
                binary(b, DOLIR_OP_ICMP_EQ, DOLIR_TYPE_I1, index, c32(b, sr)),
                value, read_slot(b, slot)));
        }
        return true;
    }
    case PPC_OP_MFTB: {
        if (i->spr != 268 && i->spr != 269) {
            DolIRValue value = dolir_append(b->function, b->block,
                DOLIR_OP_HELPER_CALL, DOLIR_TYPE_I32, NULL, 0, i->spr,
                DOLIR_HELPER_SPR_READ, i->address,
                DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                DOLIR_EFFECT_MAY_EXIT | DOLIR_EFFECT_MAY_RAISE |
                DOLIR_EFFECT_BARRIER);
            set_gpr(b, i->rD, value);
            return true;
        }
        DolIRValue timebase = read_slot(b, DOLIR_STATE_TIMEBASE);
        if (i->spr == 269)
            timebase = binary(b, DOLIR_OP_LSHR, DOLIR_TYPE_I64,
                              timebase, c64(b, 32));
        set_gpr(b, i->rD, trunc_value(b, DOLIR_TYPE_I32, timebase));
        return true;
    }
    case PPC_OP_TLBIE: {
        DolIRValue address = gpr(b, i->rB);
        dolir_append(b->function, b->block, DOLIR_OP_HELPER_CALL,
                     DOLIR_TYPE_VOID, &address, 1, 0,
                     DOLIR_HELPER_TLBIE, i->address,
                     DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                     DOLIR_EFFECT_MAY_EXIT | DOLIR_EFFECT_MAY_RAISE |
                     DOLIR_EFFECT_BARRIER);
        return true;
    }
    case PPC_OP_SYNC: case PPC_OP_EIEIO: case PPC_OP_ISYNC:
    case PPC_OP_TLBSYNC:
        dolir_append(b->function, b->block, DOLIR_OP_HELPER_CALL,
                     DOLIR_TYPE_VOID, NULL, 0, 0,
                     DOLIR_HELPER_MEMORY_FENCE, i->address,
                     DOLIR_EFFECT_BARRIER | DOLIR_EFFECT_READ_MEMORY |
                     DOLIR_EFFECT_WRITE_MEMORY);
        return true;
    default:
        return false;
    }
}

static bool block_for_address(const DolIRFunction* function, u32 address, u32* block) {
    if (address < function->guest_start || address >= function->guest_end ||
        ((address - function->guest_start) & 3u))
        return false;
    u32 index = (address - function->guest_start) / 4u;
    if (index >= function->block_count)
        return false;
    *block = index;
    return true;
}

static void direct_target(Builder* b, u32 address, u32 slot) {
    b->block->terminator.target_addresses[slot] = address;
    if (!block_for_address(b->function, address, &b->block->terminator.targets[slot]))
        b->block->terminator.targets[slot] = DOLIR_NO_BLOCK;
}

static bool lower_control(Builder* b, u32 index, u32 count) {
    const PPCInst* i = b->inst;
    DolIRTerminator* term = &b->block->terminator;
    switch (i->op) {
    case PPC_OP_B:
        if (i->lk)
            write_slot(b, DOLIR_STATE_LR, c32(b, i->address + 4u));
        term->kind = DOLIR_TERM_BRANCH;
        term->linked = i->lk;
        term->guest_pc = i->address;
        direct_target(b, i->branch_target, 0);
        return true;
    case PPC_OP_BC:
        if (i->lk)
            write_slot(b, DOLIR_STATE_LR, c32(b, i->address + 4u));
        term->kind = DOLIR_TERM_COND_BRANCH;
        term->condition = branch_condition(b);
        term->linked = i->lk;
        term->guest_pc = i->address;
        direct_target(b, i->branch_target, 0);
        direct_target(b, i->address + 4u, 1);
        return true;
    case PPC_OP_BCLR: case PPC_OP_BCCTR:
        term->kind = DOLIR_TERM_INDIRECT;
        term->condition = branch_condition(b);
        term->target_value = binary(b, DOLIR_OP_AND, DOLIR_TYPE_I32,
                                    read_slot(b, i->op == PPC_OP_BCLR ? DOLIR_STATE_LR : DOLIR_STATE_CTR),
                                    c32(b, ~3u));
        if (i->lk)
            write_slot(b, DOLIR_STATE_LR, c32(b, i->address + 4u));
        term->linked = i->lk;
        term->guest_pc = i->address;
        direct_target(b, i->address + 4u, 1);
        return true;
    case PPC_OP_SC:
        term->kind = DOLIR_TERM_SYSTEM_CALL;
        term->guest_pc = i->address;
        return true;
    case PPC_OP_RFI:
        term->kind = DOLIR_TERM_RFI;
        term->guest_pc = i->address;
        return true;
    default:
        (void)index;
        (void)count;
        return false;
    }
}

bool dolir_build_chunk(DolIRModule* module, const PPCInst* insts, u32 count,
                       u32 guest_start) {
    if (!module || !insts || !count)
        return false;
    char name[64];
    snprintf(name, sizeof(name), "func_%08X", guest_start);
    DolIRFunction* function = dolir_add_function(module, name, guest_start,
                                                  guest_start + count * 4u);
    if (!function)
        return false;
    for (u32 n = 0; n < count; n++)
        if (!dolir_add_block(function, insts[n].address))
            return false;

    for (u32 n = 0; n < count; n++) {
        Builder b = {function, &function->blocks[n], &insts[n]};
        b.block->raw = b.inst->raw;
        b.block->cycle_cost = dolir_instruction_cycle_cost(b.inst);
        if (b.inst->embedded_data) {
            b.block->terminator.kind = DOLIR_TERM_FALLBACK;
            b.block->terminator.guest_pc = b.inst->address;
            b.block->terminator.raw = b.inst->raw;
            continue;
        }
        if (ppc_op_uses_fpu(b.inst->op))
            dolir_append(function, b.block, DOLIR_OP_HELPER_CALL,
                         DOLIR_TYPE_I1, NULL, 0, b.inst->address,
                         DOLIR_HELPER_FP_AVAILABLE, b.inst->address,
                         DOLIR_EFFECT_READ_STATE | DOLIR_EFFECT_WRITE_STATE |
                         DOLIR_EFFECT_MAY_EXIT | DOLIR_EFFECT_MAY_RAISE);
        if (lower_control(&b, n, count))
            continue;
        bool lowered = lower_integer(&b) || lower_memory(&b) ||
                       lower_float_memory(&b) || lower_psq(&b) || lower_float(&b) ||
                       lower_paired(&b) || lower_state(&b);
        if (lowered) {
            normal_fallthrough(&b, n, count);
        } else {
            b.block->terminator.kind = DOLIR_TERM_FALLBACK;
            b.block->terminator.guest_pc = b.inst->address;
            b.block->terminator.raw = b.inst->raw;
        }
    }
    dolir_analyze_addresses(function);
    return true;
}
