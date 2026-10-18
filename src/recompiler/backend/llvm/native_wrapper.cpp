#include "backend/llvm/emitter.h"
#include "cpu/cpu.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

namespace dolllvm {

using namespace llvm;

bool FunctionEmitter::emitWrapper(raw_ostream &diagnostics) {
  auto *pointer = PointerType::getUnqual(context_);
  auto *type = FunctionType::get(Type::getVoidTy(context_), {pointer}, false);
  const std::string wrapperName = symbolName(source_.name);
  Function *wrapper = module_.getFunction(wrapperName);
  if (!wrapper)
    wrapper = Function::Create(type, GlobalValue::ExternalLinkage, wrapperName,
                               module_);
  if (wrapper->getFunctionType() != type || !wrapper->empty()) {
    diagnostics << "dolllvm: conflicting native entry " << source_.name << "\n";
    return false;
  }
  wrapper->setCallingConv(CallingConv::C);
  wrapper->setVisibility(GlobalValue::HiddenVisibility);
  wrapper->setDSOLocal(true);
  wrapper->getArg(0)->setName("ctx");
  wrapper->getArg(0)->addAttr(Attribute::NonNull);
  wrapper->getArg(0)->addAttr(
      Attribute::getWithDereferenceableBytes(context_, sizeof(CPUState)));

  BasicBlock *entry = BasicBlock::Create(context_, "entry", wrapper);
  IRBuilderBase::InsertPoint saved = builder_.saveIP();
  Argument *savedContext = ctx_;
  builder_.SetInsertPoint(entry);
  ctx_ = wrapper->getArg(0);
  StructType *chainTy = chainType();
  AllocaInst *chain = builder_.CreateAlloca(chainTy, nullptr, "chain");
  chain->setAlignment(Align(16));
  Value *guardCycles = builder_.CreateStructGEP(chainTy, chain, 1);
  Value *guardSteps = builder_.CreateStructGEP(chainTy, chain, 2);
  Value *pendingCycles = builder_.CreateStructGEP(chainTy, chain, 3);
  builder_.CreateStore(builder_.getInt64(0), guardCycles);
  builder_.CreateStore(builder_.getInt64(0), guardSteps);
  builder_.CreateStore(builder_.getInt64(0), pendingCycles);
  Value *entryPC = loadContext(DOLIR_STATE_PC);
  Value *returnPC =
      builder_.CreateAnd(loadContext(DOLIR_STATE_LR), builder_.getInt32(~3u));
  BasicBlock *escaped = nullptr;
  if (native_abi_ && cold_escapes_) {
    const u32 bufferWords = intrinsic_escapes_ ? 5u : 64u;
    ArrayType *bufferTy = ArrayType::get(pointer, bufferWords);
    Value *buffer = builder_.CreateStructGEP(chainTy, chain, 0);
    Value *jumped = nullptr;
    if (intrinsic_escapes_) {
      Value *frame =
          builder_.CreateCall(Intrinsic::getDeclaration(
                                  &module_, Intrinsic::frameaddress, {pointer}),
                              {builder_.getInt32(0)});
      builder_.CreateStore(
          frame,
          builder_.CreateInBoundsGEP(
              bufferTy, buffer, {builder_.getInt64(0), builder_.getInt64(0)}));
      Value *stack = builder_.CreateCall(
          Intrinsic::getDeclaration(&module_, Intrinsic::stacksave, {pointer}));
      builder_.CreateStore(
          stack,
          builder_.CreateInBoundsGEP(
              bufferTy, buffer, {builder_.getInt64(0), builder_.getInt64(2)}));
      jumped = builder_.CreateCall(
          Intrinsic::getDeclaration(&module_, Intrinsic::eh_sjlj_setjmp),
          {buffer});
    } else {
      auto setjmp = module_.getOrInsertFunction(
          "_setjmp",
          FunctionType::get(Type::getInt32Ty(context_), {pointer}, false));
      if (auto *setjmpFunction = dyn_cast<Function>(setjmp.getCallee())) {
        setjmpFunction->addFnAttr(Attribute::ReturnsTwice);
        setjmpFunction->addFnAttr(Attribute::NoUnwind);
      }
      jumped = builder_.CreateCall(setjmp, {buffer});
    }
    BasicBlock *invoke = BasicBlock::Create(context_, "invoke", wrapper);
    escaped = BasicBlock::Create(context_, "escaped", wrapper);
    builder_.CreateCondBr(builder_.CreateICmpEQ(jumped, builder_.getInt32(0)),
                          invoke, escaped);
    builder_.SetInsertPoint(invoke);
  }
  Value *control = builder_.CreateOr(
      builder_.CreateZExt(entryPC, Type::getInt64Ty(context_)),
      builder_.CreateShl(
          builder_.CreateZExt(returnPC, Type::getInt64Ty(context_)),
          builder_.getInt64(32)));
  SmallVector<Value *, 32> arguments = {wrapper->getArg(0), chain, control};
  if (native_abi_) {
    if (nativeCyclesInResult(abi_range_))
      arguments.push_back(builder_.getInt64(0));
    for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
      auto stateSlot = static_cast<DolIRStateSlot>(slot);
      if (stateInput(abi_range_, stateSlot))
        arguments.push_back(loadContext(stateSlot));
    }
  }
  CallInst *body = builder_.CreateCall(function_, arguments);
  body->setCallingConv(bodyCallingConvention());
  body->addFnAttr(Attribute::NoInline);
  if (native_abi_) {
    u32 field = cold_escapes_ ? 0u : 2u;
    for (u32 slot = 0; slot < DOLIR_STATE_COUNT; slot++) {
      auto stateSlot = static_cast<DolIRStateSlot>(slot);
      if (!stateOutput(abi_range_, stateSlot))
        continue;
      Value *value = cold_escapes_
                         ? nativeOutputValue(body, abi_range_, stateSlot)
                         : builder_.CreateExtractValue(body, field++);
      storeContext(stateSlot, value);
    }
    if (cold_escapes_) {
      Value *downcount =
          loadOffset(Type::getInt64Ty(context_), offsetof(CPUState, downcount));
      Value *cycles =
          nativeCyclesInResult(abi_range_)
              ? nativeCycleValue(body, abi_range_)
              : builder_.CreateLoad(Type::getInt64Ty(context_), pendingCycles);
      builder_.CreateStore(builder_.CreateSub(downcount, cycles),
                           bytePtr(offsetof(CPUState, downcount)));
      storeContext(DOLIR_STATE_PC, returnPC);
    } else {
      storeContext(DOLIR_STATE_PC, nativeResultPC(body));
    }
  }
  builder_.CreateRetVoid();
  if (escaped) {
    builder_.SetInsertPoint(escaped);
    builder_.CreateRetVoid();
  }
  ctx_ = savedContext;
  builder_.restoreIP(saved);
  return !verifyFunction(*wrapper, &diagnostics);
}

} // namespace dolllvm
