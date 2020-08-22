#include "fuse-functions/fuse-functions.h"

#include <iterator>
#include <vector>

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LowerInvoke.h"

#include "support/pass-pipeline.h"

namespace
{

static constexpr char PassName[] = "fuse-functions";
#define DEBUG_TYPE ::PassName

// This finds all the call sites in a module that are potential candidates for
// inlining. This could be turned into an analysis pass.
static std::vector<llvm::CallInst*> get_candidate_call_sites(llvm::Module& m,
  bool ignoreNoInline, const llvm::SetVector<llvm::CallInst*>& callsToIgnore)
{
  std::vector<llvm::CallInst*> candidateCallInsts{};
  auto funcs = llvm::make_filter_range(m, [=](llvm::Function& f)
    {
      return (!f.hasFnAttribute(llvm::Attribute::OptimizeNone)
        && !f.isDeclaration()
        && !f.empty()
        && (ignoreNoInline || !f.hasFnAttribute(llvm::Attribute::NoInline)));
    });
  for (llvm::Function& func : funcs)
  {
    for (llvm::BasicBlock& block : func)
    {
      auto insts = llvm::make_filter_range(block,
        [&callsToIgnore](llvm::Instruction& inst)
        {
          if (auto* callInst = llvm::dyn_cast<llvm::CallInst>(&inst))
          {
            return (!callInst->isInlineAsm() && 
              callInst->getCalledFunction() &&
              !callInst->getCalledFunction()->isIntrinsic() &&
              callInst->getCalledFunction() != callInst->getFunction() &&
              !callsToIgnore.count(callInst));
          }

          return false;
        });
      auto callInsts = llvm::map_range(insts,
        [](llvm::Instruction& inst)
        {
          return llvm::cast<llvm::CallInst>(&inst);
        });
      std::copy(callInsts.begin(), callInsts.end(), 
        std::back_inserter(candidateCallInsts));
    }
  }

  return candidateCallInsts;
}


} // namespace 


STATISTIC(NumInlinedCalls, "Number of inlined calls");
STATISTIC(NumFailedInlinedCalls, "Number of calls that failed to be inlined");
STATISTIC(NumFusedFunctions, "Number of fused functions");


jvs::FuseFunctionsPass::FuseFunctionsPass(bool ignoreNoInline /*= false*/)
  : IgnoreNoInline(ignoreNoInline)
{
}


llvm::PreservedAnalyses jvs::FuseFunctionsPass::run(llvm::Module& m,
  llvm::ModuleAnalysisManager& manager)
{
  // We lower invoke instructions to call instructions to be able to inline more
  // code.
  auto [preservedAnalysis, parseError] =
    run_pass_pipeline(m, "lowerinvoke,simplifycfg");
  if (!parseError.empty())
  {
    llvm::errs() << "Error parsing passes: " << parseError << '\n';
    return preservedAnalysis;
  }
  
  llvm::SetVector<llvm::CallInst*> failedInlineCallSites{};
  llvm::SetVector<llvm::Function*> callTargets{};
  std::vector<llvm::CallInst*> callInsts = 
    get_candidate_call_sites(m, IgnoreNoInline, failedInlineCallSites);
  while (!callInsts.empty())
  {
    for (llvm::CallInst* callInst = nullptr; !callInsts.empty(); )
    {
      if (callInsts.empty())
      {
        break;
      }

      callInst = callInsts.back();
      callInsts.pop_back();
      llvm::Function* candidateCallTarget = callInst->getCalledFunction();
      llvm::InlineFunctionInfo inlineFuncInfo{};
      auto inlineResult = llvm::InlineFunction(*callInst, inlineFuncInfo);
      if (!inlineResult.isSuccess())
      {
        ++NumFailedInlinedCalls;
        LLVM_DEBUG(llvm::dbgs() << "Failed to inline call: "
          << inlineResult.getFailureReason() << "\nCall: "
          << *callInst);
        failedInlineCallSites.insert(callInst);
        continue;
      }

      ++NumInlinedCalls;
      callTargets.insert(candidateCallTarget);
    }

    callInsts = 
      get_candidate_call_sites(m, IgnoreNoInline, failedInlineCallSites);    
  }

  for (llvm::Function* callTarget : callTargets)
  {
    callTarget->removeDeadConstantUsers();
    if (callTarget->isDefTriviallyDead())
    {
      callTarget->eraseFromParent();
      ++NumFusedFunctions;
    }
  }

  return llvm::PreservedAnalyses::none();
}

