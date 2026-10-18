#ifndef DOLRECOMP_LLVM_PSQ_CONVERT_H
#define DOLRECOMP_LLVM_PSQ_CONVERT_H

#include <llvm/IR/IRBuilder.h>

namespace llvm {
class Module;
class Value;
} // namespace llvm

namespace dolllvm {

llvm::Value *extendPSQFloat(llvm::IRBuilder<> &builder, llvm::Module &module,
                            llvm::Value *bits);
llvm::Value *dequantizePSQInteger(llvm::IRBuilder<> &builder,
                                  llvm::Value *value, llvm::Value *scale,
                                  bool is_signed);
llvm::Value *truncatePSQFloat(llvm::IRBuilder<> &builder, llvm::Value *value);
llvm::Value *quantizePSQFloat(llvm::IRBuilder<> &builder, llvm::Value *value,
                              llvm::Value *scale, int minimum, int maximum);

} // namespace dolllvm

#endif
