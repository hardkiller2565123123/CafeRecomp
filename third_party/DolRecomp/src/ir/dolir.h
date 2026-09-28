#ifndef DOLRECOMP_DOLIR_H
#define DOLRECOMP_DOLIR_H

#include "common/types.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOLIR_NO_VALUE 0u
#define DOLIR_NO_BLOCK 0xFFFFFFFFu
#define DOLIR_STATE_MASK_WORDS ((DOLIR_STATE_COUNT + 63u) / 64u)

typedef u32 DolIRValue;

typedef enum {
    DOLIR_TYPE_VOID,
    DOLIR_TYPE_I1,
    DOLIR_TYPE_I8,
    DOLIR_TYPE_I16,
    DOLIR_TYPE_I32,
    DOLIR_TYPE_I64,
    DOLIR_TYPE_F32,
    DOLIR_TYPE_F64,
    DOLIR_TYPE_V2F32,
    DOLIR_TYPE_V2F64,
} DolIRType;

typedef enum {
    DOLIR_STATE_GPR0,
    DOLIR_STATE_GPR31 = DOLIR_STATE_GPR0 + 31,
    DOLIR_STATE_FPR0,
    DOLIR_STATE_FPR31 = DOLIR_STATE_FPR0 + 31,
    DOLIR_STATE_PS1_0,
    DOLIR_STATE_PS1_31 = DOLIR_STATE_PS1_0 + 31,
    DOLIR_STATE_PC,
    DOLIR_STATE_LR,
    DOLIR_STATE_CTR,
    DOLIR_STATE_CR,
    // Virtual SSA-only CR fields. The LLVM backend extracts these from the
    // architectural packed CR and repacks them only at observation boundaries.
    DOLIR_STATE_CR0,
    DOLIR_STATE_CR7 = DOLIR_STATE_CR0 + 7,
    DOLIR_STATE_XER,
    DOLIR_STATE_XER_CA,
    DOLIR_STATE_XER_OV,
    DOLIR_STATE_XER_SO,
    DOLIR_STATE_FPSCR,
    DOLIR_STATE_MSR,
    DOLIR_STATE_SRR0,
    DOLIR_STATE_SRR1,
    DOLIR_STATE_DAR,
    DOLIR_STATE_DSISR,
    DOLIR_STATE_EAR,
    DOLIR_STATE_HID2,
    DOLIR_STATE_TIMEBASE,
    DOLIR_STATE_SR0,
    DOLIR_STATE_SR15 = DOLIR_STATE_SR0 + 15,
    DOLIR_STATE_GQR0,
    DOLIR_STATE_GQR7 = DOLIR_STATE_GQR0 + 7,
    DOLIR_STATE_EXCEPTION,
    DOLIR_STATE_PROGRAM_EXCEPTION,
    DOLIR_STATE_RESERVE_ADDR,
    DOLIR_STATE_RESERVE_VALID,
    DOLIR_STATE_DOWNCOUNT,
    DOLIR_STATE_COUNT,
} DolIRStateSlot;

typedef enum {
    DOLIR_OP_PHI,
    DOLIR_OP_CONSTANT,
    DOLIR_OP_STATE_READ,
    DOLIR_OP_STATE_WRITE,
    DOLIR_OP_ADD,
    DOLIR_OP_SUB,
    DOLIR_OP_MUL,
    DOLIR_OP_UDIV,
    DOLIR_OP_SDIV,
    DOLIR_OP_AND,
    DOLIR_OP_OR,
    DOLIR_OP_XOR,
    DOLIR_OP_NOT,
    DOLIR_OP_SHL,
    DOLIR_OP_LSHR,
    DOLIR_OP_ASHR,
    DOLIR_OP_ROTL,
    DOLIR_OP_CLZ,
    DOLIR_OP_BSWAP,
    DOLIR_OP_TRUNC,
    DOLIR_OP_ZEXT,
    DOLIR_OP_SEXT,
    DOLIR_OP_BITCAST,
    DOLIR_OP_ICMP_EQ,
    DOLIR_OP_ICMP_NE,
    DOLIR_OP_ICMP_ULT,
    DOLIR_OP_ICMP_ULE,
    DOLIR_OP_ICMP_SLT,
    DOLIR_OP_ICMP_SLE,
    DOLIR_OP_FCMP_OEQ,
    DOLIR_OP_FCMP_OLT,
    DOLIR_OP_FCMP_OGE,
    DOLIR_OP_SELECT,
    DOLIR_OP_FADD,
    DOLIR_OP_FSUB,
    DOLIR_OP_FMUL,
    DOLIR_OP_FDIV,
    DOLIR_OP_FNEG,
    DOLIR_OP_FABS,
    DOLIR_OP_FPTRUNC,
    DOLIR_OP_FPEXT,
    DOLIR_OP_VECTOR_BUILD,
    DOLIR_OP_VECTOR_EXTRACT,
    DOLIR_OP_VECTOR_SHUFFLE,
    DOLIR_OP_GUEST_LOAD,
    DOLIR_OP_GUEST_STORE,
    DOLIR_OP_HELPER_CALL,
} DolIROp;

