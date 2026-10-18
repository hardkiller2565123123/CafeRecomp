#include "backend/llvm/emitter.h"
#include "cpu/cpu.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/Support/Alignment.h>

namespace dolllvm {

using namespace llvm;

Value *FunctionEmitter::normalizeAddress(Value *address) {
  return builder_.CreateAnd(address, builder_.getInt32(~0x40000000u));
}

Value *FunctionEmitter::provenMemoryPointer(
    const DolIRInstruction &instruction, Value *address, u32 width,
    Value **offset) {
  if (!fixed_memory_layout_ || instruction.address_domain != DOLIR_ADDRESS_MEM1 ||
      instruction.address_lower != instruction.address_upper)
    return nullptr;
  auto *constant = dyn_cast<ConstantInt>(address);
  if (!constant || constant->getZExtValue() != instruction.address_lower)
    return nullptr;
  const u32 normalized = instruction.address_lower & ~0x40000000u;
  if (expected_ram_size_ < width || normalized < GC_RAM_BASE ||
      normalized - GC_RAM_BASE > expected_ram_size_ - width)
    return nullptr;
  *offset = builder_.getInt32(normalized - GC_RAM_BASE);
  function_->addFnAttr("dolrecomp-native-memory", "mem1");
  return builder_.CreateInBoundsGEP(Type::getInt8Ty(context_), ram_, *offset);
}

Value *FunctionEmitter::rangeCheck(Value *normalized, u32 base, Value *size,
                                   u32 width) {
  Value *offset = builder_.CreateSub(normalized, builder_.getInt32(base));
  Value *largeEnough = builder_.CreateICmpUGE(size, builder_.getInt32(width));
  Value *last = builder_.CreateSub(size, builder_.getInt32(width));
  return builder_.CreateAnd(largeEnough, builder_.CreateICmpULE(offset, last));
}

Value *FunctionEmitter::endianLoad(Value *pointer, Type *resultType,
                                   u32 width) {
  Type *integerType = IntegerType::get(context_, width * 8u);
  LoadInst *load = builder_.CreateLoad(integerType, pointer, "native.load");
  load->setAlignment(Align(1));
  Value *loaded = load;
  loaded = bswap(loaded);
  if (resultType != integerType)
    loaded = builder_.CreateZExtOrTrunc(loaded, resultType);
  return loaded;
}

Value *FunctionEmitter::externalRead(Value *address, u32 width) {
  Type *ptr = PointerType::getUnqual(context_);
  Value *fn = loadOffset(ptr, offsetof(CPUState, external_read));
  BasicBlock *call = BasicBlock::Create(context_, "read_external", function_);
  BasicBlock *zero = BasicBlock::Create(context_, "read_unmapped", function_);
  BasicBlock *join = BasicBlock::Create(context_, "read_slow_join", function_);
  builder_.CreateCondBr(builder_.CreateIsNotNull(fn), call, zero,
                        MDBuilder(context_).createBranchWeights(2000, 1));
  builder_.SetInsertPoint(call);
  materialize(current_pc_);
  auto *functionType = FunctionType::get(
      Type::getInt64Ty(context_),
      {ptr, Type::getInt32Ty(context_), Type::getInt8Ty(context_)}, false);
  Value *called = builder_.CreateCall(functionType, fn,
                                      {ctx_, address, builder_.getInt8(width)});
  Value *exception =
      loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, exception));
  BasicBlock *resume =
      BasicBlock::Create(context_, "read_slow_resume", function_);
  BasicBlock *failed =
      BasicBlock::Create(context_, "read_slow_exit", function_);
  builder_.CreateCondBr(builder_.CreateICmpEQ(exception, builder_.getInt32(0)),
                        resume, failed);
  builder_.SetInsertPoint(failed);
  returnFromBody();
  builder_.SetInsertPoint(resume);
  reloadUsedState();
  builder_.CreateBr(join);
  BasicBlock *calledEnd = builder_.GetInsertBlock();
  builder_.SetInsertPoint(zero);
  Value *empty = builder_.getInt64(0);
  builder_.CreateBr(join);
  builder_.SetInsertPoint(join);
  PHINode *phi = builder_.CreatePHI(Type::getInt64Ty(context_), 2);
  phi->addIncoming(called, calledEnd);
  phi->addIncoming(empty, zero);
  return phi;
}

