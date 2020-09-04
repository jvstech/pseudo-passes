#if !defined(JVS_PSEUDO_PASSES_PACHINKO_CALLS_H_)
#define JVS_PSEUDO_PASSES_PACHINKO_CALLS_H_

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

// forward declarations
namespace llvm
{

class Module;

} // namespace llvm

namespace jvs
{

struct PachinkoCallsPass : llvm::PassInfoMixin<PachinkoCallsPass>
{
  llvm::PreservedAnalyses run(llvm::Module& m, 
    llvm::ModuleAnalysisManager& manager);
};

} // namespace jvs



#endif // !JVS_PSEUDO_PASSES_PACHINKO_CALLS_H_
