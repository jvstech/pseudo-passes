#if !defined(JVS_PSEUDO_PASSES_RESIZE_MALLOC_H_)
#define JVS_PSEUDO_PASSES_RESIZE_MALLOC_H_

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

// forward declarations
namespace llvm
{

class Function;

} // namespace llvm

namespace jvs
{

struct ResizeMallocPass : llvm::PassInfoMixin<ResizeMallocPass>
{
  llvm::PreservedAnalyses run(llvm::Function& f,
    llvm::FunctionAnalysisManager& manager);
};

} // namespace jvs


#endif // !JVS_PSEUDO_PASSES_RESIZE_MALLOC_H_
