#include "passes/breakpoint-net.h"

#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "support/value-util.h"

namespace
{

static constexpr char PluginName[] = "BreakpointNet";

// Pass registration
static llvm::PassPluginLibraryInfo getBreakpointNetPluginInfo()
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
          if (name.equals("breakpoint-net"))
          {
            fpm.addPass(jvs::BreakpointNetPass());
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
  return getBreakpointNetPluginInfo();
}



llvm::PreservedAnalyses jvs::BreakpointNetPass::run(llvm::Function& f, 
  llvm::FunctionAnalysisManager& manager)
{
  // Skip functions marked [[optnone]].
  if (f.hasFnAttribute(llvm::Attribute::OptimizeNone))
  {
    return llvm::PreservedAnalyses::all();
  }

  auto debugTrapFn = 
    llvm::Intrinsic::getDeclaration(f.getParent(), llvm::Intrinsic::debugtrap);
  bool modified{false};
  for (llvm::BasicBlock& block : f)
  {
    auto inst = &*block.begin();
    if (&block == &f.getEntryBlock())
    {
      // Skip allocas.
      for (; inst && llvm::isa<llvm::AllocaInst>(inst); 
        inst = inst->getNextNode())
      {
      }
    }

    if (!inst)
    {
      return llvm::PreservedAnalyses::all();
    }

    // Skip PHI nodes and landing pad/catch/cleanup instructions.
    for (; inst && 
      jvs::is_any<
        llvm::PHINode, 
        llvm::LandingPadInst, 
        llvm::CatchPadInst,
        llvm::CatchSwitchInst,
        llvm::CleanupPadInst
      >(inst); inst = inst->getNextNode())
    {
    }

    if (!inst)
    {
      return llvm::PreservedAnalyses::all();
    }

    for (; inst; inst = inst->getNextNode())
    {
      llvm::CallInst::Create(debugTrapFn, /* NameStr */ "", 
        /* InsertBefore */ inst);
      modified = true;
    }
  }

  if (!modified)
  {
    return llvm::PreservedAnalyses::all();
  }
  
  // We only inserted instructions that didn't affect the control flow.
  llvm::PreservedAnalyses preservedAnalyses{};
  preservedAnalyses.preserveSet<llvm::CFGAnalyses>();
  return preservedAnalyses;
}
