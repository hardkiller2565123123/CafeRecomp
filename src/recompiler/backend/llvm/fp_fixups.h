#pragma once

namespace llvm {
class Function;
class Module;
class Type;
} // namespace llvm

namespace dolllvm {

llvm::Function *getPairNaNFixup(llvm::Module &module, llvm::Type *pairType);
llvm::Function *getFPRFUnusualFixup(llvm::Module &module);
llvm::Function *getPairFMANaNFixup(llvm::Module &module, llvm::Type *pairType);
llvm::Function *getPairFMARoundingFixup(llvm::Module &module);

} // namespace dolllvm
