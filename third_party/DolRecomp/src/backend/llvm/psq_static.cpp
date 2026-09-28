#include "backend/llvm/emitter.h"
#include "backend/llvm/psq_convert.h"
#include "cpu/cpu.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Module.h>

#include <utility>

namespace dolllvm {

using namespace llvm;

static u32 psqTypeWidth(u32 type) {
  return type == 0 ? 4 : (type == 5 || type == 7 ? 2 : 1);
}

Value *FunctionEmitter::emitKnownPSQ(const DolIRInstruction &inst, u32 type,
                                     s32 scale) {
  u32 reg = inst.immediate & 0xFFu;
  bool w = ((inst.immediate >> 8) & 1u) != 0;
  u32 gqr = (inst.immediate >> 9) & 7u;
  bool indexed = ((inst.immediate >> 12) & 1u) != 0;
  bool load = inst.aux == DOLIR_HELPER_PSQ_LOAD;
  u32 width = psqTypeWidth(type);
  Value *address = operand(inst, 0);
  Type *ptr = PointerType::getUnqual(context_);
  auto callee = module_.getOrInsertFunction(
      load ? "ppc_psq_load" : "ppc_psq_store",
      FunctionType::get(Type::getInt1Ty(context_),
                        {ptr, Type::getInt8Ty(context_),
                         Type::getInt32Ty(context_), Type::getInt1Ty(context_),
                         Type::getInt8Ty(context_), Type::getInt1Ty(context_),
                         Type::getInt32Ty(context_)},
                        false));
  Value *enabled = psqEnabled(indexed);
  if (type == 0) {
    enabled = builder_.CreateAnd(
        enabled,
        builder_.CreateICmpEQ(builder_.CreateAnd(address, builder_.getInt32(3)),
                              builder_.getInt32(0)));
  }

  auto incomingRepresentations = fp_rep_;
  auto incomingExactSingles = fp_exact_single_;
  auto incomingDenormalSafety = fp_denormal_safe_;
  auto incomingValueClasses = fp_value_class_;
  auto incomingKnownState = known_state_;
  bool incomingFPAvailable = fp_available_checked_;
  BasicBlock *fast = BasicBlock::Create(context_, "psq_known", function_);
  BasicBlock *slow =
      BasicBlock::Create(context_, "psq_known_helper", function_);
  BasicBlock *join = BasicBlock::Create(context_, "psq_known_join", function_);
  builder_.CreateCondBr(enabled, fast, slow,
                        MDBuilder(context_).createBranchWeights(1000, 1));

  builder_.SetInsertPoint(fast);
  if (load) {
    auto loadLane = [&](Value *laneAddress) {
      if (type == 0) {
        Value *bits =
            emitGuestLoad(laneAddress, Type::getInt32Ty(context_), 4, false);
        return std::pair<Value *, Value *>(
            builder_.CreateBitCast(bits, Type::getFloatTy(context_)),
            extendPSQFloat(builder_, module_, bits));
      }
      Type *integerType = IntegerType::get(context_, width * 8);
      Value *integer = emitGuestLoad(laneAddress, integerType, width, false);
      Value *single = dequantizePSQInteger(
          builder_, integer, builder_.getInt32(static_cast<u32>(scale)),
          type == 6 || type == 7);
      return std::pair<Value *, Value *>(
          single, builder_.CreateFPExt(single, Type::getDoubleTy(context_)));
    };
    auto lane0 = loadLane(address);
    std::pair<Value *, Value *> lane1(
        ConstantFP::get(Type::getFloatTy(context_), 1.0),
        ConstantFP::get(Type::getDoubleTy(context_), 1.0));
    if (!w)
      lane1 = loadLane(builder_.CreateAdd(address, builder_.getInt32(width)));
    Type *pairType = FixedVectorType::get(Type::getFloatTy(context_), 2);
    Value *pair = PoisonValue::get(pairType);
    pair = builder_.CreateInsertElement(pair, lane0.first, uint64_t{0});
    pair = builder_.CreateInsertElement(pair, lane1.first, 1u);
    builder_.CreateStore(pair, pair_f32_[reg]);
    builder_.CreateStore(lane0.second, state_[DOLIR_STATE_FPR0 + reg]);
    builder_.CreateStore(lane1.second, state_[DOLIR_STATE_PS1_0 + reg]);
  } else {
    Value *pair = fp_rep_[reg] == FPRepresentation::PairF32 &&
                          (type == 0 || fp_denormal_safe_[reg])
                      ? pairF32(reg)
                      : pairF64(reg);
    auto storeLane = [&](Value *laneAddress, u32 lane) {
      Value *value = builder_.CreateExtractElement(pair, lane);
      Value *bits =
          type == 0
              ? truncatePSQFloat(builder_, value)
              : quantizePSQFloat(builder_, value,
                                 builder_.getInt32(static_cast<u32>(scale)),
                                 type == 6   ? -128
                                 : type == 7 ? -32768
                                             : 0,
                                 type == 4   ? 255
                                 : type == 5 ? 65535
                                 : type == 6 ? 127
                                             : 32767);
      emitGuestStore(laneAddress, bits, width);
    };
    storeLane(address, 0);
    if (!w)
      storeLane(builder_.CreateAdd(address, builder_.getInt32(width)), 1);
  }
  builder_.CreateBr(join);
  BasicBlock *fastEnd = builder_.GetInsertBlock();

  builder_.SetInsertPoint(slow);
  materialize(inst.guest_pc);
  Value *success = builder_.CreateCall(
      callee, {ctx_, builder_.getInt8(reg), address, builder_.getInt1(w),
               builder_.getInt8(gqr), builder_.getInt1(indexed),
               builder_.getInt32(inst.guest_pc)});
  BasicBlock *resume =
      BasicBlock::Create(context_, "psq_known_resume", function_);
  BasicBlock *failed =
      BasicBlock::Create(context_, "psq_known_exit", function_);
  builder_.CreateCondBr(success, resume, failed);
  builder_.SetInsertPoint(failed);
  returnFromBody();
  builder_.SetInsertPoint(resume);
  reloadUsedState();
  if (load)
    builder_.CreateStore(roundPairToSingle(pairF64(reg)), pair_f32_[reg]);
  builder_.CreateBr(join);

  builder_.SetInsertPoint(join);
  PHINode *result = builder_.CreatePHI(Type::getInt1Ty(context_), 2);
  result->addIncoming(ConstantInt::getTrue(context_), fastEnd);
  result->addIncoming(success, resume);
  fp_rep_ = incomingRepresentations;
  fp_exact_single_ = incomingExactSingles;
  fp_denormal_safe_ = incomingDenormalSafety;
  fp_value_class_ = incomingValueClasses;
  known_state_ = incomingKnownState;
  fp_available_checked_ = incomingFPAvailable;
  if (load) {
    fp_rep_[reg] = FPRepresentation::PairF32;
    fp_exact_single_[reg] = true;
    fp_denormal_safe_[reg] = type != 0;
    fp_value_class_[reg] =
        type == 0 ? FPValueClass::Unknown : FPValueClass::NormalOrZero;
  }
  psq_indexed_proven_ = true;
  if (!indexed)
    psq_direct_proven_ = true;
  return result;
}

} // namespace dolllvm
