#include "passes/fuse-functions.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LowerInvoke.h"

#include "combined-call-site.h"
#include "support/metadata-util.h"
#include "support/pass-pipeline.h"
#include "support/unqualified.h"

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
STATISTIC(NumInlinedCalls, "Number of inlined calls");
STATISTIC(NumFailedInlinedCalls, "Number of calls that failed to be inlined");
STATISTIC(NumFusedFunctions, "Number of fused functions");
STATISTIC(NumRegsDemoted, "Number of registers demoted");
STATISTIC(NumPhisDemoted, "Number of phi-nodes demoted");

struct CombinedCallSiteWorkLists
{
  llvm::SetVector<llvm::Function*> ModifiedCallers{};
  llvm::SetVector<llvm::CallInst*> CallSites{};
};

using CalleeCallSitesMap =  
  llvm::MapVector<llvm::Function*, std::vector<llvm::CallInst*>>;

using CallerCalleeCallSitesMap = 
  llvm::MapVector<llvm::Function*, CalleeCallSitesMap>;

using CalleeBlockMap =
  llvm::MapVector<llvm::Function*, llvm::BasicBlock*>;

static bool does_value_escape(const llvm::Instruction& inst)
{
  const llvm::BasicBlock* bb = inst.getParent();
  for (const llvm::User* u : inst.users())
  {
    const llvm::Instruction* userInst = llvm::cast<llvm::Instruction>(u);
    if (userInst->getParent() != bb || llvm::isa<llvm::PHINode>(userInst))
    {
      return true;
    }
  }

  return false;
}

static void demote_registers(llvm::Function& f)
{
  // Find the first non-alloca instruction and create an insertion point. This 
  // is safe if the block is well-formed: it will always have a terminator;
  // otherwise we'll trigger an assertion.
  auto& bbEntry = f.getEntryBlock();
  llvm::BasicBlock::iterator it = bbEntry.begin();
  for (; llvm::isa<llvm::AllocaInst>(it); ++it)
  {
  }

  llvm::CastInst* allocaInsertionPoint = new llvm::BitCastInst(
    llvm::Constant::getNullValue(llvm::Type::getInt32Ty(f.getContext())),
    llvm::Type::getInt32Ty(f.getContext()), "reg2mem alloca point", &*it);
  // Find the escaped instructions, but don't create stack slots for
  // allocas in entry block.
  llvm::SmallVector<llvm::Instruction*, 128> workList{};
  for (llvm::BasicBlock& ibb : f)
  {
    for (llvm::BasicBlock::iterator iib = ibb.begin(), iie = ibb.end();
      iib != iie; ++iib)
    {
      if (!(llvm::isa<llvm::AllocaInst>(iib) && iib->getParent() == &bbEntry) &&
        does_value_escape(*iib))
      {
        workList.push_back(&*iib);
      }
    }
  }

  // Demote escaped instructions.
  NumRegsDemoted += workList.size();
  for (llvm::Instruction* ilb : llvm::reverse(workList))
  {
    auto callInst = llvm::dyn_cast<llvm::CallInst>(ilb);
    llvm::StringRef calleeName{};
    if (callInst && callInst->getCalledFunction() && 
      !callInst->getCalledFunction()->isDeclaration())
    {
      calleeName = callInst->getCalledFunction()->getName();
    }

    auto* instAllocaInst =
      llvm::DemoteRegToStack(*ilb, false, allocaInsertionPoint);
    if (callInst)
    {
      if (!calleeName.empty())
      {
        jvs::attach_metadata(*instAllocaInst, jvs::FuseFunctionRet, calleeName);
        for (auto user : instAllocaInst->users())
        {
          if (auto storeInst = llvm::dyn_cast<llvm::StoreInst>(user))
          {
            jvs::attach_metadata(*storeInst, jvs::FuseFunctionRet, calleeName);
          }
          else if (auto userInst = llvm::dyn_cast<llvm::Instruction>(user))
          {
            if (llvm::isa<llvm::SwitchInst>(userInst->getNextNode()))
            {
              jvs::attach_metadata(*userInst, jvs::FuseFunctionRet, calleeName);
            }
          }
        }
      }

    }
  }

  workList.clear();

  // Find all phi nodes.
  for (llvm::BasicBlock& ibb : f)
  {
    for (llvm::BasicBlock::iterator iib = ibb.begin(), iie = ibb.end();
      iib != iie; ++iib)
    {
      if (llvm::isa<llvm::PHINode>(iib))
      {
        workList.push_back(&*iib);
      }
    }
  }

  // Demote phi nodes.
  NumPhisDemoted += workList.size();
  for (llvm::Instruction* ilb : llvm::reverse(workList))
  {
    // Save the fuse.function metadata (if any exists).
    auto funcName = jvs::get_metadata(*ilb, jvs::FuseFunctionName);
    auto funcArgIdx = jvs::get_metadata(*ilb, jvs::FuseFunctionArgIdx);
    llvm::AllocaInst* demoted = llvm::DemotePHIToStack(
      llvm::cast<llvm::PHINode>(ilb), allocaInsertionPoint);
    // Attach the fuse.function metadata to the load instructions that use the
    // new alloca instruction.
    if (funcName || funcArgIdx)
    {
      auto loadInstRange = llvm::map_range(demoted->users(),
        [](llvm::User* user)
        {            
          return llvm::dyn_cast<llvm::LoadInst>(user);
        });
      for (llvm::LoadInst* loadInst : loadInstRange)
      {
        if (loadInst)
        {
          if (funcName)
          {
            jvs::attach_metadata(*loadInst, jvs::FuseFunctionName, *funcName);
          }

          if (funcArgIdx)
          {
            jvs::attach_metadata(*loadInst, jvs::FuseFunctionArgIdx,
              *funcArgIdx);
          }
        }
      }
    }
  }
}

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
            !callInst->getCalledFunction()->isDeclaration() &&
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