typedef enum {
    DOLIR_HELPER_FALLBACK_INSTRUCTION,
    DOLIR_HELPER_FP_AVAILABLE,
    DOLIR_HELPER_SYSTEM_CALL,
    DOLIR_HELPER_RFI,
    DOLIR_HELPER_PROGRAM_EXCEPTION,
    DOLIR_HELPER_FPSCR_UPDATED,
    DOLIR_HELPER_PSQ_LOAD,
    DOLIR_HELPER_PSQ_STORE,
    DOLIR_HELPER_DCBZ_L,
    DOLIR_HELPER_ECIWX,
    DOLIR_HELPER_ECOWX,
    DOLIR_HELPER_TLBIE,
    DOLIR_HELPER_MEMORY_FENCE,
    DOLIR_HELPER_EXACT_FLOAT,
    DOLIR_HELPER_STORE_CONDITIONAL,
    DOLIR_HELPER_FPSCR_BIT,
    DOLIR_HELPER_SPR_READ,
    DOLIR_HELPER_SPR_WRITE,
    DOLIR_HELPER_LSWX,
    DOLIR_HELPER_CACHE_CONTROL,
    DOLIR_HELPER_EXACT_PAIRED,
} DolIRHelper;

typedef enum {
    DOLIR_PROGRAM_TRAP = 0x00020000u,
    DOLIR_PROGRAM_PRIV = 0x00040000u,
} DolIRProgramException;

typedef enum {
    DOLIR_EXACT_FRES,
    DOLIR_EXACT_FRSQRTE,
    DOLIR_EXACT_FMADD,
    DOLIR_EXACT_FMSUB,
    DOLIR_EXACT_FNMADD,
    DOLIR_EXACT_FNMSUB,
    DOLIR_EXACT_FMADDS,
    DOLIR_EXACT_FMSUBS,
    DOLIR_EXACT_FNMADDS,
    DOLIR_EXACT_FNMSUBS,
    DOLIR_EXACT_FCTIW,
    DOLIR_EXACT_FCTIWZ,
    DOLIR_EXACT_FCMPU,
    DOLIR_EXACT_FCMPO,
    DOLIR_EXACT_FADDS,
    DOLIR_EXACT_FSUBS,
    DOLIR_EXACT_FMULS,
    DOLIR_EXACT_FDIVS,
    DOLIR_EXACT_FADD,
    DOLIR_EXACT_FSUB,
    DOLIR_EXACT_FMUL,
    DOLIR_EXACT_FDIV,
    DOLIR_EXACT_FRSP,
} DolIRExactFloat;

typedef enum {
    DOLIR_EXACT_PS_ADD,
    DOLIR_EXACT_PS_SUB,
    DOLIR_EXACT_PS_MUL,
    DOLIR_EXACT_PS_DIV,
    DOLIR_EXACT_PS_MADD,
    DOLIR_EXACT_PS_MSUB,
    DOLIR_EXACT_PS_NMADD,
    DOLIR_EXACT_PS_NMSUB,
    DOLIR_EXACT_PS_MADDS0,
    DOLIR_EXACT_PS_MADDS1,
    DOLIR_EXACT_PS_SUM0,
    DOLIR_EXACT_PS_SUM1,
    DOLIR_EXACT_PS_MULS0,
    DOLIR_EXACT_PS_MULS1,
    DOLIR_EXACT_PS_RES,
    DOLIR_EXACT_PS_RSQRTE,
    DOLIR_EXACT_PS_CMPU0,
    DOLIR_EXACT_PS_CMPO0,
    DOLIR_EXACT_PS_CMPU1,
    DOLIR_EXACT_PS_CMPO1,
} DolIRExactPaired;

