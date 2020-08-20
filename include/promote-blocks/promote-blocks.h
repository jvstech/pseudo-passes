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
  llvm::PreservedAnalyses run(llvm::Module& m,
    llvm::ModuleAnalysisManager& manager);
};

} // namespace jvs



#endif // !JVS_PSEUDO_PASSES_PROMOTE_BLOCKS_H_
