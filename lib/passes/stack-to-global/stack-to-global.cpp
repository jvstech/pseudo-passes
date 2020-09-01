#include "passes/stack-to-global.h"

#include <vector>

#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/FormatVariadic.h"
#include "passes/demote-registers.h"
#include "support/value-util.h"

namespace
{

static constexpr char PluginName[] = "StackToGlobal";

// Pass registration
static llvm::PassPluginLibraryInfo getStackToGlobalPluginInfo()
{
  return
  {
    LLVM_PLUGIN_API_VERSION,
    PluginName,
    LLVM_VERSION_STRING,
    [](llvm::PassBuilder& passBuilder)
    {
      // Function passes
      passBuilder.registerPipelineParsingCallback(
        [](llvm::StringRef name, llvm::FunctionPassManager& fpm,
          llvm::ArrayRef<llvm::PassBuilder::PipelineElement>)
        {
          if (name.equals("stack-to-global"))
          {
            fpm.addPass(jvs::StackToGlobalPass());
            return true;
          }

          return false;
        });
    }
  };
}

} // namespace

// This function is required for `opt` to be able to recognize this pass when
// requested in the pass pipeline.
extern "C" LLVM_ATTRIBUTE_WEAK auto llvmGetPassPluginInfo()
-> ::llvm::PassPluginLibraryInfo
{
  return getStackToGlobalPluginInfo();
}

llvm::PreservedAnalyses jvs::StackToGlobalPass::run(llvm::Function& f, 
  llvm::FunctionAnalysisManager& manager)
{
  // Skip functions marked [[optnone]] and declarations.
  if (f.isDeclaration() || f.hasFnAttribute(llvm::Attribute::OptimizeNone))
  {
    return llvm::PreservedAnalyses::all();
  }

  bool modified{false};

  // Demote everything to the stack. Counterintuitive to this pass, I know...
  auto demoted = jvs::DemoteRegisters(f, true);
  modified = (demoted.DemotedInstructionCount || demoted.DemotedPhiNodeCount);

  // Collect all the static allocas in the entry block.
  std::vector<llvm::AllocaInst*> entryAllocas{};
  entryAllocas.reserve(f.getEntryBlock().sizeWithoutDebug());
  auto inst = &*f.getEntryBlock().begin();
  while (auto allocaInst = llvm::dyn_cast_or_null<llvm::AllocaInst>(inst))
  {
    entryAllocas.push_back(allocaInst);
    inst = inst->getNextNode();
  }

  llvm::Module& m = *f.getParent();
  llvm::SmallString<32> funcName = !f.hasName()
    ? llvm::formatv("function-{0:x}", &f).sstr<32>().str()
    : f.getName();

  // Create global variables for each of the allocas.
  for (llvm::AllocaInst* allocaInst : entryAllocas)
  {
    llvm::SmallString<16> allocaName = !allocaInst->hasName()
      ? llvm::formatv("alloca-{0:x}", allocaInst).sstr<16>().str()
      : allocaInst->getName();
  
    auto globalSlot = new llvm::GlobalVariable(m, 
      allocaInst->getAllocatedType(),
      /* isConstant */ false,
      // Let's make them external so every other program can access them, too.
      // I mean, why not? I'm not driving. Cheers!
      llvm::GlobalValue::LinkageTypes::ExternalLinkage,
      /* Initializer */ nullptr,
      /* Name */ llvm::formatv("{0}.{1}.state", funcName, allocaName));
    allocaInst->replaceAllUsesWith(globalSlot);
    allocaInst->eraseFromParent();
    modified = true;
  }

  if (!modified)
  {
    return llvm::PreservedAnalyses::none();
  }

  // Nothing we've done has broken control flow.
  llvm::PreservedAnalyses preservedAnalyses{};
  preservedAnalyses.preserveSet<llvm::CFGAnalyses>();
  return preservedAnalyses;
}
