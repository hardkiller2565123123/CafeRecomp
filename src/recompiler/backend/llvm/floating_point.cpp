#include "backend/llvm/emitter.h"
#include "cpu/cpu.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>

namespace dolllvm {

using namespace llvm;


void FunctionEmitter::emitExactFloat(u64 descriptor) {
  auto op = static_cast<DolIRExactFloat>(descriptor & 0xFFu);
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
  Type *ptr = PointerType::getUnqual(context_);
  Type *f64 = Type::getDoubleTy(context_);
  syncState(DOLIR_STATE_FPSCR);

  if (op == DOLIR_EXACT_FCMPU || op == DOLIR_EXACT_FCMPO) {
    auto crSlot = static_cast<DolIRStateSlot>(DOLIR_STATE_CR0 + crfd);
    syncState(crSlot);
    auto callee = module_.getOrInsertFunction(
        "ppc_fcmp", FunctionType::get(Type::getVoidTy(context_),
                                      {ptr, Type::getInt8Ty(context_), f64, f64,
                                       Type::getInt1Ty(context_)},
                                      false));
    builder_.CreateCall(callee, {ctx_, builder_.getInt8(crfd),
                                 stateValue(fprSlot(a)), stateValue(fprSlot(b)),
                                 builder_.getInt1(op == DOLIR_EXACT_FCMPO)});
    reloadState(crSlot);
    reloadState(DOLIR_STATE_FPSCR);
    return;
  }

  fp_rep_[d] = FPRepresentation::Raw;
  fp_exact_single_[d] = false;
  fp_denormal_safe_[d] = false;

  DolIRStateSlot destination = fprSlot(d);
  Value *old = stateValue(destination);
  if (op >= DOLIR_EXACT_FADDS && op <= DOLIR_EXACT_FRSP) {
    const char *name = nullptr;
    switch (op) {
    case DOLIR_EXACT_FADDS:
      name = "ppc_fadds";
      break;
    case DOLIR_EXACT_FSUBS:
      name = "ppc_fsubs";
      break;
    case DOLIR_EXACT_FMULS:
      name = "ppc_fmuls";
      break;
    case DOLIR_EXACT_FDIVS:
      name = "ppc_fdivs";
      break;
    case DOLIR_EXACT_FADD:
      name = "ppc_fadd";
      break;
    case DOLIR_EXACT_FSUB:
      name = "ppc_fsub";
      break;
    case DOLIR_EXACT_FMUL:
      name = "ppc_fmul";
      break;
    case DOLIR_EXACT_FDIV:
      name = "ppc_fdiv";
      break;
    case DOLIR_EXACT_FRSP:
      name = "ppc_frsp";
      break;
    default:
      break;
    }
    syncState(destination);
    bool single = op <= DOLIR_EXACT_FDIVS || op == DOLIR_EXACT_FRSP;
    if (single)
      syncState(ps1Slot(d));
    syncState(fprSlot(op == DOLIR_EXACT_FRSP ? b : a));
    if (op != DOLIR_EXACT_FRSP)
      syncState(
          fprSlot(op == DOLIR_EXACT_FMULS || op == DOLIR_EXACT_FMUL ? c : b));
    if (op == DOLIR_EXACT_FRSP) {
      auto callee = module_.getOrInsertFunction(
          name, FunctionType::get(
                    Type::getVoidTy(context_),
                    {ptr, Type::getInt8Ty(context_), Type::getInt8Ty(context_)},
                    false));
      builder_.CreateCall(callee,
                          {ctx_, builder_.getInt8(d), builder_.getInt8(b)});
    } else {
      auto callee = module_.getOrInsertFunction(
          name, FunctionType::get(Type::getVoidTy(context_),
                                  {ptr, Type::getInt8Ty(context_),
                                   Type::getInt8Ty(context_),
                                   Type::getInt8Ty(context_)},
                                  false));
      builder_.CreateCall(
          callee,
          {ctx_, builder_.getInt8(d), builder_.getInt8(a),
           builder_.getInt8(
               op == DOLIR_EXACT_FMULS || op == DOLIR_EXACT_FMUL ? c : b)});
    }
    reloadState(destination);
    if (single)
      reloadState(ps1Slot(d));
  } else if (op == DOLIR_EXACT_FCTIW || op == DOLIR_EXACT_FCTIWZ) {
    AllocaInst *output = temporary(Type::getInt64Ty(context_), "fctiw.result");
    builder_.CreateStore(
        builder_.CreateBitCast(old, Type::getInt64Ty(context_)), output);
    auto callee = module_.getOrInsertFunction(
        "ppc_fctiw",
        FunctionType::get(Type::getInt1Ty(context_),
                          {ptr, f64, Type::getInt1Ty(context_), ptr}, false));
    Value *success = builder_.CreateCall(
        callee, {ctx_, stateValue(fprSlot(b)),
                 builder_.getInt1(op == DOLIR_EXACT_FCTIWZ), output});
    Value *converted = builder_.CreateBitCast(
        builder_.CreateLoad(Type::getInt64Ty(context_), output), f64);
    builder_.CreateStore(builder_.CreateSelect(success, converted, old),
                         state_[destination]);
  } else if (op == DOLIR_EXACT_FRES || op == DOLIR_EXACT_FRSQRTE) {
    AllocaInst *output = temporary(f64, "estimate.result");
    builder_.CreateStore(old, output);
    const char *name = op == DOLIR_EXACT_FRES ? "ppc_fres" : "ppc_frsqrte";
    auto callee = module_.getOrInsertFunction(
        name,
        FunctionType::get(Type::getInt1Ty(context_), {ptr, f64, ptr}, false));
    Value *success =
        builder_.CreateCall(callee, {ctx_, stateValue(fprSlot(b)), output});
    Value *estimate = builder_.CreateLoad(f64, output);
    builder_.CreateStore(builder_.CreateSelect(success, estimate, old),
                         state_[destination]);
    if (op == DOLIR_EXACT_FRES) {
      DolIRStateSlot ps1 = ps1Slot(d);
      Value *oldPs1 = stateValue(ps1);
      builder_.CreateStore(builder_.CreateSelect(success, estimate, oldPs1),
                           state_[ps1]);
    }
  } else {
    bool single = op >= DOLIR_EXACT_FMADDS && op <= DOLIR_EXACT_FNMSUBS;
    bool subtract = op == DOLIR_EXACT_FMSUB || op == DOLIR_EXACT_FNMSUB ||
                    op == DOLIR_EXACT_FMSUBS || op == DOLIR_EXACT_FNMSUBS;
    bool negative = op == DOLIR_EXACT_FNMADD || op == DOLIR_EXACT_FNMSUB ||
                    op == DOLIR_EXACT_FNMADDS || op == DOLIR_EXACT_FNMSUBS;
    AllocaInst *output = temporary(f64, "fma.result");
    builder_.CreateStore(old, output);
    auto callee = module_.getOrInsertFunction(
        "ppc_fma",
        FunctionType::get(Type::getInt1Ty(context_),
                          {ptr, f64, f64, f64, Type::getInt1Ty(context_),
                           Type::getInt1Ty(context_), Type::getInt1Ty(context_),
                           ptr},
                          false));
    Value *success = builder_.CreateCall(
        callee,
        {ctx_, stateValue(fprSlot(a)), stateValue(fprSlot(c)),
         stateValue(fprSlot(b)), builder_.getInt1(single),
         builder_.getInt1(subtract), builder_.getInt1(negative), output});
    Value *fused = builder_.CreateLoad(f64, output);
    builder_.CreateStore(builder_.CreateSelect(success, fused, old),
                         state_[destination]);
    if (single) {
      DolIRStateSlot ps1 = ps1Slot(d);
      Value *oldPs1 = stateValue(ps1);
      builder_.CreateStore(builder_.CreateSelect(success, fused, oldPs1),
                           state_[ps1]);
    }
  }
  reloadState(DOLIR_STATE_FPSCR);
  bool singleResult =
      (op >= DOLIR_EXACT_FADDS && op <= DOLIR_EXACT_FDIVS) ||
      op == DOLIR_EXACT_FRSP ||
      (op >= DOLIR_EXACT_FMADDS && op <= DOLIR_EXACT_FNMSUBS);
  if (singleResult) {
    Value *single = builder_.CreateFPTrunc(
        pairF64(d), FixedVectorType::get(Type::getFloatTy(context_), 2));
    builder_.CreateStore(single, pair_f32_[d]);
    fp_rep_[d] = FPRepresentation::PairF32;
    fp_exact_single_[d] = true;
    fp_denormal_safe_[d] = true;
  }
}


} // namespace dolllvm
