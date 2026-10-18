#include "backend/llvm/emitter.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Triple.h>

namespace dolllvm {

using namespace llvm;

namespace {

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

void placeOutput(u32 &lane, u32 &offset, u32 width) {
  if (offset && offset + width > 64) {
    lane++;
    offset = 0;
  }
}

} // namespace

u32 FunctionEmitter::nativeOutputLaneCount(const DolLLVMFunctionRange *range) {
  u32 lane = 0;
  u32 offset = 0;
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!stateOutput(range, stateSlot))
      continue;
    const u32 width = stateWidth(stateSlot);
    placeOutput(lane, offset, width);
    offset += width;
    if (offset == 64) {
      lane++;
      offset = 0;
    }
  }
  return lane + (offset != 0);
}

bool FunctionEmitter::nativeCyclesInResult(const DolLLVMFunctionRange *range) {
  if (!range || !cold_escapes_ ||
      !(range->abi_flags & DOLLLVM_FUNCTION_ABI_NATIVE))
    return false;
  const Triple triple(module_.getTargetTriple());
  const u32 returnRegisters = triple.isAArch64() ? 8u : 3u;
  return nativeOutputLaneCount(range) + 1u <= returnRegisters;
}

u32 FunctionEmitter::nativeResultLaneCount(const DolLLVMFunctionRange *range) {
  return nativeOutputLaneCount(range) + nativeCyclesInResult(range);
}

Value *FunctionEmitter::nativeResult(Value *pc, bool native, bool fromContext) {
  Value *result = UndefValue::get(nativeResultType(abi_range_));
  result = builder_.CreateInsertValue(result, pc, 0);
  result = builder_.CreateInsertValue(result, builder_.getInt1(native), 1);
  if (!fromContext)
    materializeFPRF();
  u32 field = 2;
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!stateOutput(abi_range_, stateSlot))
      continue;
    Value *value = fromContext ? loadContext(stateSlot) : stateValue(stateSlot);
    result = builder_.CreateInsertValue(result, value, field++);
  }
  return result;
}

Value *FunctionEmitter::nativeOutputs(bool fromContext) {
  Type *resultType = nativeResultType(abi_range_);
  if (resultType->isVoidTy())
    return nullptr;
  if (!fromContext)
    materializeFPRF();
  const u32 stateLanes = nativeOutputLaneCount(abi_range_);
  const u32 laneCount = nativeResultLaneCount(abi_range_);
  SmallVector<Value *, 8> lanes(laneCount, builder_.getInt64(0));
  u32 lane = 0;
  u32 offset = 0;
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!stateOutput(abi_range_, stateSlot))
      continue;
    const u32 width = stateWidth(stateSlot);
    placeOutput(lane, offset, width);
    Value *value = fromContext ? loadContext(stateSlot) : stateValue(stateSlot);
    if (value->getType()->isDoubleTy())
      value = builder_.CreateBitCast(value, Type::getInt64Ty(context_));
    else
      value = builder_.CreateZExt(value, Type::getInt64Ty(context_));
    if (offset)
      value = builder_.CreateShl(value, offset);
    lanes[lane] = builder_.CreateOr(lanes[lane], value);
    offset += width;
    if (offset == 64) {
      lane++;
      offset = 0;
    }
  }
  if (nativeCyclesInResult(abi_range_))
    lanes[stateLanes] =
        builder_.CreateLoad(Type::getInt64Ty(context_), cycles_);
  if (laneCount == 1)
    return lanes.front();
  Value *result = UndefValue::get(resultType);
  for (u32 index = 0; index < laneCount; index++)
    result = builder_.CreateInsertValue(result, lanes[index], index);
  return result;
}

Value *FunctionEmitter::nativeOutputValue(Value *result,
                                          const DolLLVMFunctionRange *range,
                                          DolIRStateSlot wanted) {
  const u32 laneCount = nativeResultLaneCount(range);
  u32 lane = 0;
  u32 offset = 0;
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!stateOutput(range, stateSlot))
      continue;
    const u32 width = stateWidth(stateSlot);
    placeOutput(lane, offset, width);
    if (stateSlot == wanted) {
      Value *value =
          laneCount == 1 ? result : builder_.CreateExtractValue(result, lane);
      if (offset)
        value = builder_.CreateLShr(value, offset);
      Type *stateTy = type(dolir_state_type(stateSlot));
      if (stateTy->isDoubleTy())
        return builder_.CreateBitCast(value, stateTy);
      return builder_.CreateTrunc(value, stateTy);
    }
    offset += width;
    if (offset == 64) {
      lane++;
      offset = 0;
    }
  }
  return UndefValue::get(type(dolir_state_type(wanted)));
}

Value *FunctionEmitter::nativeCycleValue(Value *result,
                                         const DolLLVMFunctionRange *range) {
  const u32 stateLanes = nativeOutputLaneCount(range);
  const u32 resultLanes = nativeResultLaneCount(range);
  return resultLanes == 1 ? result
                          : builder_.CreateExtractValue(result, stateLanes);
}

Value *FunctionEmitter::nativeResultPC(Value *result) {
  return builder_.CreateExtractValue(result, 0, "native.pc");
}

Value *FunctionEmitter::nativeResultContinues(Value *result) {
  return builder_.CreateExtractValue(result, 1, "native.continues");
}

void FunctionEmitter::acceptNativeResult(Value *result,
                                         const DolLLVMFunctionRange *range) {
  u32 field = cold_escapes_ ? 0u : 2u;
  for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
    auto stateSlot = static_cast<DolIRStateSlot>(slot);
    if (!stateOutput(range, stateSlot))
      continue;
    Value *value = cold_escapes_ ? nativeOutputValue(result, range, stateSlot)
                                 : builder_.CreateExtractValue(result, field++);
    builder_.CreateStore(value, state_[slot]);
  }
  if (nativeCyclesInResult(range))
    builder_.CreateStore(nativeCycleValue(result, range), cycles_);
  invalidateFPRepresentations();
  fp_available_checked_ = false;
  pending_fprf_ = nullptr;
  known_state_.fill(nullptr);
  psq_direct_proven_ = false;
  psq_indexed_proven_ = false;
}

void FunctionEmitter::returnNative(Value *pc) {
  if (cold_escapes_) {
    BasicBlock *normal =
        BasicBlock::Create(context_, "native_return", function_);
    BasicBlock *escape =
        BasicBlock::Create(context_, "return_escape", function_);
    Value *matches = builder_.CreateICmpEQ(pc, return_pc_);
    Function *expect = Intrinsic::getDeclaration(&module_, Intrinsic::expect,
                                                 {Type::getInt1Ty(context_)});
    matches = builder_.CreateCall(expect, {matches, builder_.getTrue()});
    builder_.CreateCondBr(matches, normal, escape);
    builder_.SetInsertPoint(escape);
    materialize(pc);
    returnFromBody();
    builder_.SetInsertPoint(normal);
    flushCallCounters();
    Value *outputs = nativeOutputs(false);
    if (outputs)
      builder_.CreateRet(outputs);
    else
      builder_.CreateRetVoid();
    return;
  }
  flushCallCounters();
  builder_.CreateRet(nativeResult(pc, true, false));
}

} // namespace dolllvm
