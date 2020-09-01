#if !defined(JVS_PSEUDO_PASSES_DEMOTE_REGISTERS_H_)
#define JVS_PSEUDO_PASSES_DEMOTE_REGISTERS_H_

#include <cstddef>
#include <utility>
#include <vector>

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

// forward declarations
namespace llvm
{

class AllocaInst;
class Function;
class Module;

} // namespace llvm

namespace jvs
{

struct DemotedInstructions
{
  std::size_t DemotedInstructionCount{0};
  std::size_t DemotedPhiNodeCount{0};
  std::vector<llvm::AllocaInst*> Allocas{};

  DemotedInstructions(std::size_t demotedInsts, std::size_t demotedPhiNodes,
    std::vector<llvm::AllocaInst*>&& allocaRange)
    : DemotedInstructionCount(demotedInsts),
    DemotedPhiNodeCount(demotedPhiNodes),
    Allocas(std::move(allocaRange))
  {
  }
};

// Clone of the legacy 'reg2mem' pass for the new pass manager.
struct DemoteRegistersPass : llvm::PassInfoMixin<DemoteRegistersPass>
{
  llvm::PreservedAnalyses run(llvm::Function& f,
    llvm::FunctionAnalysisManager& manager);
};

DemotedInstructions DemoteRegisters(llvm::Function& f, 
  bool demoteOperands = false);

} // namespace jvs


#endif // !JVS_PSEUDO_PASSES_DEMOTE_REGISTERS_H_
