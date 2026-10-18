#include "backend/llvm/emitter.h"
#include "cpu/cpu.h"

#include <llvm/IR/Constants.h>

namespace dolllvm {

using namespace llvm;

void FunctionEmitter::scanStableControls() {
  stable_gqr_.fill(true);
  stable_hid2_ = true;
  stable_ni_ = true;
  for (u32 b = 0; b < source_.block_count; b++) {
    const DolIRBlock &block = source_.blocks[b];
    if (block.terminator.kind == DOLIR_TERM_FALLBACK) {
      stable_gqr_.fill(false);
      stable_hid2_ = false;
      stable_ni_ = false;
    }
    for (u32 i = 0; i < block.instruction_count; i++) {
      const DolIRInstruction &inst = block.instructions[i];
      for (u32 gqr = 0; gqr < 8; gqr++) {
        auto slot = static_cast<DolIRStateSlot>(DOLIR_STATE_GQR0 + gqr);
        if (dolir_state_mask_test(inst.state_defs, slot))
          stable_gqr_[gqr] = false;
      }
      if (dolir_state_mask_test(inst.state_defs, DOLIR_STATE_HID2))
        stable_hid2_ = false;
      if ((inst.op == DOLIR_OP_STATE_WRITE && inst.aux == DOLIR_STATE_FPSCR) ||
          (inst.op == DOLIR_OP_HELPER_CALL &&
           (inst.aux == DOLIR_HELPER_FPSCR_UPDATED ||
            inst.aux == DOLIR_HELPER_FPSCR_BIT)))
        stable_ni_ = false;
    }
  }
}

void FunctionEmitter::initializeEntryControls() {
  Type *i32 = Type::getInt32Ty(context_);
  for (u32 gqr = 0; gqr < 8; gqr++) {
    auto slot = static_cast<DolIRStateSlot>(DOLIR_STATE_GQR0 + gqr);
    if (!stable_gqr_[gqr] || !used_[slot])
      continue;
    Value *value = builder_.CreateLoad(i32, state_[slot], "gqr.entry");
    entry_gqr_[gqr] = value;
    entry_gqr_load_type_[gqr] =
        builder_.CreateAnd(builder_.CreateLShr(value, builder_.getInt32(16)),
                           builder_.getInt32(7), "gqr.load.type");
    entry_gqr_store_type_[gqr] =
        builder_.CreateAnd(value, builder_.getInt32(7), "gqr.store.type");
    Value *loadScale =
        builder_.CreateAnd(builder_.CreateLShr(value, builder_.getInt32(24)),
                           builder_.getInt32(0x3f));
    Value *storeScale =
        builder_.CreateAnd(builder_.CreateLShr(value, builder_.getInt32(8)),
                           builder_.getInt32(0x3f));
    entry_gqr_load_scale_[gqr] = builder_.CreateAShr(
        builder_.CreateShl(loadScale, builder_.getInt32(26)),
        builder_.getInt32(26), "gqr.load.scale");
    entry_gqr_store_scale_[gqr] = builder_.CreateAShr(
        builder_.CreateShl(storeScale, builder_.getInt32(26)),
        builder_.getInt32(26), "gqr.store.scale");
  }
  if (stable_hid2_ && used_[DOLIR_STATE_HID2]) {
    Value *hid2 =
        builder_.CreateLoad(i32, state_[DOLIR_STATE_HID2], "hid2.entry");
    entry_psq_indexed_enabled_ = builder_.CreateICmpNE(
        builder_.CreateAnd(hid2, builder_.getInt32(PPC_HID2_PSE)),
        builder_.getInt32(0), "psq.indexed.enabled");
    entry_psq_direct_enabled_ = builder_.CreateICmpEQ(
        builder_.CreateAnd(hid2,
                           builder_.getInt32(PPC_HID2_PSE | PPC_HID2_LSQE)),
        builder_.getInt32(PPC_HID2_PSE | PPC_HID2_LSQE), "psq.direct.enabled");
  }
  if (stable_ni_ && used_[DOLIR_STATE_FPSCR]) {
    Value *fpscr =
        builder_.CreateLoad(i32, state_[DOLIR_STATE_FPSCR], "fpscr.control");
    entry_ni_enabled_ =
        builder_.CreateICmpNE(builder_.CreateAnd(fpscr, builder_.getInt32(4)),
                              builder_.getInt32(0), "fpscr.ni");
  }
}

Value *FunctionEmitter::gqrValue(u32 index) {
  auto slot = static_cast<DolIRStateSlot>(DOLIR_STATE_GQR0 + index);
  if (known_state_[slot])
    return known_state_[slot];
  if (entry_gqr_[index])
    return entry_gqr_[index];
  return builder_.CreateLoad(Type::getInt32Ty(context_), state_[slot]);
}

Value *FunctionEmitter::gqrType(u32 index, bool load) {
  Value *value = gqrValue(index);
  if (auto *constant = dyn_cast<ConstantInt>(value)) {
    u32 shift = load ? 16u : 0u;
    return builder_.getInt32((constant->getZExtValue() >> shift) & 7u);
  }
  Value *entryType =
      load ? entry_gqr_load_type_[index] : entry_gqr_store_type_[index];
  if (value == entry_gqr_[index] && entryType)
    return entryType;
  return builder_.CreateAnd(
      load ? builder_.CreateLShr(value, builder_.getInt32(16)) : value,
      builder_.getInt32(7));
}

Value *FunctionEmitter::gqrScale(u32 index, bool load) {
  Value *value = gqrValue(index);
  if (auto *constant = dyn_cast<ConstantInt>(value)) {
    u32 shift = load ? 24u : 8u;
    u32 bits = (constant->getZExtValue() >> shift) & 0x3fu;
    s32 scale = static_cast<s32>(bits << 26) >> 26;
    return builder_.getInt32(static_cast<u32>(scale));
  }
  Value *entryScale =
      load ? entry_gqr_load_scale_[index] : entry_gqr_store_scale_[index];
  if (value == entry_gqr_[index] && entryScale)
    return entryScale;
  Value *bits = builder_.CreateAnd(
      builder_.CreateLShr(value, builder_.getInt32(load ? 24 : 8)),
      builder_.getInt32(0x3f));
  return builder_.CreateAShr(builder_.CreateShl(bits, builder_.getInt32(26)),
                             builder_.getInt32(26));
}

Value *FunctionEmitter::psqEnabled(bool indexed) {
  if (indexed ? psq_indexed_proven_ : psq_direct_proven_)
    return ConstantInt::getTrue(context_);
  Value *cached =
      indexed ? entry_psq_indexed_enabled_ : entry_psq_direct_enabled_;
  if (cached)
    return cached;
  Value *hid2 = stateValue(DOLIR_STATE_HID2);
  u32 mask = PPC_HID2_PSE | (indexed ? 0u : PPC_HID2_LSQE);
  return builder_.CreateICmpEQ(
      builder_.CreateAnd(hid2, builder_.getInt32(mask)),
      builder_.getInt32(mask));
}

Value *FunctionEmitter::niEnabled() {
  if (entry_ni_enabled_)
    return entry_ni_enabled_;
  Value *fpscr = known_state_[DOLIR_STATE_FPSCR]
                     ? static_cast<Value *>(known_state_[DOLIR_STATE_FPSCR])
                     : builder_.CreateLoad(Type::getInt32Ty(context_),
                                           state_[DOLIR_STATE_FPSCR]);
  return builder_.CreateICmpNE(builder_.CreateAnd(fpscr, builder_.getInt32(4)),
                               builder_.getInt32(0));
}

} // namespace dolllvm
