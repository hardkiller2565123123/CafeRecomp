#include "backend/llvm/emitter.h"

namespace dolllvm {

llvm::Value *FunctionEmitter::emitGuestLoad(llvm::Value *address,
                                             llvm::Type *resultType, u32 width,
                                             bool sign) {
  DolIRInstruction instruction{};
  return emitGuestLoad(instruction, address, resultType, width, sign);
}

void FunctionEmitter::emitGuestStore(llvm::Value *address, llvm::Value *value,
                                     u32 width) {
  DolIRInstruction instruction{};
  emitGuestStore(instruction, address, value, width);
}

} // namespace dolllvm
