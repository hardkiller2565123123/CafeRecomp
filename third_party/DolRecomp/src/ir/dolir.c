#include "ir/dolir.h"

#include <stdlib.h>
#include <string.h>

static bool grow_array(void** data, u32* capacity, u32 count, size_t element_size) {
    if (count < *capacity)
        return true;
    u32 next = *capacity ? *capacity * 2u : 16u;
    void* resized = realloc(*data, (size_t)next * element_size);
    if (!resized)
        return false;
    *data = resized;
    *capacity = next;
    return true;
}

void dolir_module_init(DolIRModule* module) {
    memset(module, 0, sizeof(*module));
}

void dolir_module_free(DolIRModule* module) {
    if (!module)
        return;
    for (u32 f = 0; f < module->function_count; f++) {
        DolIRFunction* function = &module->functions[f];
        for (u32 b = 0; b < function->block_count; b++)
            free(function->blocks[b].instructions);
        free(function->blocks);
        free(function->value_types);
    }
    free(module->functions);
    memset(module, 0, sizeof(*module));
}

DolIRFunction* dolir_add_function(DolIRModule* module, const char* name,
                                  u32 guest_start, u32 guest_end) {
    if (!grow_array((void**)&module->functions, &module->function_capacity,
                    module->function_count, sizeof(*module->functions)))
        return NULL;
    DolIRFunction* function = &module->functions[module->function_count++];
    memset(function, 0, sizeof(*function));
    snprintf(function->name, sizeof(function->name), "%s", name ? name : "region");
    function->guest_start = guest_start;
    function->guest_end = guest_end;
    if (!grow_array((void**)&function->value_types, &function->value_capacity,
                    0, sizeof(*function->value_types)))
        return NULL;
    function->value_types[0] = DOLIR_TYPE_VOID;
    function->value_count = 1;
    return function;
}

DolIRBlock* dolir_add_block(DolIRFunction* function, u32 guest_address) {
    if (!grow_array((void**)&function->blocks, &function->block_capacity,
                    function->block_count, sizeof(*function->blocks)))
        return NULL;
    DolIRBlock* block = &function->blocks[function->block_count++];
    memset(block, 0, sizeof(*block));
    block->guest_address = guest_address;
    block->terminator.targets[0] = DOLIR_NO_BLOCK;
    block->terminator.targets[1] = DOLIR_NO_BLOCK;
    return block;
}

DolIRValue dolir_append(DolIRFunction* function, DolIRBlock* block,
                        DolIROp op, DolIRType type, const DolIRValue* operands,
                        u8 operand_count, u64 immediate, u32 aux, u32 guest_pc,
                        u32 effects) {
    if (operand_count > 4 ||
        !grow_array((void**)&block->instructions, &block->instruction_capacity,
                    block->instruction_count, sizeof(*block->instructions)))
        return DOLIR_NO_VALUE;

    DolIRValue result = DOLIR_NO_VALUE;
    if (type != DOLIR_TYPE_VOID) {
        if (!grow_array((void**)&function->value_types, &function->value_capacity,
                        function->value_count, sizeof(*function->value_types)))
            return DOLIR_NO_VALUE;
        result = function->value_count++;
        function->value_types[result] = type;
    }

    DolIRInstruction* inst = &block->instructions[block->instruction_count++];
    memset(inst, 0, sizeof(*inst));
    inst->op = op;
    inst->type = type;
    inst->result = result;
    inst->operand_count = operand_count;
    for (u8 i = 0; i < operand_count; i++)
        inst->operands[i] = operands[i];
    inst->immediate = immediate;
    inst->aux = aux;
    inst->guest_pc = guest_pc;
    inst->effects = effects;
    dolir_populate_effects(inst);
    return result;
}

DolIRValue dolir_constant(DolIRFunction* function, DolIRBlock* block,
                          DolIRType type, u64 bits, u32 guest_pc) {
    return dolir_append(function, block, DOLIR_OP_CONSTANT, type, NULL, 0,
                        bits, 0, guest_pc, DOLIR_EFFECT_NONE);
}

DolIRType dolir_state_type(DolIRStateSlot slot) {
    if (slot >= DOLIR_STATE_FPR0 && slot <= DOLIR_STATE_PS1_31)
        return DOLIR_TYPE_F64;
    if (slot == DOLIR_STATE_TIMEBASE || slot == DOLIR_STATE_DOWNCOUNT)
        return DOLIR_TYPE_I64;
    if (slot == DOLIR_STATE_RESERVE_VALID || slot == DOLIR_STATE_XER_CA ||
        slot == DOLIR_STATE_XER_OV || slot == DOLIR_STATE_XER_SO)
        return DOLIR_TYPE_I1;
    return DOLIR_TYPE_I32;
}

DolIRValue dolir_state_read(DolIRFunction* function, DolIRBlock* block,
                            DolIRStateSlot slot, u32 guest_pc) {
    return dolir_append(function, block, DOLIR_OP_STATE_READ,
                        dolir_state_type(slot), NULL, 0, 0, (u32)slot,
                        guest_pc, DOLIR_EFFECT_READ_STATE);
}

bool dolir_state_write(DolIRFunction* function, DolIRBlock* block,
                       DolIRStateSlot slot, DolIRValue value, u32 guest_pc) {
    dolir_append(function, block, DOLIR_OP_STATE_WRITE, DOLIR_TYPE_VOID,
                 &value, 1, 0, (u32)slot, guest_pc, DOLIR_EFFECT_WRITE_STATE);
    return block->instruction_count != 0;
}