Value *FunctionEmitter::emitGuestLoad(const DolIRInstruction &instruction,
                                      Value *address, Type *resultType,
                                      u32 width, bool sign) {
  Value *directOffset = nullptr;
  if (Value *pointer =
          provenMemoryPointer(instruction, address, width, &directOffset)) {
    Value *loaded = endianLoad(pointer, resultType, width);
    if (sign && width * 8u < resultType->getIntegerBitWidth()) {
      Value *narrow = builder_.CreateTrunc(
          loaded, IntegerType::get(context_, width * 8u));
      return builder_.CreateSExt(narrow, resultType);
    }
    return loaded;
  }
  materializeFPRF();
  Value *normalized = normalizeAddress(address);
  Value *mem1 = rangeCheck(normalized, GC_RAM_BASE, ram_size_, width);
  BasicBlock *mem1Block = BasicBlock::Create(context_, "load_mem1", function_);
  BasicBlock *checkMem2 =
      BasicBlock::Create(context_, "load_check_mem2", function_);
  BasicBlock *mem2Block = BasicBlock::Create(context_, "load_mem2", function_);
  BasicBlock *slowBlock = BasicBlock::Create(context_, "load_slow", function_);
  BasicBlock *join = BasicBlock::Create(context_, "load_join", function_);
  builder_.CreateCondBr(mem1, mem1Block, checkMem2,
                        MDBuilder(context_).createBranchWeights(2000, 1));

  builder_.SetInsertPoint(mem1Block);
  Value *mem1Offset =
      builder_.CreateSub(normalized, builder_.getInt32(GC_RAM_BASE));
  Value *mem1Ptr =
      builder_.CreateInBoundsGEP(Type::getInt8Ty(context_), ram_, mem1Offset);
  Value *mem1Value = endianLoad(mem1Ptr, resultType, width);
  builder_.CreateBr(join);

  builder_.SetInsertPoint(checkMem2);
  Value *inMem2 = builder_.CreateAnd(
      builder_.CreateIsNotNull(mem2_),
      rangeCheck(normalized, WII_MEM2_BASE, mem2_size_, width));
  builder_.CreateCondBr(inMem2, mem2Block, slowBlock,
                        MDBuilder(context_).createBranchWeights(2000, 1));

  builder_.SetInsertPoint(mem2Block);
  Value *mem2Offset =
      builder_.CreateSub(normalized, builder_.getInt32(WII_MEM2_BASE));
  Value *mem2Ptr =
      builder_.CreateInBoundsGEP(Type::getInt8Ty(context_), mem2_, mem2Offset);
  Value *mem2Value = endianLoad(mem2Ptr, resultType, width);
  builder_.CreateBr(join);

  builder_.SetInsertPoint(slowBlock);
  Value *slow64 = externalRead(address, width);
  Value *slowValue = builder_.CreateZExtOrTrunc(slow64, resultType);
  BasicBlock *slowEnd = builder_.GetInsertBlock();
  builder_.CreateBr(join);

  builder_.SetInsertPoint(join);
  PHINode *phi = builder_.CreatePHI(resultType, 3);
  phi->addIncoming(mem1Value, mem1Block);
  phi->addIncoming(mem2Value, mem2Block);
  phi->addIncoming(slowValue, slowEnd);
  if (sign && width * 8u < resultType->getIntegerBitWidth()) {
    Value *narrow =
        builder_.CreateTrunc(phi, IntegerType::get(context_, width * 8u));
    return builder_.CreateSExt(narrow, resultType);
  }
  return phi;
}

void FunctionEmitter::clearReservation(Value *address) {
  Value *valid = builder_.CreateLoad(Type::getInt1Ty(context_),
                                     state_[DOLIR_STATE_RESERVE_VALID]);
  Value *reserved = builder_.CreateLoad(Type::getInt32Ty(context_),
                                        state_[DOLIR_STATE_RESERVE_ADDR]);
  Value *differentLine = builder_.CreateICmpNE(
      builder_.CreateAnd(builder_.CreateXor(reserved, address),
                         builder_.getInt32(~31u)),
      builder_.getInt32(0));
  builder_.CreateStore(builder_.CreateAnd(valid, differentLine),
                       state_[DOLIR_STATE_RESERVE_VALID]);
}

void FunctionEmitter::journal(Value *offset, u32 width) {
  if (!write_journal_)
    return;
  Type *ptr = PointerType::getUnqual(context_);
  GlobalVariable *journal = cast<GlobalVariable>(
      module_.getOrInsertGlobal("g_mem_write_journal", ptr));
  GlobalVariable *user = cast<GlobalVariable>(
      module_.getOrInsertGlobal("g_mem_write_journal_user", ptr));
  Value *fn = builder_.CreateLoad(ptr, journal);
  BasicBlock *call = BasicBlock::Create(context_, "journal", function_);
  BasicBlock *done = BasicBlock::Create(context_, "journal_done", function_);
  builder_.CreateCondBr(builder_.CreateIsNotNull(fn), call, done,
                        MDBuilder(context_).createBranchWeights(2000, 1));
  builder_.SetInsertPoint(call);
  auto *functionType = FunctionType::get(
      Type::getVoidTy(context_),
      {Type::getInt32Ty(context_), Type::getInt32Ty(context_), ptr}, false);
  builder_.CreateCall(
      functionType, fn,
      {offset, builder_.getInt32(width), builder_.CreateLoad(ptr, user)});
  builder_.CreateBr(done);
  builder_.SetInsertPoint(done);
}

