#include "ir/dolir.h"

#include "cpu/cpu.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    u64 bits;
    bool known;
} ConstantValue;

static u32 type_width(DolIRType type) {
    switch (type) {
    case DOLIR_TYPE_I1: return 1;
    case DOLIR_TYPE_I8: return 8;
    case DOLIR_TYPE_I16: return 16;
    case DOLIR_TYPE_I32: return 32;
    case DOLIR_TYPE_I64: return 64;
    default: return 0;
    }
}

static u64 truncate_bits(u64 value, u32 width) {
    return width == 64 ? value : value & ((1ull << width) - 1ull);
}

static ConstantValue unknown_value(void) {
    ConstantValue value = {0, false};
    return value;
}

static ConstantValue known_value(u64 bits, u32 width) {
    ConstantValue value = {truncate_bits(bits, width), true};
    return value;
}

static ConstantValue unary_value(const DolIRInstruction* instruction,
                                 ConstantValue operand) {
    const u32 width = type_width(instruction->type);
    if (!operand.known || !width)
        return unknown_value();
    switch (instruction->op) {
    case DOLIR_OP_NOT:
        return known_value(~operand.bits, width);
    case DOLIR_OP_TRUNC:
    case DOLIR_OP_ZEXT:
        return known_value(operand.bits, width);
    case DOLIR_OP_CLZ: {
        u32 count = 0;
        while (count < width && !(operand.bits & (1ull << (width - count - 1u))))
            count++;
        return known_value(count, width);
    }
    default:
        return unknown_value();
    }
}

static ConstantValue binary_value(const DolIRInstruction* instruction,
                                  ConstantValue left, ConstantValue right) {
    const u32 width = type_width(instruction->type);
    if (!left.known || !right.known || !width)
        return unknown_value();
    switch (instruction->op) {
    case DOLIR_OP_ADD: return known_value(left.bits + right.bits, width);
    case DOLIR_OP_SUB: return known_value(left.bits - right.bits, width);
    case DOLIR_OP_MUL: return known_value(left.bits * right.bits, width);
    case DOLIR_OP_UDIV:
        return right.bits ? known_value(left.bits / right.bits, width)
                          : unknown_value();
    case DOLIR_OP_AND: return known_value(left.bits & right.bits, width);
    case DOLIR_OP_OR: return known_value(left.bits | right.bits, width);
    case DOLIR_OP_XOR: return known_value(left.bits ^ right.bits, width);
    case DOLIR_OP_SHL:
        return right.bits < width ? known_value(left.bits << right.bits, width)
                                  : unknown_value();
    case DOLIR_OP_LSHR:
        return right.bits < width ? known_value(left.bits >> right.bits, width)
                                  : unknown_value();
    case DOLIR_OP_ROTL: {
        u32 shift = (u32)right.bits & (width - 1u);
        if (!shift)
            return known_value(left.bits, width);
        return known_value((left.bits << shift) |
                           (left.bits >> (width - shift)), width);
    }
    case DOLIR_OP_ICMP_EQ: return known_value(left.bits == right.bits, 1);
    case DOLIR_OP_ICMP_NE: return known_value(left.bits != right.bits, 1);
    case DOLIR_OP_ICMP_ULT: return known_value(left.bits < right.bits, 1);
    case DOLIR_OP_ICMP_ULE: return known_value(left.bits <= right.bits, 1);
    default: return unknown_value();
    }
}

static DolIRAddressDomain classify_address(u32 address) {
    if (address >= 0xCC008000u && address < 0xCC009000u)
        return DOLIR_ADDRESS_FIFO;
    if (address >= 0xCC000000u && address < 0xCE000000u)
        return DOLIR_ADDRESS_MMIO;
    u32 normalized = address & ~0x40000000u;
    if (normalized >= GC_RAM_BASE &&
        normalized < GC_RAM_BASE + GC_MAIN_RAM_SIZE)
        return DOLIR_ADDRESS_MEM1;
    if (normalized >= WII_MEM2_BASE &&
        normalized < WII_MEM2_BASE + WII_MEM2_SIZE)
        return DOLIR_ADDRESS_MEM2;
    if (address < GC_RAM_BASE)
        return DOLIR_ADDRESS_PHYSICAL;
    if (address < 0xE0000000u)
        return DOLIR_ADDRESS_GUEST_DATA;
    return DOLIR_ADDRESS_UNKNOWN;
}

