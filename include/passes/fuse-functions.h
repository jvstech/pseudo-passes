#if !defined(JVS_PSEUDO_PASSES_FUSE_FUNCTIONS_H_)
#define JVS_PSEUDO_PASSES_FUSE_FUNCTIONS_H_

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

// forward declarations
namespace llvm
{

class Module;

} // namespace llvm

namespace jvs
{

struct FuseFunctionsPass : llvm::PassInfoMixin<FuseFunctionsPass>
{
  FuseFunctionsPass(bool ignoreNoInline = false);

  llvm::PreservedAnalyses run(llvm::Module& m,
    llvm::ModuleAnalysisManager& manager);

  const bool IgnoreNoInline;
};

} // namespace jvs


#endif // !JVS_PSEUDO_PASSES_FUSE_FUNCTIONS_H_
