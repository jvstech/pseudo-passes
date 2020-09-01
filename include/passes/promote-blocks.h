#if !defined(JVS_PSEUDO_PASSES_PROMOTE_BLOCKS_H_)
#define JVS_PSEUDO_PASSES_PROMOTE_BLOCKS_H_

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

// forward declarations
namespace llvm
{

class Module;

} // namespace llvm

namespace jvs
{

struct PromoteBlocksPass : llvm::PassInfoMixin<PromoteBlocksPass>
{
  PromoteBlocksPass(bool perInstruction = false);

  llvm::PreservedAnalyses run(llvm::Module& m,
    llvm::ModuleAnalysisManager& manager);

  const bool PerInstruction;
};

} // namespace jvs



#endif // !JVS_PSEUDO_PASSES_PROMOTE_BLOCKS_H_