static u32 terminator_target_count(DolIRTerminatorKind kind) {
    if (kind == DOLIR_TERM_COND_BRANCH || kind == DOLIR_TERM_INDIRECT)
        return 2;
    return kind == DOLIR_TERM_BRANCH ? 1 : 0;
}

static void find_region_leaders(const DolIRFunction* function, bool* leaders) {
    leaders[0] = true;
    for (u32 index = 0; index < function->block_count; index++) {
        const DolIRTerminator* term = &function->blocks[index].terminator;
        if (term->kind == DOLIR_TERM_FALLBACK)
            leaders[index] = true;
        if (index + 1u < function->block_count &&
            term->kind != DOLIR_TERM_FALLTHROUGH)
            leaders[index + 1u] = true;
        for (u32 slot = 0; slot < terminator_target_count(term->kind); slot++)
            if (term->targets[slot] != DOLIR_NO_BLOCK)
                leaders[term->targets[slot]] = true;
    }
}

static ConstantValue instruction_value(const DolIRInstruction* instruction,
                                       const ConstantValue* values,
                                       const ConstantValue* state) {
    const u32 width = type_width(instruction->type);
    if (instruction->op == DOLIR_OP_CONSTANT && width)
        return known_value(instruction->immediate, width);
    if (instruction->op == DOLIR_OP_STATE_READ)
        return state[instruction->aux];
    if (instruction->operand_count == 1)
        return unary_value(instruction, values[instruction->operands[0]]);
    if (instruction->operand_count == 2)
        return binary_value(instruction, values[instruction->operands[0]],
                            values[instruction->operands[1]]);
    if (instruction->op == DOLIR_OP_SELECT && instruction->operand_count == 3) {
        ConstantValue condition = values[instruction->operands[0]];
        ConstantValue yes = values[instruction->operands[1]];
        ConstantValue no = values[instruction->operands[2]];
        if (condition.known)
            return condition.bits ? yes : no;
        if (yes.known && no.known && yes.bits == no.bits)
            return yes;
    }
    return unknown_value();
}

void dolir_analyze_addresses(DolIRFunction* function) {
    if (!function || !function->block_count || !function->value_count)
        return;
    bool* leaders = (bool*)calloc(function->block_count, sizeof(*leaders));
    ConstantValue* values =
        (ConstantValue*)calloc(function->value_count, sizeof(*values));
    if (!leaders || !values) {
        free(leaders);
        free(values);
        return;
    }
    ConstantValue state[DOLIR_STATE_COUNT];
    memset(state, 0, sizeof(state));
    find_region_leaders(function, leaders);
    for (u32 block_index = 0; block_index < function->block_count; block_index++) {
        DolIRBlock* block = &function->blocks[block_index];
        if (leaders[block_index])
            memset(state, 0, sizeof(state));
        for (u32 index = 0; index < block->instruction_count; index++) {
            DolIRInstruction* instruction = &block->instructions[index];
            instruction->address_domain = DOLIR_ADDRESS_UNKNOWN;
            instruction->address_lower = 0;
            instruction->address_upper = 0;
            if ((instruction->op == DOLIR_OP_GUEST_LOAD ||
                 instruction->op == DOLIR_OP_GUEST_STORE) &&
                values[instruction->operands[0]].known) {
                u32 address = (u32)values[instruction->operands[0]].bits;
                instruction->address_domain = classify_address(address);
                instruction->address_lower = address;
                instruction->address_upper = address;
            }
            ConstantValue result =
                instruction_value(instruction, values, state);
            if (instruction->result)
                values[instruction->result] = result;
            if (instruction->op == DOLIR_OP_STATE_WRITE)
                state[instruction->aux] =
                    values[instruction->operands[0]];
            for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++)
                if (dolir_state_mask_test(instruction->state_defs,
                                          (DolIRStateSlot)slot) &&
                    !(instruction->op == DOLIR_OP_STATE_WRITE &&
                      instruction->aux == slot))
                    state[slot] = unknown_value();
        }
    }
    free(leaders);
    free(values);
}

const char* dolir_address_domain_name(DolIRAddressDomain domain) {
    static const char* names[] = {
        "unknown", "guest-data", "mem1", "mem2", "physical", "mmio", "fifo"
    };
    return domain <= DOLIR_ADDRESS_FIFO ? names[domain] : "invalid";
}
