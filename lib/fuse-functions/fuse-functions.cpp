#include "fuse-functions/fuse-functions.h"

#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LowerInvoke.h"

#include "support/pass-pipeline.h"

// std namespace injection to allow decomposition of 
// llvm::detail::DenseMapPair<K, V>
namespace std
{

template <typename K, typename V>
class tuple_size<llvm::detail::DenseMapPair<K, V>> 
  : public std::integral_constant<std::size_t, 2>
{
};

template <typename K, typename V>
class tuple_element<0, llvm::detail::DenseMapPair<K, V>>
{
public:
  using type = K;
};

template <typename K, typename V>
class tuple_element<1, llvm::detail::DenseMapPair<K, V>>
{
public:
  using type = V;
};

} // namespace std


namespace
{

static constexpr char PassName[] = "fuse-functions";
#define DEBUG_TYPE ::PassName

struct CombinedCallSites
{
  std::vector<llvm::Function*> ModifiedCallers{};
  std::vector<llvm::CallInst*> CallSites{};
};

using CallerCalleeCallSitesMap = 
  llvm::DenseMap<llvm::Function*, 
    llvm::DenseMap<llvm::Function*, std::vector<llvm::CallInst*>>>;

static void get_candidate_call_sites(llvm::Function& func, 
  bool ignoreNoInline, const llvm::DenseSet<llvm::CallInst*>& callsToIgnore,
  CallerCalleeCallSitesMap& candidateCallInsts)
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
    for (llvm::CallInst* callInst : callInsts)
    {
      candidateCallInsts[callInst->getFunction()][callInst->getCalledFunction()]
        .push_back(callInst);
    }
  }
}

// This finds all the call sites in a module that are potential candidates for
// inlining. This could be turned into an analysis pass.
static CallerCalleeCallSitesMap get_candidate_call_sites(llvm::Module& m,
  bool ignoreNoInline, const llvm::DenseSet<llvm::CallInst*>& callsToIgnore)
{
  CallerCalleeCallSitesMap candidateCallMap{};
  auto funcs = llvm::make_filter_range(m, [=](llvm::Function& f)
    {
      return (!f.hasFnAttribute(llvm::Attribute::OptimizeNone)
        && !f.isDeclaration()
        && !f.empty()
        && (ignoreNoInline || !f.hasFnAttribute(llvm::Attribute::NoInline)));
    });
  for (llvm::Function& func : funcs)
  {
    get_candidate_call_sites(func, ignoreNoInline, callsToIgnore, 
      candidateCallMap);
  }

  return candidateCallMap;
}

static llvm::BasicBlock* get_or_create_unreachable_block(llvm::Function& func)
{
  auto unreachableBlocks = llvm::make_filter_range(func,
    [](llvm::BasicBlock& block)
    {
      return (block.size() == 1 && 
        llvm::isa<llvm::UnreachableInst>(block.front()));
    });
  for (auto& block : unreachableBlocks)
  {
    return &block;
  }

  auto* unreachableBlock = llvm::BasicBlock::Create(func.getContext(),
    "", &func);
  new llvm::UnreachableInst(func.getContext(), unreachableBlock);
  return unreachableBlock;
}

