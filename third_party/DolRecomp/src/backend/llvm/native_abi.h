#ifndef DOLRECOMP_LLVM_NATIVE_ABI_H
#define DOLRECOMP_LLVM_NATIVE_ABI_H

#include "backend/llvm/llvm_backend.h"

#include <vector>

namespace dolllvm {

void prepareModuleABIs(const DolIRModule &source,
                       std::vector<DolLLVMFunctionRange> &ranges);

}

#endif
