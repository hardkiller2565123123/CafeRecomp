#include "backend/llvm/emitter.h"
#include "cpu/cpu.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>

namespace dolllvm {

using namespace llvm;

bool FunctionEmitter::emitVectorPaired(DolIRExactPaired op, u32 d, u32 a, u32 b,
                                       u32 c) {
  auto isSingle = [this](u32 reg) { return fp_exact_single_[reg]; };
  auto singleMathSafe = [this, &isSingle](u32 reg) {
    return isSingle(reg) && fp_denormal_safe_[reg];
  };
  auto finite = [this](u32 reg) {
    return fp_value_class_[reg] >= FPValueClass::Finite;
  };
  auto finish = [this, d](Value *pair, FPValueClass valueClass) {
    Value *rounded = roundPairToSingle(pair);
    writePairF32(d, rounded, true, valueClass);
    updateFPRF(builder_.CreateExtractElement(rounded, uint64_t{0}));
  };

  if (op == DOLIR_EXACT_PS_ADD || op == DOLIR_EXACT_PS_SUB) {
    bool useSingle = singleMathSafe(a) && singleMathSafe(b);
    Value *left = useSingle ? pairF32(a) : pairF64(a);
    Value *right = useSingle ? pairF32(b) : pairF64(b);
    unsigned intrinsic = op == DOLIR_EXACT_PS_ADD
                             ? Intrinsic::experimental_constrained_fadd
                             : Intrinsic::experimental_constrained_fsub;
    bool operandsFinite = finite(a) && finite(b);
    finish(preferPairedNaN(constrainedPairBinary(intrinsic, left, right), left,
                           right, operandsFinite),
           operandsFinite ? FPValueClass::NoNaN : FPValueClass::Unknown);
    return true;
  }

  if (op == DOLIR_EXACT_PS_DIV) {
    Value *left = pairF64(a);
    Value *right = pairF64(b);
    finish(preferPairedNaN(
               constrainedPairBinary(Intrinsic::experimental_constrained_fdiv,
                                     left, right),
               left, right, false),
           FPValueClass::Unknown);
    return true;
  }

  if (op == DOLIR_EXACT_PS_MUL) {
    bool useSingle = singleMathSafe(a) && singleMathSafe(c);
    Value *left = useSingle ? pairF32(a) : pairF64(a);
    Value *architecturalRight = useSingle ? pairF32(c) : pairF64(c);
    Value *right = useSingle
                       ? architecturalRight
                       : forcePairedMultiplierPrecision(architecturalRight);
    bool operandsFinite = finite(a) && finite(c);
    finish(preferPairedNaN(
               constrainedPairBinary(Intrinsic::experimental_constrained_fmul,
                                     left, right),
               left, architecturalRight, operandsFinite),
           operandsFinite ? FPValueClass::NoNaN : FPValueClass::Unknown);
    return true;
  }

  if (op == DOLIR_EXACT_PS_MULS0 || op == DOLIR_EXACT_PS_MULS1) {
    bool useSingle = singleMathSafe(a) && singleMathSafe(c);
    Value *left = useSingle ? pairF32(a) : pairF64(a);
    Value *architecturalScalar = useSingle ? pairF32(c) : pairF64(c);
    Value *scalarPair =
        useSingle ? architecturalScalar
                  : forcePairedMultiplierPrecision(architecturalScalar);
    int lane = op == DOLIR_EXACT_PS_MULS1 ? 1 : 0;
    Value *splat =
        builder_.CreateShuffleVector(scalarPair, scalarPair, {lane, lane});
    Value *architecturalSplat = builder_.CreateShuffleVector(
        architecturalScalar, architecturalScalar, {lane, lane});
    bool operandsFinite = finite(a) && finite(c);
    finish(preferPairedNaN(
               constrainedPairBinary(Intrinsic::experimental_constrained_fmul,
                                     left, splat),
               left, architecturalSplat, operandsFinite),
           operandsFinite ? FPValueClass::NoNaN : FPValueClass::Unknown);
    return true;
  }

  if (op == DOLIR_EXACT_PS_MADD || op == DOLIR_EXACT_PS_MSUB ||
      op == DOLIR_EXACT_PS_NMADD || op == DOLIR_EXACT_PS_NMSUB ||
      op == DOLIR_EXACT_PS_MADDS0 || op == DOLIR_EXACT_PS_MADDS1) {
    return emitVectorPairedFMA(op, d, a, b, c);
  }

  if (op == DOLIR_EXACT_PS_SUM0 || op == DOLIR_EXACT_PS_SUM1) {
    Value *left = builder_.CreateExtractElement(pairF64(a), uint64_t{0});
    Value *right = builder_.CreateExtractElement(pairF64(b), 1u);
    Value *sum = constrainedPairBinary(Intrinsic::experimental_constrained_fadd,
                                       left, right);
    Value *copy = builder_.CreateExtractElement(
        pairF64(c), op == DOLIR_EXACT_PS_SUM0 ? 1u : 0u);
    Type *pairType = FixedVectorType::get(Type::getDoubleTy(context_), 2);
    Value *result = PoisonValue::get(pairType);
    result = builder_.CreateInsertElement(
        result, op == DOLIR_EXACT_PS_SUM0 ? sum : copy, uint64_t{0});
    result = builder_.CreateInsertElement(
        result, op == DOLIR_EXACT_PS_SUM0 ? copy : sum, 1u);
    finish(result, FPValueClass::Unknown);
    return true;
  }

  return false;
}

void FunctionEmitter::emitExactPaired(u64 descriptor) {
  auto op = static_cast<DolIRExactPaired>(descriptor & 0xFFu);
  u32 d = (descriptor >> 8) & 0xFFu;
  u32 a = (descriptor >> 16) & 0xFFu;
  u32 b = (descriptor >> 24) & 0xFFu;
  u32 c = (descriptor >> 32) & 0xFFu;
  u32 crfd = (descriptor >> 40) & 0xFFu;
  auto fprSlot = [](u32 reg) {
    return static_cast<DolIRStateSlot>(DOLIR_STATE_FPR0 + reg);
  };
  auto ps1Slot = [](u32 reg) {
    return static_cast<DolIRStateSlot>(DOLIR_STATE_PS1_0 + reg);
  };
  auto syncPair = [this, &fprSlot, &ps1Slot](u32 reg) {
    syncState(fprSlot(reg));
    syncState(ps1Slot(reg));
  };
  auto reloadPair = [this, &fprSlot, &ps1Slot](u32 reg) {
    reloadState(fprSlot(reg));
    reloadState(ps1Slot(reg));
  };
  Type *ptr = PointerType::getUnqual(context_);
  Type *i8 = Type::getInt8Ty(context_);
  Type *f64 = Type::getDoubleTy(context_);

  if (op < DOLIR_EXACT_PS_CMPU0 && op != DOLIR_EXACT_PS_RES &&
      op != DOLIR_EXACT_PS_RSQRTE && emitVectorPaired(op, d, a, b, c))
    return;
  syncState(DOLIR_STATE_FPSCR);

  if (op >= DOLIR_EXACT_PS_CMPU0) {
    bool lane1 = op == DOLIR_EXACT_PS_CMPU1 || op == DOLIR_EXACT_PS_CMPO1;
    bool ordered = op == DOLIR_EXACT_PS_CMPO0 || op == DOLIR_EXACT_PS_CMPO1;
    auto crSlot = static_cast<DolIRStateSlot>(DOLIR_STATE_CR0 + crfd);
    syncState(crSlot);
    syncPair(a);
    syncPair(b);
    auto callee = module_.getOrInsertFunction(
        "ppc_fcmp", FunctionType::get(
                        Type::getVoidTy(context_),
                        {ptr, i8, f64, f64, Type::getInt1Ty(context_)}, false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt8(crfd),
                                 stateValue(lane1 ? ps1Slot(a) : fprSlot(a)),
                                 stateValue(lane1 ? ps1Slot(b) : fprSlot(b)),
                                 builder_.getInt1(ordered)});
    reloadState(crSlot);
    reloadState(DOLIR_STATE_FPSCR);
    return;
  }

  syncPair(d);
  if (op == DOLIR_EXACT_PS_RES || op == DOLIR_EXACT_PS_RSQRTE) {
    syncPair(b);
    const char *name =
        op == DOLIR_EXACT_PS_RES ? "ppc_ps_res_op" : "ppc_ps_rsqrte_op";
    auto callee = module_.getOrInsertFunction(
        name,
        FunctionType::get(Type::getVoidTy(context_), {ptr, i8, i8}, false));
    builder_.CreateCall(callee,
                        {ctx_, builder_.getInt8(d), builder_.getInt8(b)});
  } else if (op == DOLIR_EXACT_PS_MADD || op == DOLIR_EXACT_PS_MSUB ||
             op == DOLIR_EXACT_PS_NMADD || op == DOLIR_EXACT_PS_NMSUB) {
    syncPair(a);
    syncPair(b);
    syncPair(c);
    bool subtract = op == DOLIR_EXACT_PS_MSUB || op == DOLIR_EXACT_PS_NMSUB;
    bool negative = op == DOLIR_EXACT_PS_NMADD || op == DOLIR_EXACT_PS_NMSUB;
    auto callee = module_.getOrInsertFunction(
        "ppc_ps_madd_op",
        FunctionType::get(Type::getVoidTy(context_),
                          {ptr, i8, i8, i8, i8, Type::getInt1Ty(context_),
                           Type::getInt1Ty(context_)},
                          false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt8(d), builder_.getInt8(a),
                                 builder_.getInt8(c), builder_.getInt8(b),
                                 builder_.getInt1(subtract),
                                 builder_.getInt1(negative)});
  } else if (op == DOLIR_EXACT_PS_MADDS0 || op == DOLIR_EXACT_PS_MADDS1 ||
             op == DOLIR_EXACT_PS_SUM0 || op == DOLIR_EXACT_PS_SUM1) {
    syncPair(a);
    syncPair(b);
    syncPair(c);
    const char *name = op == DOLIR_EXACT_PS_MADDS0   ? "ppc_ps_madds0"
                       : op == DOLIR_EXACT_PS_MADDS1 ? "ppc_ps_madds1"
                       : op == DOLIR_EXACT_PS_SUM0   ? "ppc_ps_sum0"
                                                     : "ppc_ps_sum1";
    auto callee = module_.getOrInsertFunction(
        name, FunctionType::get(Type::getVoidTy(context_),
                                {ptr, i8, i8, i8, i8}, false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt8(d), builder_.getInt8(a),
                                 builder_.getInt8(c), builder_.getInt8(b)});
  } else if (op == DOLIR_EXACT_PS_MULS0 || op == DOLIR_EXACT_PS_MULS1) {
    syncPair(a);
    syncPair(c);
    const char *name =
        op == DOLIR_EXACT_PS_MULS0 ? "ppc_ps_muls0" : "ppc_ps_muls1";
    auto callee = module_.getOrInsertFunction(
        name,
        FunctionType::get(Type::getVoidTy(context_), {ptr, i8, i8, i8}, false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt8(d), builder_.getInt8(a),
                                 builder_.getInt8(c)});
  } else {
    syncPair(a);
    u32 rhs = op == DOLIR_EXACT_PS_MUL ? c : b;
    syncPair(rhs);
    const char *name = op == DOLIR_EXACT_PS_ADD   ? "ppc_ps_add_op"
                       : op == DOLIR_EXACT_PS_SUB ? "ppc_ps_sub_op"
                       : op == DOLIR_EXACT_PS_MUL ? "ppc_ps_mul_op"
                                                  : "ppc_ps_div_op";
    auto callee = module_.getOrInsertFunction(
        name,
        FunctionType::get(Type::getVoidTy(context_), {ptr, i8, i8, i8}, false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt8(d), builder_.getInt8(a),
                                 builder_.getInt8(rhs)});
  }
  reloadPair(d);
  reloadState(DOLIR_STATE_FPSCR);
  fp_rep_[d] = FPRepresentation::Raw;
  fp_exact_single_[d] = false;
  fp_denormal_safe_[d] = false;
  if (op != DOLIR_EXACT_PS_RES && op != DOLIR_EXACT_PS_RSQRTE) {
    Value *result = pairF64(d);
    Value *single = builder_.CreateFPTrunc(
        result, FixedVectorType::get(Type::getFloatTy(context_), 2));
    builder_.CreateStore(single, pair_f32_[d]);
    fp_rep_[d] = FPRepresentation::PairF32;
    fp_exact_single_[d] = true;
    fp_denormal_safe_[d] = true;
  } else {
    fp_rep_[d] = FPRepresentation::Raw;
    fp_exact_single_[d] = false;
    fp_denormal_safe_[d] = false;
  }
}

} // namespace dolllvm

// paired singles; it's what makes gekko unique
// i have spent too much time here. I'm too far gone to have any sort of sanity.