static CombinedCallSites combine_calls(CallerCalleeCallSitesMap& callMap)
{
  CombinedCallSites combinedCalls{};
  for (auto& [caller, calleeCallSites] : callMap)
  {
    if (calleeCallSites.empty())
    {
      // Can't do anything with no call sites.
      continue;
    }

    for (auto& [callee, callSites] : calleeCallSites)
    {
      if (callSites.empty())
      {
        // Can't do anything with no call sites.
        continue;
      }

      if (callSites.size() == 1)
      {
        // There's no reason to touch a single call instruction to a single 
        // function -- that's exactly what we want.
        combinedCalls.CallSites.push_back(callSites.front());
        continue;
      }

      std::vector<llvm::Value*> args{};
      llvm::DenseMap<llvm::BasicBlock*, llvm::BasicBlock*> callSiteRet{};
      llvm::DenseMap<llvm::Instruction*, llvm::BasicBlock*> callSiteOrigParent{};
      llvm::CallInst* combinedCall{nullptr};
      llvm::BasicBlock* callBlock =
        llvm::BasicBlock::Create(caller->getContext(), "", caller);
      combinedCalls.ModifiedCallers.push_back(caller);

      for (llvm::CallInst* callSite : callSites)
      {
        auto* parentBlock = callSite->getParent();
        auto* retBlock = parentBlock->splitBasicBlock(callSite->getNextNode());
        callSiteOrigParent[callSite] = parentBlock;
        callSiteRet[parentBlock] = retBlock;
        // Move the call instruction to the safe beginning of the return block.
        callSite->moveBefore(retBlock->getFirstNonPHI());

        // Generate a branch to the call block and get rid of the branch 
        // generated by splitBasicBlock().
        auto* branchInst = llvm::BranchInst::Create(callBlock, parentBlock);
        branchInst->getPrevNode()->eraseFromParent();
      }

      if (!callee->arg_empty())
      {
        // Create a PHI node for each source basic block+value pair made to this
        // call.
        std::size_t argCount{0};
        for (auto& arg : callee->args())
        {
          auto* argNode = llvm::PHINode::Create(arg.getType(), callSites.size(),
            "", callBlock);
          for (llvm::CallInst* c : callSites)
          {
            argNode->addIncoming(
              c->getArgOperand(argCount), callSiteOrigParent[c]);
          }

          args.push_back(argNode);
          ++argCount;
        }
      }

      // This is the actual call to the callee.
      combinedCall = llvm::CallInst::Create(callee,
        llvm::makeArrayRef(args), "", callBlock);
      std::size_t callCount{0};
      for (llvm::CallInst* callInst : callSites)
      {
        // Remove the original call and replace all its references to the
        // combined call.
        callInst->replaceAllUsesWith(combinedCall);
        callInst->eraseFromParent();
        ++callCount;
      }

      // Create PHI nodes and switch instructions for branching back to the 
      // return blocks.
      auto* fromNode = llvm::PHINode::Create(
        llvm::Type::getInt32Ty(caller->getContext()), callCount, "",
        combinedCall);
      // Since we must have a default case, we point it at an unreachable block
      // (since all cases are being accounted for).
      auto* switchBack = llvm::SwitchInst::Create(fromNode,
        get_or_create_unreachable_block(*caller), callSiteRet.size(),
        callBlock);
      std::size_t switchCount{0};
      for (auto& [parentBlock, retBlock] : callSiteRet)
      {
        auto* branchIdx = llvm::ConstantInt::get(caller->getContext(),
          llvm::APInt(32, switchCount++, true));
        fromNode->addIncoming(branchIdx, parentBlock);
        switchBack->addCase(branchIdx, retBlock);
      }

      combinedCalls.CallSites.push_back(combinedCall);
    }
  }
  
  return combinedCalls;
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
    run_pass_pipeline(m, "module(function(lowerinvoke,simplifycfg),mergefunc)");
  if (!parseError.empty())
  {
    llvm::errs() << "Error parsing passes: " << parseError << '\n';
    return preservedAnalysis;
  }

  llvm::PreservedAnalyses result = preservedAnalysis;
  llvm::DenseSet<llvm::CallInst*> failedInlineCallSites{};
  llvm::SetVector<llvm::Function*> callTargets{};
  CallerCalleeCallSitesMap callMap =
    get_candidate_call_sites(m, IgnoreNoInline, failedInlineCallSites);
  auto combinedCalls = combine_calls(callMap);
  std::unique_ptr<llvm::FunctionPass> reg2mem(
    llvm::createDemoteRegisterToMemoryPass());
  std::optional<PassPipeline> mem2regPass(
    std::in_place, PassPipeline::create_function_pipeline("mem2reg"));
  if (!mem2regPass->parse_error().empty())
  {
    llvm::errs() << mem2regPass->parse_error() << '\n';
    mem2regPass.reset();
  }

  llvm::for_each(combinedCalls.ModifiedCallers, [&](llvm::Function* f)
    {
      if (reg2mem->runOnFunction(*f))
      {
        result = llvm::PreservedAnalyses::none();
      }

      if (mem2regPass && !mem2regPass->run(*f).areAllPreserved())
      {
        result = llvm::PreservedAnalyses::none();
      }
    });
  
  do
  {    
    for (llvm::CallInst* callInst = nullptr; !combinedCalls.CallSites.empty(); )
    {
      if (combinedCalls.CallSites.empty())
      {
        break;
      }
  
      callInst = combinedCalls.CallSites.back();
      combinedCalls.CallSites.pop_back();
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
  
      result = llvm::PreservedAnalyses::none();
      ++NumInlinedCalls;
      callTargets.insert(candidateCallTarget);
    }

    callMap =
      get_candidate_call_sites(m, IgnoreNoInline, failedInlineCallSites);
    combinedCalls = combine_calls(callMap);
    if (mem2regPass)
    {
      mem2regPass.emplace(PassPipeline::create_function_pipeline("mem2reg"));
    }

    llvm::for_each(combinedCalls.ModifiedCallers, [&](llvm::Function* f)
      {
        if (reg2mem->runOnFunction(*f))
        {
          result = llvm::PreservedAnalyses::none();
        }

        if (mem2regPass && !mem2regPass->run(*f).areAllPreserved())
        {
          result = llvm::PreservedAnalyses::none();
        }
      });
  } while (!combinedCalls.CallSites.empty());
  
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

