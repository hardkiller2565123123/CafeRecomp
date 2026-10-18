#include "backend/llvm/native_abi.h"

#include "backend/llvm/emitter.h"
#include "cpu/cpu.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <unordered_map>

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>

namespace {

bool nativeMemoryAccess(const DolIRInstruction &instruction) {
  if (instruction.address_domain != DOLIR_ADDRESS_MEM1 ||
      instruction.address_lower != instruction.address_upper)
    return false;
  const u32 width = instruction.aux & 0xffu;
  const u32 address = instruction.address_lower & ~0x40000000u;
  return width && width <= GC_MAIN_RAM_SIZE && address >= GC_RAM_BASE &&
         address - GC_RAM_BASE <= GC_MAIN_RAM_SIZE - width;
}

u32 nativeABIFlags(const DolIRFunction &function) {
  bool memory = false;
  for (u32 blockIndex = 0; blockIndex < function.block_count; blockIndex++) {
    const DolIRBlock &block = function.blocks[blockIndex];
    if (!block.cycle_cost || block.terminator.kind == DOLIR_TERM_FALLBACK ||
        block.terminator.kind == DOLIR_TERM_SYSTEM_CALL ||
        block.terminator.kind == DOLIR_TERM_RFI)
      return 0;
    for (u32 index = 0; index < block.instruction_count; index++) {
      const DolIRInstruction &instruction = block.instructions[index];
      if (instruction.op == DOLIR_OP_GUEST_LOAD ||
          instruction.op == DOLIR_OP_GUEST_STORE) {
        if (!nativeMemoryAccess(instruction))
          return 0;
        memory = true;
        continue;
      }
      if (instruction.op != DOLIR_OP_HELPER_CALL)
        continue;
      switch (static_cast<DolIRHelper>(instruction.aux)) {
      case DOLIR_HELPER_MEMORY_FENCE:
        break;
      default:
        return 0;
      }
    }
  }
  return static_cast<u32>(DOLLLVM_FUNCTION_ABI_NATIVE) |
         (memory ? static_cast<u32>(DOLLLVM_FUNCTION_ABI_NATIVE_MEMORY) : 0u);
}

DolLLVMFunctionRange *exactRange(std::vector<DolLLVMFunctionRange> &ranges,
                                 u32 start) {
  for (DolLLVMFunctionRange &range : ranges)
    if (range.start == start)
      return &range;
  return nullptr;
}

const DolLLVMFunctionRange *
addressRange(const std::vector<DolLLVMFunctionRange> &ranges, u32 address) {
  for (const DolLLVMFunctionRange &range : ranges)
    if (address >= range.start && address < range.end)
      return &range;
  return nullptr;
}

bool emptyABI(const DolLLVMFunctionRange &range) {
  if (range.abi_flags)
    return false;
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
    if (range.input_state[word] || range.output_state[word] ||
        range.escape_state[word])
      return false;
  return true;
}

} // namespace

namespace dolllvm {

void prepareModuleABIs(const DolIRModule &source,
                       std::vector<DolLLVMFunctionRange> &ranges) {
  std::vector<DolLLVMCallEdge> edges;
  for (u32 index = 0; index < source.function_count; index++) {
    const DolIRFunction &function = source.functions[index];
    DolLLVMFunctionRange *range = exactRange(ranges, function.guest_start);
    if (!range)
      continue;
    if (emptyABI(*range))
      dolllvm_analyze_function_abi(&function, range);
    for (u32 blockIndex = 0; blockIndex < function.block_count; blockIndex++) {
      const DolIRTerminator &term = function.blocks[blockIndex].terminator;
      u32 count = term.kind == DOLIR_TERM_COND_BRANCH ? 2u
                  : term.kind == DOLIR_TERM_FALLTHROUGH ||
                          term.kind == DOLIR_TERM_BRANCH
                      ? 1u
                  : term.kind == DOLIR_TERM_INDIRECT ? 2u
                                                     : 0u;
      for (u32 slot = 0; slot < count; slot++) {
        const DolLLVMFunctionRange *target =
            addressRange(ranges, term.target_addresses[slot]);
        if (target && target->start != range->start)
          edges.push_back({range->start, term.target_addresses[slot]});
      }
    }
  }
  dolllvm_propagate_function_abis(ranges.data(),
                                  static_cast<u32>(ranges.size()), edges.data(),
                                  static_cast<u32>(edges.size()));
}

bool FunctionEmitter::stateInput(const DolLLVMFunctionRange *range,
                                 DolIRStateSlot slot) const {
  return range && (dolir_state_mask_test(range->input_state, slot) ||
                   dolir_state_mask_test(range->escape_state, slot));
}

bool FunctionEmitter::stateOutput(const DolLLVMFunctionRange *range,
                                  DolIRStateSlot slot) const {
  return range && dolir_state_mask_test(range->output_state, slot);
}

llvm::Type *
FunctionEmitter::nativeResultType(const DolLLVMFunctionRange *range) {
  llvm::SmallVector<llvm::Type *, 32> fields;
  if (!cold_escapes_) {
    fields.push_back(llvm::Type::getInt32Ty(context_));
    fields.push_back(llvm::Type::getInt1Ty(context_));
  }
  if (cold_escapes_) {
    const u32 lanes = nativeResultLaneCount(range);
    if (!lanes)
      return llvm::Type::getVoidTy(context_);
    if (lanes == 1)
      return llvm::Type::getInt64Ty(context_);
    fields.assign(lanes, llvm::Type::getInt64Ty(context_));
  } else {
    for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
      auto stateSlot = static_cast<DolIRStateSlot>(slot);
      if (stateOutput(range, stateSlot))
        fields.push_back(type(dolir_state_type(stateSlot)));
    }
  }
  return llvm::StructType::get(context_, fields);
}

