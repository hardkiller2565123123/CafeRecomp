#ifndef DOLRECOMP_LLVM_TARGET_H
#define DOLRECOMP_LLVM_TARGET_H

#include "backend/llvm/llvm_backend.h"

#include <memory>
#include <string>

#include <llvm/Support/CodeGen.h>

namespace llvm {
class TargetMachine;
}

namespace dolllvm {

struct TargetProfile {
  std::string triple;
  std::string cpu;
  std::string features;
  const char *name;
  const char *suffix;
  bool aarch64;
};

bool resolveTargetProfile(const DolLLVMOptions *options, TargetProfile &result,
                          std::string &error);
std::unique_ptr<llvm::TargetMachine>
createTargetMachine(const TargetProfile &profile, int optimization_level,
                    DolLLVMSemantics semantics,
                    std::string &error);
bool objectMatchesProfile(const char *path, const TargetProfile &profile);
void initializeTargets();

} // namespace dolllvm

#endif