/// This finds all the call sites in a module that are potential candidates for
/// inlining. This could be turned into an analysis pass.
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

/// Returns the first block in the given function containing only an 
// 'unreachable' instruction, or creates one if it doesn't exist.
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

static CombinedCallSiteWorkLists combine_calls(
  CallerCalleeCallSitesMap& callMap)
{
  CombinedCallSiteWorkLists workLists{};
  for (auto& [caller, calleeCallSites] : callMap)
  {
    if (calleeCallSites.empty())
    {
      // Can't do anything with no call sites.
      continue;
    }

    // Generate ranges of existing callee-name-tagged instructions.
    auto combinedCallSites = jvs::map_combined_call_sites(*caller);

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
        workLists.CallSites.insert(callSites.front());
        continue;
      }

      // Find any existing combined call blocks for the callee.
      auto existingSiteIter =
        combinedCallSites.find(callee->getName());
      if (existingSiteIter != combinedCallSites.end())
      {
        // Found an existing combined call. Combine the current call site into
        // it.
        auto& combinedCallSite = existingSiteIter->second.front();
        for (llvm::CallInst* callSite : callSites)
        {
          combinedCallSite.combine_call(*callSite);
        }

        workLists.ModifiedCallers.insert(caller);
        continue;
      }

      std::vector<llvm::Value*> args{};      
      llvm::MapVector<llvm::BasicBlock*, llvm::BasicBlock*> callSiteRet{};
      llvm::DenseMap<llvm::Instruction*, llvm::BasicBlock*> callSiteOrigParent{};
      llvm::CallInst* combinedCallInst{nullptr};
      llvm::BasicBlock* callBlock =
        llvm::BasicBlock::Create(caller->getContext(), ".fuse.callblock",
          caller);
      workLists.ModifiedCallers.insert(caller);

      for (llvm::CallInst* callSite : callSites)
      {
        auto* parentBlock = callSite->getParent();
        auto* retBlock =
          parentBlock->splitBasicBlock(callSite->getNextNode(),
            ".fuse.retblock");
        callSiteOrigParent[callSite] = parentBlock;
        callSiteRet[parentBlock] = retBlock;

        // Move the call instruction to the safe beginning of the return 
        // block.
        callSite->moveBefore(retBlock->getFirstNonPHI());

        // Generate a branch to the call block and get rid of the branch 
        // generated by splitBasicBlock().
        auto* oldBranch = parentBlock->getTerminator();
        llvm::BranchInst::Create(callBlock, parentBlock);
        oldBranch->eraseFromParent();
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
          // Attach the original function name and the argument index to the PHI 
          // node.
          jvs::attach_metadata(*argNode, jvs::FuseFunctionName,
            callee->getName());
          jvs::attach_metadata(*argNode, jvs::FuseFunctionArgIdx,
            llvm::StringRef(reinterpret_cast<char*>(&argCount),
              sizeof(argCount)));
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
      combinedCallInst =
        llvm::CallInst::Create(callee, llvm::makeArrayRef(args), "", callBlock);
      // Create a return value store if the callee doesn't return `void`. This
      // should always be done even if the call ignores the return value, as
      // other calls may *not* ignore it. After inlining, it's extremely 
      // difficult to tell which value is supposed to be the the return value,
      // so storing the *known* return value here simplifies the process. We can
      // remove it after all inlining is finished if the store turns out to be
      // dead.
      if (!callee->getReturnType()->isVoidTy())
      {
        auto allocaRetInst = new llvm::AllocaInst(callee->getReturnType(),
          callee->getParent()->getDataLayout().getAllocaAddrSpace(),
          ".fuse.return.buffer", 
          &*callBlock->getParent()->getEntryBlock().begin());
        auto storeRetInst = new llvm::StoreInst(combinedCallInst, allocaRetInst,
          combinedCallInst);
        storeRetInst->moveAfter(combinedCallInst);
        jvs::attach_metadata(jvs::FuseFunctionRet, callee->getName(),
          *allocaRetInst, *storeRetInst);
      }

      std::size_t callCount{0};
      for (llvm::CallInst* callInst : callSites)
      {
        // Remove the original call and replace all its references to the
        // combined call.
        callInst->replaceAllUsesWith(combinedCallInst);
        callInst->eraseFromParent();
        ++callCount;
      }

      // Create PHI nodes and switch instructions for branching back to the 
      // return blocks.
      auto* fromNode = llvm::PHINode::Create(
        llvm::Type::getInt32Ty(caller->getContext()), callCount, "",
        combinedCallInst);
      // Attach the name of the function to the PHI node.
      jvs::attach_metadata(*fromNode, jvs::FuseFunctionName, callee->getName());
      // Add a marker for the beggining of the function.
      jvs::create_metadata(*combinedCallInst, jvs::FuseFunctionStart, 
        callee->getName());
      // Add a marker for the end of the function.
      jvs::create_metadata(*callBlock, jvs::FuseFunctionEnd, callee->getName());
      // Since we must have a default case, we point it at an unreachable block
      // (since all cases are being accounted for).
      auto* switchBack = llvm::SwitchInst::Create(fromNode,
        get_or_create_unreachable_block(*caller), callSiteRet.size(),
        callBlock);
      // Attach the name of the associated function.
      jvs::attach_metadata(*switchBack, jvs::FuseFunctionName,
        callee->getName());
      std::size_t switchCount{0};
      for (auto& [parentBlock, retBlock] : callSiteRet)
      {
        auto* branchIdx = llvm::ConstantInt::get(caller->getContext(),
          llvm::APInt(32, switchCount++, true));
        fromNode->addIncoming(branchIdx, parentBlock);
        switchBack->addCase(branchIdx, retBlock);
      }

      workLists.CallSites.insert(combinedCallInst);
    }
  }
  
  return workLists;
}

} // namespace 