void FunctionEmitter::endianStore(Value *pointer, Value *value, u32 width) {
  Type *integerType = IntegerType::get(context_, width * 8u);
  Value *narrowed = value;
  if (value->getType() != integerType)
    narrowed = builder_.CreateZExtOrTrunc(value, integerType);
  StoreInst *store = builder_.CreateStore(bswap(narrowed), pointer);
  store->setAlignment(Align(1));
}

void FunctionEmitter::externalWrite(Value *address, Value *value, u32 width) {
  Type *ptr = PointerType::getUnqual(context_);
  Value *fn = loadOffset(ptr, offsetof(CPUState, external_write));
  BasicBlock *call = BasicBlock::Create(context_, "write_external", function_);
  BasicBlock *done = BasicBlock::Create(context_, "write_slow_done", function_);
  builder_.CreateCondBr(builder_.CreateIsNotNull(fn), call, done);
  builder_.SetInsertPoint(call);
  materialize(current_pc_);
  auto *functionType =
      FunctionType::get(Type::getVoidTy(context_),
                        {ptr, Type::getInt32Ty(context_),
                         Type::getInt64Ty(context_), Type::getInt8Ty(context_)},
                        false);
  builder_.CreateCall(
      functionType, fn,
      {ctx_, address,
       builder_.CreateZExtOrTrunc(value, Type::getInt64Ty(context_)),
       builder_.getInt8(width)});
  Value *exception =
      loadOffset(Type::getInt32Ty(context_), offsetof(CPUState, exception));
  BasicBlock *resume =
      BasicBlock::Create(context_, "write_slow_resume", function_);
  BasicBlock *failed =
      BasicBlock::Create(context_, "write_slow_exit", function_);
  builder_.CreateCondBr(builder_.CreateICmpEQ(exception, builder_.getInt32(0)),
                        resume, failed);
  builder_.SetInsertPoint(failed);
  returnFromBody();
  builder_.SetInsertPoint(resume);
  reloadUsedState();
  builder_.CreateBr(done);
  builder_.SetInsertPoint(done);
}

void FunctionEmitter::emitGuestStore(const DolIRInstruction &instruction,
                                     Value *address, Value *value, u32 width) {
  Value *directOffset = nullptr;
  if (Value *pointer =
          provenMemoryPointer(instruction, address, width, &directOffset)) {
    clearReservation(address);
    journal(directOffset, width);
    endianStore(pointer, value, width);
    return;
  }
  materializeFPRF();
  clearReservation(address);
  Value *normalized = normalizeAddress(address);
  BasicBlock *mem1Block = BasicBlock::Create(context_, "store_mem1", function_);
  BasicBlock *checkMem2 =
      BasicBlock::Create(context_, "store_check_mem2", function_);
  BasicBlock *mem2Block = BasicBlock::Create(context_, "store_mem2", function_);
  BasicBlock *slowBlock = BasicBlock::Create(context_, "store_slow", function_);
  BasicBlock *join = BasicBlock::Create(context_, "store_join", function_);
  builder_.CreateCondBr(rangeCheck(normalized, GC_RAM_BASE, ram_size_, width),
                        mem1Block, checkMem2,
                        MDBuilder(context_).createBranchWeights(2000, 1));

  builder_.SetInsertPoint(mem1Block);
  Value *mem1Offset =
      builder_.CreateSub(normalized, builder_.getInt32(GC_RAM_BASE));
  journal(mem1Offset, width);
  Value *mem1Ptr =
      builder_.CreateInBoundsGEP(Type::getInt8Ty(context_), ram_, mem1Offset);
  endianStore(mem1Ptr, value, width);
  builder_.CreateBr(join);

  builder_.SetInsertPoint(checkMem2);
  Value *inMem2 = builder_.CreateAnd(
      builder_.CreateIsNotNull(mem2_),
      rangeCheck(normalized, WII_MEM2_BASE, mem2_size_, width));
  builder_.CreateCondBr(inMem2, mem2Block, slowBlock,
                        MDBuilder(context_).createBranchWeights(2000, 1));

  builder_.SetInsertPoint(mem2Block);
  Value *mem2Offset =
      builder_.CreateSub(normalized, builder_.getInt32(WII_MEM2_BASE));
  Value *mem2Ptr =
      builder_.CreateInBoundsGEP(Type::getInt8Ty(context_), mem2_, mem2Offset);
  endianStore(mem2Ptr, value, width);
  builder_.CreateBr(join);

  builder_.SetInsertPoint(slowBlock);
  externalWrite(address, value, width);
  builder_.CreateBr(join);
  builder_.SetInsertPoint(join);
}

} // namespace dolllvm