const char* dolir_type_name(DolIRType type) {
    static const char* names[] = {"void", "i1", "i8", "i16", "i32", "i64",
                                  "f32", "f64", "v2f32", "v2f64"};
    return type <= DOLIR_TYPE_V2F64 ? names[type] : "invalid";
}

const char* dolir_op_name(DolIROp op) {
    static const char* names[] = {
        "phi", "const", "state.read", "state.write", "add", "sub", "mul",
        "udiv", "sdiv", "and", "or", "xor", "not", "shl", "lshr", "ashr",
        "rotl", "clz", "bswap", "trunc", "zext", "sext", "bitcast", "icmp.eq",
        "icmp.ne", "icmp.ult", "icmp.ule", "icmp.slt", "icmp.sle", "fcmp.oeq",
        "fcmp.olt", "fcmp.oge", "select", "fadd", "fsub", "fmul", "fdiv",
        "fneg", "fabs", "fptrunc", "fpext", "vector.build", "vector.extract",
        "vector.shuffle", "guest.load", "guest.store", "helper.call"
    };
    return op <= DOLIR_OP_HELPER_CALL ? names[op] : "invalid";
}

static bool verify_function(const DolIRFunction* function, FILE* diagnostics) {
    bool ok = true;
    if (function->guest_start >= function->guest_end || function->block_count == 0) {
        fprintf(diagnostics, "dolir: invalid function %s range or empty CFG\n", function->name);
        ok = false;
    }
    for (u32 b = 0; b < function->block_count; b++) {
        const DolIRBlock* block = &function->blocks[b];
        if (block->terminator.kind == DOLIR_TERM_NONE) {
            fprintf(diagnostics, "dolir: block 0x%08X has no terminator\n", block->guest_address);
            ok = false;
        }
        for (u32 i = 0; i < block->instruction_count; i++) {
            const DolIRInstruction* inst = &block->instructions[i];
            if (inst->result >= function->value_count || inst->operand_count > 4) {
                fprintf(diagnostics, "dolir: invalid instruction at 0x%08X\n", inst->guest_pc);
                ok = false;
            }
            for (u8 n = 0; n < inst->operand_count; n++) {
                if (inst->operands[n] == DOLIR_NO_VALUE ||
                    inst->operands[n] >= function->value_count) {
                    fprintf(diagnostics, "dolir: bad operand at 0x%08X\n", inst->guest_pc);
                    ok = false;
                }
            }
            if ((inst->op == DOLIR_OP_STATE_READ || inst->op == DOLIR_OP_STATE_WRITE) &&
                inst->aux >= DOLIR_STATE_COUNT) {
                fprintf(diagnostics, "dolir: bad state slot at 0x%08X\n", inst->guest_pc);
                ok = false;
            }
            if (inst->address_domain > DOLIR_ADDRESS_FIFO ||
                (inst->address_domain != DOLIR_ADDRESS_UNKNOWN &&
                 inst->address_lower > inst->address_upper)) {
                fprintf(diagnostics, "dolir: bad address provenance at 0x%08X\n",
                        inst->guest_pc);
                ok = false;
            }
        }
        for (u32 n = 0; n < 2; n++) {
            u32 target = block->terminator.targets[n];
            if (target != DOLIR_NO_BLOCK && target >= function->block_count) {
                fprintf(diagnostics, "dolir: bad branch target from 0x%08X\n", block->guest_address);
                ok = false;
            }
        }
    }
    return ok;
}

bool dolir_verify(const DolIRModule* module, FILE* diagnostics) {
    if (!module || module->function_count == 0) {
        fprintf(diagnostics, "dolir: empty module\n");
        return false;
    }
    bool ok = true;
    for (u32 f = 0; f < module->function_count; f++)
        ok = verify_function(&module->functions[f], diagnostics) && ok;
    return ok;
}

void dolir_dump(const DolIRModule* module, FILE* out) {
    for (u32 f = 0; f < module->function_count; f++) {
        const DolIRFunction* function = &module->functions[f];
        fprintf(out, "func %s [0x%08X,0x%08X) {\n", function->name,
                function->guest_start, function->guest_end);
        for (u32 b = 0; b < function->block_count; b++) {
            const DolIRBlock* block = &function->blocks[b];
            fprintf(out, "  b%u @0x%08X:\n", b, block->guest_address);
            for (u32 i = 0; i < block->instruction_count; i++) {
                const DolIRInstruction* inst = &block->instructions[i];
                if (inst->result)
                    fprintf(out, "    %%%u:%s = ", inst->result, dolir_type_name(inst->type));
                else
                    fprintf(out, "    ");
                fprintf(out, "%s", dolir_op_name(inst->op));
                for (u8 n = 0; n < inst->operand_count; n++)
                    fprintf(out, "%s%%%u", n ? ", " : " ", inst->operands[n]);
                if (inst->op == DOLIR_OP_CONSTANT)
                    fprintf(out, " 0x%llX", (unsigned long long)inst->immediate);
                if (inst->op == DOLIR_OP_STATE_READ || inst->op == DOLIR_OP_STATE_WRITE)
                    fprintf(out, " slot=%u", inst->aux);
                if (inst->op == DOLIR_OP_GUEST_LOAD ||
                    inst->op == DOLIR_OP_GUEST_STORE)
                    fprintf(out, " address=%s[0x%08X,0x%08X]",
                            dolir_address_domain_name(inst->address_domain),
                            inst->address_lower, inst->address_upper);
                fprintf(out, " ; pc=0x%08X\n", inst->guest_pc);
            }
            fprintf(out, "    term.%u\n", block->terminator.kind);
        }
        fprintf(out, "}\n");
    }
}
