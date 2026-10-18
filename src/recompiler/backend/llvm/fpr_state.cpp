#include "backend/llvm/emitter.h"

#include "backend/llvm/fp_fixups.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Module.h>

namespace dolllvm {

using namespace llvm;

Value *FunctionEmitter::pairF32(u32 reg) {
  return builder_.CreateLoad(
      FixedVectorType::get(Type::getFloatTy(context_), 2), pair_f32_[reg],
      "ps.single");
}

Value *FunctionEmitter::pairF64(u32 reg) {
  Type *pairType = FixedVectorType::get(Type::getDoubleTy(context_), 2);
  if (fp_rep_[reg] == FPRepresentation::PairF64)
    return builder_.CreateLoad(pairType, pair_f64_[reg], "ps.double");
  if (fp_rep_[reg] == FPRepresentation::PairF32 && fp_denormal_safe_[reg])
    return builder_.CreateFPExt(pairF32(reg), pairType, "ps.widen");

  return architecturalPairF64(reg);
}

Value *FunctionEmitter::architecturalPairF64(u32 reg) {
  Type *pairType = FixedVectorType::get(Type::getDoubleTy(context_), 2);
  Value *pair = PoisonValue::get(pairType);
  pair = builder_.CreateInsertElement(
      pair, stateValue(static_cast<DolIRStateSlot>(DOLIR_STATE_FPR0 + reg)),
      uint64_t{0});
  return builder_.CreateInsertElement(
      pair, stateValue(static_cast<DolIRStateSlot>(DOLIR_STATE_PS1_0 + reg)),
      1u, "ps.arch");
}

void FunctionEmitter::writePairF32(u32 reg, Value *pair, bool denormalSafe,
                                   FPValueClass valueClass) {
  builder_.CreateStore(pair, pair_f32_[reg]);
  Type *f64 = Type::getDoubleTy(context_);
  Value *lane0 = builder_.CreateFPExt(
      builder_.CreateExtractElement(pair, uint64_t{0}), f64);
  Value *lane1 =
      builder_.CreateFPExt(builder_.CreateExtractElement(pair, 1u), f64);
  builder_.CreateStore(lane0, state_[DOLIR_STATE_FPR0 + reg]);
  builder_.CreateStore(lane1, state_[DOLIR_STATE_PS1_0 + reg]);
  fp_rep_[reg] = FPRepresentation::PairF32;
  fp_exact_single_[reg] = true;
  fp_denormal_safe_[reg] = denormalSafe;
  fp_value_class_[reg] = valueClass;
}

void FunctionEmitter::writePairF64(u32 reg, Value *pair, bool exactSingle,
                                   FPValueClass valueClass) {
  builder_.CreateStore(pair, pair_f64_[reg]);
  builder_.CreateStore(builder_.CreateExtractElement(pair, uint64_t{0}),
                       state_[DOLIR_STATE_FPR0 + reg]);
  builder_.CreateStore(builder_.CreateExtractElement(pair, 1u),
                       state_[DOLIR_STATE_PS1_0 + reg]);
  fp_rep_[reg] = FPRepresentation::PairF64;
  fp_exact_single_[reg] = exactSingle;
  fp_denormal_safe_[reg] = false;
  fp_value_class_[reg] = valueClass;
}

Value *FunctionEmitter::constrainedPairBinary(unsigned intrinsic, Value *left,
                                              Value *right) {
  auto *vector = dyn_cast<FixedVectorType>(left->getType());
  if (vector && vector->getNumElements() == 2 &&
      vector->getElementType()->isFloatTy()) {
    Value *zero = ConstantAggregateZero::get(vector);
    Value *wideLeft =
        builder_.CreateShuffleVector(left, zero, {0, 1, 2, 3}, "ps.native");
    Value *wideRight =
        builder_.CreateShuffleVector(right, zero, {0, 1, 2, 3}, "ps.native");
    Value *wide = builder_.CreateConstrainedFPBinOp(
        static_cast<Intrinsic::ID>(intrinsic), wideLeft, wideRight, nullptr,
        "ps.math", nullptr, RoundingMode::Dynamic, fp::ebIgnore);
    return builder_.CreateShuffleVector(wide, PoisonValue::get(wide->getType()),
                                        {0, 1}, "ps.pair");
  }
  return builder_.CreateConstrainedFPBinOp(
      static_cast<Intrinsic::ID>(intrinsic), left, right, nullptr, "ps.math",
      nullptr, RoundingMode::Dynamic, fp::ebIgnore);
}

Value *FunctionEmitter::roundPairToSingle(Value *pair) {
  Type *pairType = FixedVectorType::get(Type::getFloatTy(context_), 2);
  if (pair->getType() == pairType)
    return pair;
  return builder_.CreateConstrainedFPCast(
      Intrinsic::experimental_constrained_fptrunc, pair, pairType, nullptr,
      "ps.round", nullptr, RoundingMode::Dynamic, fp::ebIgnore);
}

Value *FunctionEmitter::forcePairedMultiplierPrecision(Value *pair) {
  Type *integerPair = FixedVectorType::get(Type::getInt64Ty(context_), 2);
  auto splat = [integerPair](u64 value) {
    return ConstantVector::getSplat(
        ElementCount::getFixed(2),
        ConstantInt::get(integerPair->getScalarType(), value));
  };
  Value *bits = builder_.CreateBitCast(pair, integerPair);
  Value *fraction = builder_.CreateAnd(bits, splat(0x000FFFFFFFFFFFFFull));
  Value *subnormal = builder_.CreateAnd(
      builder_.CreateICmpEQ(
          builder_.CreateAnd(bits, splat(0x7FF0000000000000ull)), splat(0)),
      builder_.CreateICmpNE(fraction, splat(0)));
  Value *hasSubnormal =
      builder_.CreateOr(builder_.CreateExtractElement(subnormal, uint64_t{0}),
                        builder_.CreateExtractElement(subnormal, 1u));
  BasicBlock *regular =
      BasicBlock::Create(context_, "ps_force25_regular", function_);
  BasicBlock *rare =
      BasicBlock::Create(context_, "ps_force25_subnormal", function_);
  BasicBlock *join = BasicBlock::Create(context_, "ps_force25_join", function_);
  builder_.CreateCondBr(hasSubnormal, rare, regular,
                        MDBuilder(context_).createBranchWeights(1, 2000));

  builder_.SetInsertPoint(regular);
  Value *regularRounded = builder_.CreateAdd(
      builder_.CreateAnd(bits, splat(0xFFFFFFFFF8000000ull)),
      builder_.CreateAnd(bits, splat(0x0000000008000000ull)));
  builder_.CreateBr(join);

  builder_.SetInsertPoint(rare);
  Function *ctlz =
      Intrinsic::getDeclaration(&module_, Intrinsic::ctlz, {integerPair});
  Value *leading =
      builder_.CreateCall(ctlz, {fraction, ConstantInt::getFalse(context_)});
  Value *shift = builder_.CreateSub(leading, splat(11));
  Value *shortShift = builder_.CreateICmpULT(shift, splat(28));
  Value *safeShift = builder_.CreateSelect(shortShift, shift, splat(0));
  Value *discard = builder_.CreateSub(splat(27), safeShift);
  Value *dynamicKeep = builder_.CreateNot(
      builder_.CreateSub(builder_.CreateShl(splat(1), discard), splat(1)));
  Value *dynamicRound =
      builder_.CreateLShr(splat(0x0000000008000000ull), safeShift);
  Value *subnormalKeep =
      builder_.CreateSelect(shortShift, dynamicKeep, splat(~0ull));
  Value *subnormalRound =
      builder_.CreateSelect(shortShift, dynamicRound, splat(0));
  Value *keep = builder_.CreateSelect(subnormal, subnormalKeep,
                                      splat(0xFFFFFFFFF8000000ull));
  Value *round = builder_.CreateSelect(subnormal, subnormalRound,
                                       splat(0x0000000008000000ull));
  Value *rounded = builder_.CreateAdd(builder_.CreateAnd(bits, keep),
                                      builder_.CreateAnd(bits, round));
  builder_.CreateBr(join);

  builder_.SetInsertPoint(join);
  PHINode *result = builder_.CreatePHI(integerPair, 2, "ps.force25.bits");
  result->addIncoming(regularRounded, regular);
  result->addIncoming(rounded, rare);
  Value *special = builder_.CreateICmpEQ(
      builder_.CreateAnd(bits, splat(0x7FF0000000000000ull)),
      splat(0x7FF0000000000000ull));
  Value *preserved = builder_.CreateSelect(special, bits, result);
  return builder_.CreateBitCast(preserved, pair->getType(), "ps.force25");
}

Value *FunctionEmitter::preferPairedNaN(Value *result, Value *left,
                                        Value *right, bool operandsFinite) {
  if (operandsFinite)
    return result;
  Value *resultNaN = builder_.CreateFCmpUNO(result, result);
  Value *hasExceptionalLane =
      builder_.CreateOr(builder_.CreateExtractElement(resultNaN, uint64_t{0}),
                        builder_.CreateExtractElement(resultNaN, 1u));
  BasicBlock *regular =
      BasicBlock::Create(context_, "ps_nan_regular", function_);
  BasicBlock *rare = BasicBlock::Create(context_, "ps_nan_fixup", function_);
  BasicBlock *join = BasicBlock::Create(context_, "ps_nan_join", function_);
  builder_.CreateCondBr(hasExceptionalLane, rare, regular,
                        MDBuilder(context_).createBranchWeights(1, 2000));

  builder_.SetInsertPoint(regular);
  builder_.CreateBr(join);

  builder_.SetInsertPoint(rare);
  Function *fixup = getPairNaNFixup(module_, result->getType());
  CallInst *fixed = builder_.CreateCall(fixup, {result, left, right}, "ps.nan");
  fixed->setCallingConv(fixup->getCallingConv());
  builder_.CreateBr(join);

  builder_.SetInsertPoint(join);
  PHINode *selected = builder_.CreatePHI(result->getType(), 2, "ps.nan.result");
  selected->addIncoming(result, regular);
  selected->addIncoming(fixed, rare);
  return selected;
}

void FunctionEmitter::updateFPRF(Value *lane0) { pending_fprf_ = lane0; }

void FunctionEmitter::materializeFPRF() {
  if (!pending_fprf_)
    return;
  Value *lane0 = pending_fprf_;
  pending_fprf_ = nullptr;
  Value *bits = builder_.CreateBitCast(lane0, Type::getInt32Ty(context_));
  Value *sign = builder_.CreateICmpNE(
      builder_.CreateAnd(bits, builder_.getInt32(0x80000000u)),
      builder_.getInt32(0));
  Value *exponent = builder_.CreateAnd(bits, builder_.getInt32(0x7F800000u));
  Value *normalExponent = builder_.CreateICmpULE(
      builder_.CreateSub(exponent, builder_.getInt32(0x00800000u)),
      builder_.getInt32(0x7E800000u));
  BasicBlock *normal = BasicBlock::Create(context_, "fprf_normal", function_);
  BasicBlock *unusual = BasicBlock::Create(context_, "fprf_unusual", function_);
  BasicBlock *join = BasicBlock::Create(context_, "fprf_join", function_);
  builder_.CreateCondBr(normalExponent, normal, unusual,
                        MDBuilder(context_).createBranchWeights(64, 1));

  builder_.SetInsertPoint(normal);
  Value *normalClass = builder_.CreateSelect(sign, builder_.getInt32(0x08u),
                                             builder_.getInt32(0x04u));
  builder_.CreateBr(join);

  builder_.SetInsertPoint(unusual);
  Function *fixup = getFPRFUnusualFixup(module_);
  CallInst *unusualClass = builder_.CreateCall(fixup, {bits}, "fprf.unusual");
  unusualClass->setCallingConv(fixup->getCallingConv());
  builder_.CreateBr(join);

  builder_.SetInsertPoint(join);
  PHINode *classification =
      builder_.CreatePHI(Type::getInt32Ty(context_), 2, "fprf.class");
  classification->addIncoming(normalClass, normal);
  classification->addIncoming(unusualClass, unusual);

  Value *fpscr = builder_.CreateLoad(Type::getInt32Ty(context_),
                                     state_[DOLIR_STATE_FPSCR]);
  fpscr = builder_.CreateAnd(fpscr, builder_.getInt32(~(0x1Fu << 12)));
  builder_.CreateStore(
      builder_.CreateOr(fpscr, builder_.CreateShl(classification, 12)),
      state_[DOLIR_STATE_FPSCR]);
}

} // namespace dolllvm
