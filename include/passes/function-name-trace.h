#if !defined(JVS_PSEUDO_PASSES_FUNCTION_NAME_TRACE_H_)
#define JVS_PSEUDO_PASSES_FUNCTION_NAME_TRACE_H_

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

// forward declarations
namespace llvm
{

class Module;

} // namespace llvm

namespace jvs
{

struct FunctionNameTracePass : llvm::PassInfoMixin<FunctionNameTracePass>
{
  llvm::PreservedAnalyses run(llvm::Module& m,
    llvm::ModuleAnalysisManager& manager);
};

} // namespace jvs


#endif // !JVS_PSEUDO_PASSES_FUNCTION_NAME_TRACE_H_
