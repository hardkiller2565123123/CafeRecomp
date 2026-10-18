#include "backend/llvm/emitter.h"

#include "backend/llvm/fp_fixups.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Module.h>

namespace dolllvm {

using namespace llvm;

bool FunctionEmitter::emitVectorPairedFMA(DolIRExactPaired op, u32 d, u32 a,
                                          u32 b, u32 c) {
  bool subtract = op == DOLIR_EXACT_PS_MSUB || op == DOLIR_EXACT_PS_NMSUB;
  bool negative = op == DOLIR_EXACT_PS_NMADD || op == DOLIR_EXACT_PS_NMSUB;
  bool splat0 = op == DOLIR_EXACT_PS_MADDS0;
  bool splat1 = op == DOLIR_EXACT_PS_MADDS1;
  bool hasSingle = fp_rep_[a] == FPRepresentation::PairF32 &&
                   fp_rep_[b] == FPRepresentation::PairF32 &&
                   fp_rep_[c] == FPRepresentation::PairF32;
  bool singleSafe = hasSingle && fp_denormal_safe_[a] && fp_denormal_safe_[b] &&
                    fp_denormal_safe_[c];
  bool operandsFinite = fp_value_class_[a] >= FPValueClass::Finite &&
                        fp_value_class_[b] >= FPValueClass::Finite &&
                        fp_value_class_[c] >= FPValueClass::Finite;
  auto emitPath = [&](bool single) {
    bool reloadDouble = hasSingle && !singleSafe && !single;
    Value *left = single         ? pairF32(a)
                  : reloadDouble ? architecturalPairF64(a)
                                 : pairF64(a);
    Value *architecturalAddend = single         ? pairF32(b)
                                 : reloadDouble ? architecturalPairF64(b)
                                                : pairF64(b);
    Value *architecturalMultiplier = single         ? pairF32(c)
                                     : reloadDouble ? architecturalPairF64(c)
                                                    : pairF64(c);
    if (splat0 || splat1) {
      int lane = splat1 ? 1 : 0;
      architecturalMultiplier = builder_.CreateShuffleVector(
          architecturalMultiplier, architecturalMultiplier, {lane, lane});
    }
    Value *multiplier =
        single ? architecturalMultiplier
               : forcePairedMultiplierPrecision(architecturalMultiplier);
    Value *addend =
        subtract ? builder_.CreateFNeg(architecturalAddend, "ps.fma.subtract")
                 : architecturalAddend;
    Value *result = constrainedPairFMA(left, multiplier, addend);
    result = correctPairedFMARounding(result, left, multiplier, addend);
    result = preferPairedFMANaN(result, left, architecturalAddend,
                                architecturalMultiplier, operandsFinite);
    Value *rounded = roundPairToSingle(result);
    if (!negative)
      return rounded;
    Type *integerPair = FixedVectorType::get(Type::getInt32Ty(context_), 2);
    Value *sign = ConstantVector::getSplat(ElementCount::getFixed(2),
                                           builder_.getInt32(0x80000000u));
    Value *negated = builder_.CreateBitCast(
        builder_.CreateXor(builder_.CreateBitCast(rounded, integerPair), sign),
        rounded->getType(), "ps.fma.negative");
    return operandsFinite
               ? negated
               : builder_.CreateSelect(builder_.CreateFCmpORD(rounded, rounded),
                                       negated, rounded);
  };

  Value *rounded;
  if (hasSingle && !singleSafe) {
    Value *nonIEEE = niEnabled();
    BasicBlock *fast = BasicBlock::Create(context_, "fma_single", function_);
    BasicBlock *strict =
        BasicBlock::Create(context_, "fma_non_ieee", function_);
    BasicBlock *join =
        BasicBlock::Create(context_, "fma_precision_join", function_);
    builder_.CreateCondBr(nonIEEE, strict, fast,
                          MDBuilder(context_).createBranchWeights(1, 20));
    builder_.SetInsertPoint(fast);
    Value *fastResult = emitPath(true);
    builder_.CreateBr(join);
    BasicBlock *fastEnd = builder_.GetInsertBlock();
    builder_.SetInsertPoint(strict);
    Value *strictResult = emitPath(false);
    builder_.CreateBr(join);
    BasicBlock *strictEnd = builder_.GetInsertBlock();
    builder_.SetInsertPoint(join);
    PHINode *selected =
        builder_.CreatePHI(FixedVectorType::get(Type::getFloatTy(context_), 2),
                           2, "ps.fma.precision");
    selected->addIncoming(fastResult, fastEnd);
    selected->addIncoming(strictResult, strictEnd);
    rounded = selected;
  } else {
    rounded = emitPath(singleSafe);
  }
  writePairF32(d, rounded, true,
               operandsFinite ? FPValueClass::NoNaN : FPValueClass::Unknown);
  updateFPRF(builder_.CreateExtractElement(rounded, uint64_t{0}));
  return true;
}

Value *FunctionEmitter::constrainedPairFMA(Value *left, Value *multiplier,
                                           Value *addend) {
  auto *vector = dyn_cast<FixedVectorType>(left->getType());
  if (vector && vector->getNumElements() == 2 &&
      vector->getElementType()->isFloatTy()) {
    Value *zero = ConstantAggregateZero::get(vector);
    Value *wideLeft = builder_.CreateShuffleVector(left, zero, {0, 1, 2, 3});
    Value *wideMultiplier =
        builder_.CreateShuffleVector(multiplier, zero, {0, 1, 2, 3});
    Value *wideAddend =
        builder_.CreateShuffleVector(addend, zero, {0, 1, 2, 3});
    Function *wideFMA = Intrinsic::getDeclaration(
        &module_, Intrinsic::experimental_constrained_fma,
        {wideLeft->getType()});
    Value *wide = builder_.CreateConstrainedFPCall(
        wideFMA, {wideLeft, wideMultiplier, wideAddend}, "ps.fma.native",
        RoundingMode::Dynamic, fp::ebIgnore);
    return builder_.CreateShuffleVector(wide, PoisonValue::get(wide->getType()),
                                        {0, 1}, "ps.fma.pair");
  }
  Function *fma = Intrinsic::getDeclaration(
      &module_, Intrinsic::experimental_constrained_fma, {left->getType()});
  return builder_.CreateConstrainedFPCall(fma, {left, multiplier, addend},
                                          "ps.fma", RoundingMode::Dynamic,
                                          fp::ebIgnore);
}

Value *FunctionEmitter::correctPairedFMARounding(Value *result, Value *left,
                                                 Value *multiplier,
                                                 Value *addend) {
  if (result->getType()->getScalarType()->isFloatTy())
    return result;

  Type *integerPair = FixedVectorType::get(Type::getInt64Ty(context_), 2);
  Value *bits = builder_.CreateBitCast(result, integerPair);
  Value *mask = ConstantVector::getSplat(
      ElementCount::getFixed(2), builder_.getInt64(0x000000001FFFFFFFull));
  Value *midpoint = ConstantVector::getSplat(
      ElementCount::getFixed(2), builder_.getInt64(0x0000000010000000ull));
  Value *ties = builder_.CreateICmpEQ(builder_.CreateAnd(bits, mask), midpoint);
  Value *hasTie =
      builder_.CreateOr(builder_.CreateExtractElement(ties, uint64_t{0}),
                        builder_.CreateExtractElement(ties, 1u));
  Value *fpscr = builder_.CreateLoad(Type::getInt32Ty(context_),
                                     state_[DOLIR_STATE_FPSCR]);
  Value *roundsToNearest = builder_.CreateICmpEQ(
      builder_.CreateAnd(fpscr, builder_.getInt32(3)), builder_.getInt32(0));
  Value *needsFixup = builder_.CreateAnd(roundsToNearest, hasTie);
  BasicBlock *regular =
      BasicBlock::Create(context_, "fma_round_regular", function_);
  BasicBlock *rare = BasicBlock::Create(context_, "fma_round_fixup", function_);
  BasicBlock *join = BasicBlock::Create(context_, "fma_round_join", function_);
  builder_.CreateCondBr(needsFixup, rare, regular,
                        MDBuilder(context_).createBranchWeights(1, 1000000));

  builder_.SetInsertPoint(regular);
  builder_.CreateBr(join);

  builder_.SetInsertPoint(rare);
  Function *fixup = getPairFMARoundingFixup(module_);
  CallInst *fixed = builder_.CreateCall(
      fixup, {result, left, multiplier, addend}, "ps.fma.corrected");
  fixed->setCallingConv(fixup->getCallingConv());
  builder_.CreateBr(join);

  builder_.SetInsertPoint(join);
  PHINode *selected = builder_.CreatePHI(result->getType(), 2, "ps.fma.exact");
  selected->addIncoming(result, regular);
  selected->addIncoming(fixed, rare);
  return selected;
}

Value *FunctionEmitter::preferPairedFMANaN(Value *result, Value *left,
                                           Value *addend, Value *multiplier,
                                           bool operandsFinite) {
  if (operandsFinite)
    return result;
  Value *resultNaN = builder_.CreateFCmpUNO(result, result);
  Value *hasNaN =
      builder_.CreateOr(builder_.CreateExtractElement(resultNaN, uint64_t{0}),
                        builder_.CreateExtractElement(resultNaN, 1u));
  BasicBlock *regular =
      BasicBlock::Create(context_, "fma_nan_regular", function_);
  BasicBlock *rare = BasicBlock::Create(context_, "fma_nan_fixup", function_);
  BasicBlock *join = BasicBlock::Create(context_, "fma_nan_join", function_);
  builder_.CreateCondBr(hasNaN, rare, regular,
                        MDBuilder(context_).createBranchWeights(1, 2000));

  builder_.SetInsertPoint(regular);
  builder_.CreateBr(join);

  builder_.SetInsertPoint(rare);
  Function *fixup = getPairFMANaNFixup(module_, result->getType());
  CallInst *fixed = builder_.CreateCall(
      fixup, {result, left, addend, multiplier}, "ps.fma.nan");
  fixed->setCallingConv(fixup->getCallingConv());
  builder_.CreateBr(join);

  builder_.SetInsertPoint(join);
  PHINode *selected = builder_.CreatePHI(result->getType(), 2, "ps.fma.result");
  selected->addIncoming(result, regular);
  selected->addIncoming(fixed, rare);
  return selected;
}

} // namespace dolllvm
