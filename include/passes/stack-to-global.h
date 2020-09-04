#if !defined(JVS_PSEUDO_PASSES_STACK_TO_GLOBAL_H_)
#define JVS_PSEUDO_PASSES_STACK_TO_GLOBAL_H_

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

// forward declarations
namespace llvm
{

class Function;

} // namespace llvm

namespace jvs
{

struct StackToGlobalPass : llvm::PassInfoMixin<StackToGlobalPass>
{
  llvm::PreservedAnalyses run(llvm::Function& m,
    llvm::FunctionAnalysisManager& manager);
};

} // namespace jvs


#endif // !JVS_PSEUDO_PASSES_STACK_TO_GLOBAL_H_