typedef enum {
    DOLIR_EFFECT_NONE = 0,
    DOLIR_EFFECT_READ_STATE = 1u << 0,
    DOLIR_EFFECT_WRITE_STATE = 1u << 1,
    DOLIR_EFFECT_READ_MEMORY = 1u << 2,
    DOLIR_EFFECT_WRITE_MEMORY = 1u << 3,
    DOLIR_EFFECT_MAY_EXIT = 1u << 4,
    DOLIR_EFFECT_MAY_RAISE = 1u << 5,
    DOLIR_EFFECT_BARRIER = 1u << 6,
} DolIREffect;

typedef enum {
    DOLIR_ADDRESS_UNKNOWN,
    DOLIR_ADDRESS_GUEST_DATA,
    DOLIR_ADDRESS_MEM1,
    DOLIR_ADDRESS_MEM2,
    DOLIR_ADDRESS_PHYSICAL,
    DOLIR_ADDRESS_MMIO,
    DOLIR_ADDRESS_FIFO,
} DolIRAddressDomain;

typedef struct {
    DolIROp op;
    DolIRType type;
    DolIRValue result;
    DolIRValue operands[4];
    u8 operand_count;
    u32 aux;
    u64 immediate;
    u32 guest_pc;
    u32 effects;
    u64 state_uses[DOLIR_STATE_MASK_WORDS];
    u64 state_defs[DOLIR_STATE_MASK_WORDS];
    DolIRAddressDomain address_domain;
    u32 address_lower;
    u32 address_upper;
    bool exact_fp;
} DolIRInstruction;

typedef enum {
    DOLIR_TERM_NONE,
    DOLIR_TERM_FALLTHROUGH,
    DOLIR_TERM_BRANCH,
    DOLIR_TERM_COND_BRANCH,
    DOLIR_TERM_INDIRECT,
    DOLIR_TERM_RETURN,
    DOLIR_TERM_SIDE_EXIT,
    DOLIR_TERM_FALLBACK,
    DOLIR_TERM_SYSTEM_CALL,
    DOLIR_TERM_RFI,
} DolIRTerminatorKind;

typedef struct {
    DolIRTerminatorKind kind;
    DolIRValue condition;
    DolIRValue target_value;
    u32 targets[2];
    u32 target_addresses[2];
    u32 guest_pc;
    u32 raw;
    bool linked;
} DolIRTerminator;

typedef struct {
    u32 guest_address;
    u32 raw;
    u32 cycle_cost;
    DolIRInstruction* instructions;
    u32 instruction_count;
    u32 instruction_capacity;
    DolIRTerminator terminator;
} DolIRBlock;

typedef struct {
    char name[64];
    u32 guest_start;
    u32 guest_end;
    DolIRBlock* blocks;
    u32 block_count;
    u32 block_capacity;
    DolIRType* value_types;
    u32 value_count;
    u32 value_capacity;
} DolIRFunction;

typedef struct {
    DolIRFunction* functions;
    u32 function_count;
    u32 function_capacity;
} DolIRModule;

void dolir_module_init(DolIRModule* module);
void dolir_module_free(DolIRModule* module);
DolIRFunction* dolir_add_function(DolIRModule* module, const char* name,
                                  u32 guest_start, u32 guest_end);
DolIRBlock* dolir_add_block(DolIRFunction* function, u32 guest_address);
DolIRValue dolir_append(DolIRFunction* function, DolIRBlock* block,
                        DolIROp op, DolIRType type, const DolIRValue* operands,
                        u8 operand_count, u64 immediate, u32 aux, u32 guest_pc,
                        u32 effects);
DolIRValue dolir_constant(DolIRFunction* function, DolIRBlock* block,
                          DolIRType type, u64 bits, u32 guest_pc);
DolIRValue dolir_state_read(DolIRFunction* function, DolIRBlock* block,
                            DolIRStateSlot slot, u32 guest_pc);
bool dolir_state_write(DolIRFunction* function, DolIRBlock* block,
                       DolIRStateSlot slot, DolIRValue value, u32 guest_pc);
DolIRType dolir_state_type(DolIRStateSlot slot);
const char* dolir_type_name(DolIRType type);
const char* dolir_op_name(DolIROp op);
bool dolir_verify(const DolIRModule* module, FILE* diagnostics);
void dolir_dump(const DolIRModule* module, FILE* out);
void dolir_populate_effects(DolIRInstruction* instruction);
bool dolir_state_mask_test(const u64* mask, DolIRStateSlot slot);
void dolir_analyze_addresses(DolIRFunction* function);
const char* dolir_address_domain_name(DolIRAddressDomain domain);

#ifdef __cplusplus
}
#endif

#endif
