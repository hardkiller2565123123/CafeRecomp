#ifndef DOLRECOMP_LLVM_PASSES_H
#define DOLRECOMP_LLVM_PASSES_H

#include "backend/llvm/llvm_backend.h"

namespace llvm {
class Module;
class TargetMachine;
class raw_ostream;
}

namespace dolllvm {

bool optimizeModule(llvm::Module &module, llvm::TargetMachine &machine,
                    const DolLLVMOptions &options,
                    llvm::raw_ostream &diagnostics);

} // namespace dolllvm

#endif
