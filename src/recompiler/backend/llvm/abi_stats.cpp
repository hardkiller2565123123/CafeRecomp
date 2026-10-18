#include "backend/llvm/llvm_backend.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>

#include <llvm/TargetParser/Triple.h>

namespace {

struct Counts {
  u32 integer = 0;
  u32 floating = 0;
};

Counts stateCounts(const u64 *mask) {
  Counts counts;
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!dolir_state_mask_test(mask, stateSlot))
      continue;
    if (dolir_state_type(stateSlot) == DOLIR_TYPE_F64)
      counts.floating++;
    else
      counts.integer++;
  }
  return counts;
}

Counts stateCounts(const u64 *first, const u64 *second) {
  u64 merged[DOLIR_STATE_MASK_WORDS];
  for (u32 word = 0; word < DOLIR_STATE_MASK_WORDS; word++)
    merged[word] = first[word] | second[word];
  return stateCounts(merged);
}

u32 stateWidth(DolIRStateSlot slot) {
  switch (dolir_state_type(slot)) {
  case DOLIR_TYPE_I1:
    return 1;
  case DOLIR_TYPE_I64:
  case DOLIR_TYPE_F64:
    return 64;
  default:
    return 32;
  }
}

u32 packedReturnLanes(const DolLLVMFunctionRange &range) {
  u32 lanes = 0;
  u32 bits = 0;
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!dolir_state_mask_test(range.output_state, stateSlot))
      continue;
    const u32 width = stateWidth(stateSlot);
    if (bits && bits + width > 64) {
      lanes++;
      bits = 0;
    }
    bits += width;
    if (bits == 64) {
      lanes++;
      bits = 0;
    }
  }
  return lanes + (bits != 0);
}

u32 envelopeBytes(const DolLLVMFunctionRange &range) {
  u32 size = 5;
  u32 alignment = 4;
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!dolir_state_mask_test(range.output_state, stateSlot))
      continue;
    const u32 width = stateWidth(stateSlot);
    const u32 bytes = std::max(1u, width / 8u);
    const u32 align = std::min(bytes, 8u);
    size = (size + align - 1u) & ~(align - 1u);
    size += bytes;
    alignment = std::max(alignment, align);
  }
  return (size + alignment - 1u) & ~(alignment - 1u);
}

u32 predictedStackArgs(const llvm::Triple &triple, Counts inputs,
                       u32 baseIntegerArgs) {
  if (triple.isX86() && triple.isOSWindows())
    return baseIntegerArgs + inputs.integer + inputs.floating > 4
               ? baseIntegerArgs + inputs.integer + inputs.floating - 4
               : 0u;
  const u32 integerRegisters = triple.isAArch64() ? 8u : 6u;
  const u32 floatingRegisters = 8u;
  const u32 integerAvailable = integerRegisters > baseIntegerArgs
                                   ? integerRegisters - baseIntegerArgs
                                   : 0u;
  return (inputs.integer > integerAvailable ? inputs.integer - integerAvailable
                                            : 0u) +
         (inputs.floating > floatingRegisters
              ? inputs.floating - floatingRegisters
              : 0u);
}

void printHistogram(FILE *output, const char *name,
                    const std::map<u32, u32> &histogram) {
  fprintf(output, "dolllvm abi %s", name);
  for (const auto &[value, count] : histogram)
    fprintf(output, " %u:%u", value, count);
  fputc('\n', output);
}

} // namespace

extern "C" void dolllvm_report_abi_stats(const DolLLVMFunctionRange *ranges,
                                         u32 rangeCount,
                                         const char *targetTriple,
                                         FILE *output) {
  if (!ranges || !targetTriple || !output)
    return;
  const llvm::Triple triple(targetTriple);
  const bool coldEscapes =
      triple.isX86() || (triple.isAArch64() && !triple.isOSWindows());
  const bool full = [] {
    const char *mode = std::getenv("DOLRECOMP_LLVM_ABI_STATS");
    return mode && !strcmp(mode, "full");
  }();
  u32 native = 0;
  u32 stackFunctions = 0;
  u32 sretFunctions = 0;
  u32 cycleMemoryFunctions = 0;
  u32 nativeMemoryFunctions = 0;
  std::map<u32, u32> inputHistogram;
  std::map<u32, u32> outputHistogram;
  for (u32 index = 0; index < rangeCount; index++) {
    const DolLLVMFunctionRange &range = ranges[index];
    if (!(range.abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE))
      continue;
    native++;
    const Counts inputs = stateCounts(range.input_state, range.escape_state);
    const Counts outputs = stateCounts(range.output_state);
    const Counts escapes = stateCounts(range.escape_state);
    const u32 stateReturnLanes = packedReturnLanes(range);
    const u32 returnRegisters = triple.isAArch64() ? 8u : 3u;
    const bool cycleReturn =
        coldEscapes && stateReturnLanes + 1u <= returnRegisters;
    const bool cycleMemory = coldEscapes && !cycleReturn;
    const u32 returnLanes =
        coldEscapes ? stateReturnLanes + static_cast<u32>(cycleReturn) : 0u;
    const u32 stackArgs =
        predictedStackArgs(triple, inputs, 3u + static_cast<u32>(cycleReturn));
    const bool sret =
        coldEscapes ? returnLanes > returnRegisters : envelopeBytes(range) > 16;
    stackFunctions += stackArgs != 0;
    sretFunctions += sret;
    cycleMemoryFunctions += cycleMemory;
    nativeMemoryFunctions +=
        (range.abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE_MEMORY) != 0;
    inputHistogram[inputs.integer + inputs.floating]++;
    outputHistogram[outputs.integer + outputs.floating]++;
    if (full)
      fprintf(output,
              "dolllvm abi fn=%08X in=%u/%u out=%u/%u escape=%u/%u stack=%u "
              "return_lanes=%u sret=%s cycle_memory=%s native_memory=%s\n",
              range.start, inputs.integer, inputs.floating, outputs.integer,
              outputs.floating, escapes.integer, escapes.floating, stackArgs,
              returnLanes, sret ? "yes" : "no", cycleMemory ? "yes" : "no",
              (range.abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE_MEMORY) ? "yes"
                                                                     : "no");
  }
  fprintf(output,
          "dolllvm abi target=%s native=%u compat=%u stack_functions=%u "
          "sret_functions=%u cycle_memory_functions=%u "
          "native_memory_functions=%u\n",
          targetTriple, native, rangeCount - native, stackFunctions,
          sretFunctions, cycleMemoryFunctions, nativeMemoryFunctions);
  printHistogram(output, "live_ins", inputHistogram);
  printHistogram(output, "live_outs", outputHistogram);
}