llvm::StructType *FunctionEmitter::chainType() {
  llvm::Type *pointer = llvm::PointerType::getUnqual(context_);
  const u32 bufferWords = intrinsic_escapes_ ? 5u : 64u;
  return llvm::StructType::get(context_,
                               {llvm::ArrayType::get(pointer, bufferWords),
                                llvm::Type::getInt64Ty(context_),
                                llvm::Type::getInt64Ty(context_),
                                llvm::Type::getInt64Ty(context_)});
}

llvm::FunctionType *
FunctionEmitter::bodyFunctionType(const DolLLVMFunctionRange *range) {
  llvm::Type *pointer = llvm::PointerType::getUnqual(context_);
  llvm::SmallVector<llvm::Type *, 32> arguments = {
      pointer, pointer, llvm::Type::getInt64Ty(context_)};
  const bool native =
      range && (range->abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE) != 0;
  if (native) {
    if (nativeCyclesInResult(range))
      arguments.push_back(llvm::Type::getInt64Ty(context_));
    for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
      auto stateSlot = static_cast<DolIRStateSlot>(slot);
      if (stateInput(range, stateSlot))
        arguments.push_back(type(dolir_state_type(stateSlot)));
    }
  }
  llvm::Type *result = native
                           ? static_cast<llvm::Type *>(nativeResultType(range))
                           : llvm::Type::getVoidTy(context_);
  return llvm::FunctionType::get(result, arguments, false);
}

llvm::CallingConv::ID FunctionEmitter::bodyCallingConvention() const {
  return llvm::CallingConv::Fast;
}

} // namespace dolllvm

extern "C" bool dolllvm_analyze_function_abi(const DolIRFunction *function,
                                             DolLLVMFunctionRange *range) {
  if (!function || !range || function->guest_start != range->start ||
      function->guest_end != range->end)
    return false;
  memset(range->input_state, 0, sizeof(range->input_state));
  memset(range->output_state, 0, sizeof(range->output_state));
  memset(range->escape_state, 0, sizeof(range->escape_state));
  for (u32 blockIndex = 0; blockIndex < function->block_count; blockIndex++) {
    const DolIRBlock &block = function->blocks[blockIndex];
    for (u32 index = 0; index < block.instruction_count; index++) {
      const DolIRInstruction &instruction = block.instructions[index];
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
        const u64 inputs =
            instruction.state_uses[word] | instruction.state_defs[word];
        range->input_state[word] |= inputs;
        range->output_state[word] |= instruction.state_defs[word];
      }
    }
  }
  range->abi_flags = nativeABIFlags(*function);
  return true;
}

extern "C" bool dolllvm_propagate_function_abis(DolLLVMFunctionRange *ranges,
                                                u32 range_count,
                                                const DolLLVMCallEdge *edges,
                                                u32 edge_count) {
  if ((!ranges && range_count) || (!edges && edge_count))
    return false;
  std::unordered_map<u32, u32> starts;
  std::vector<u32> order(range_count);
  for (u32 index = 0; index < range_count; index++) {
    starts.emplace(ranges[index].start, index);
    order[index] = index;
  }
  std::sort(order.begin(), order.end(), [&](u32 left, u32 right) {
    return ranges[left].start < ranges[right].start;
  });
  auto containing = [&](u32 address) -> u32 {
    auto position = std::upper_bound(
        order.begin(), order.end(), address,
        [&](u32 value, u32 index) { return value < ranges[index].start; });
    if (position == order.begin())
      return range_count;
    u32 index = *--position;
    return address < ranges[index].end ? index : range_count;
  };
  std::vector<std::vector<u32>> callers(range_count);
  std::vector<std::vector<u32>> callees(range_count);
  for (u32 edgeIndex = 0; edgeIndex < edge_count; edgeIndex++) {
    auto caller = starts.find(edges[edgeIndex].caller_start);
    u32 callee = containing(edges[edgeIndex].callee_address);
    if (caller == starts.end() || callee == range_count ||
        !(ranges[callee].abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE))
      continue;
    callers[callee].push_back(caller->second);
    if (ranges[caller->second].abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE)
      callees[caller->second].push_back(callee);
  }
  std::deque<u32> work;
  std::vector<bool> queued(range_count, true);
  for (u32 index = 0; index < range_count; index++)
    work.push_back(index);
  while (!work.empty()) {
    u32 callee = work.front();
    work.pop_front();
    queued[callee] = false;
    for (u32 caller : callers[callee]) {
      bool changed = false;
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
        const u64 inputs = ranges[caller].input_state[word];
        const u64 outputs = ranges[caller].output_state[word];
        ranges[caller].input_state[word] |= ranges[callee].input_state[word];
        ranges[caller].output_state[word] |= ranges[callee].output_state[word];
        changed |= inputs != ranges[caller].input_state[word] ||
                   outputs != ranges[caller].output_state[word];
      }
      if (changed && !queued[caller]) {
        queued[caller] = true;
        work.push_back(caller);
      }
    }
  }
  queued.assign(range_count, true);
  for (u32 index = 0; index < range_count; index++)
    work.push_back(index);
  while (!work.empty()) {
    const u32 caller = work.front();
    work.pop_front();
    queued[caller] = false;
    for (u32 callee : callees[caller]) {
      bool changed = false;
      for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++) {
        const u64 escape = ranges[callee].escape_state[word];
        ranges[callee].escape_state[word] |= ranges[caller].escape_state[word] |
                                             ranges[caller].output_state[word];
        changed |= escape != ranges[callee].escape_state[word];
      }
      if (changed && !queued[callee]) {
        queued[callee] = true;
        work.push_back(callee);
      }
    }
  }
  return true;
}