jvs::FuseFunctionsPass::FuseFunctionsPass(bool ignoreNoInline /*= false*/)
  : IgnoreNoInline(ignoreNoInline)
{
}


llvm::PreservedAnalyses jvs::FuseFunctionsPass::run(llvm::Module& m,
  llvm::ModuleAnalysisManager& manager)
{
  // We lower invoke instructions to call instructions to be able to inline more
  // code.
  llvm::PreservedAnalyses result;
  std::string parseError{};
  std::tie(result, parseError) =
    run_pass_pipeline(m, "module(function(lowerinvoke,simplifycfg),mergefunc)");
  if (!parseError.empty())
  {
    llvm::errs() << "Error parsing passes: " << parseError << '\n';
    return result;
  }

  llvm::DenseSet<llvm::CallInst*> failedInlineCallSites{};
  llvm::SetVector<llvm::Function*> callTargets{};
  CallerCalleeCallSitesMap callMap =
    get_candidate_call_sites(m, IgnoreNoInline, failedInlineCallSites);
  auto combinedCalls = combine_calls(callMap);
  for (llvm::Function* f : combinedCalls.ModifiedCallers)
  {
    demote_registers(*f);
  }
  
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
      //
      //failedInlineCallSites.insert(callInst);
      ///*
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
      //*/
    }

    callMap =
      get_candidate_call_sites(m, IgnoreNoInline, failedInlineCallSites);
    // Clean up multiple combined call sites for the same callee (if any exist).
    bool shouldUpdateCallMap{false};
    llvm::SetVector<llvm::Function*> modifiedCombinedCallers{};
    for (auto& [caller, calleeCallSites] : callMap)
    {
      bool callerUpdated{false};
      auto combinedCallSites = jvs::map_combined_call_sites(*caller);
      for (auto& [calleeName, combinedSites] : combinedCallSites)
      {
        if (combinedSites.size() > 1)
        {
          auto& mainCombinedSite = combinedSites.front();
          auto it = std::next(combinedSites.begin());
          for (; it != combinedSites.end(); ++it)
          {
            callerUpdated |= mainCombinedSite.combine(*it);
            shouldUpdateCallMap |= callerUpdated;
          }
        }
      }

      if (callerUpdated)
      {
        modifiedCombinedCallers.insert(caller);
      }
    }

    if (!modifiedCombinedCallers.empty())
    {
      for (llvm::Function* f : modifiedCombinedCallers)
      {
        demote_registers(*f);
      }
    }

    if (shouldUpdateCallMap)
    {
      callMap =
        get_candidate_call_sites(m, IgnoreNoInline, failedInlineCallSites);
    }

    combinedCalls = combine_calls(callMap);
    if (!combinedCalls.ModifiedCallers.empty())
    {
      for (llvm::Function* f : combinedCalls.ModifiedCallers)
      {
        demote_registers(*f);
      }
    }
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

  llvm::PreservedAnalyses mem2regPA;
  std::tie(mem2regPA, parseError) = 
    //run_pass_pipeline(m, "module(function(adce,mem2reg))");
    run_pass_pipeline(m, "module(constmerge,globalopt,globaldce)");
  if (!parseError.empty())
  {
    llvm::errs() << parseError << '\n';
  }
  
  result.intersect(mem2regPA);
  return result;
}

