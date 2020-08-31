#include "combined-call-site.h"

#include <cassert>
#include <iterator>
#include <utility>

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "support/metadata-util.h"

std::vector<jvs::CombinedCallSite> jvs::find_combined_call_sites(
  llvm::Function& caller)
{
  std::vector<jvs::CombinedCallSite> combinedCallSites{};
  // Generate ranges of existing callee-name-tagged instructions.
  auto funcNameInstRange = jvs::find_metadata(caller, jvs::FuseFunctionName);
  std::vector<llvm::Instruction*> funcNameInsts(funcNameInstRange.begin(),
    funcNameInstRange.end());

  std::vector<llvm::SwitchInst*> switchInsts{};
  std::vector<llvm::LoadInst*> loadInsts{};
  for (llvm::Instruction* inst : funcNameInsts)
  {
    if (auto switchInst = llvm::dyn_cast<llvm::SwitchInst>(inst))
    {
      switchInsts.push_back(switchInst);
    }
    else if (auto loadInst = llvm::dyn_cast<llvm::LoadInst>(inst))
    {
      loadInsts.push_back(loadInst);
    }
  }

  for (llvm::SwitchInst* switchInst : switchInsts)
  {
    // Found a return switch for a previously combined call.
    auto calleeName = *jvs::get_metadata(*switchInst, FuseFunctionName);
    llvm::LoadInst* condLoad =
      llvm::cast<llvm::LoadInst>(switchInst->getCondition());
    CombinedCallSite combinedCallElement(caller, calleeName, *switchInst, 
      *condLoad);
    combinedCallSites.push_back(std::move(combinedCallElement));
  }

  return combinedCallSites;
}

auto jvs::map_combined_call_sites(llvm::Function& caller) noexcept
  -> llvm::DenseMap<llvm::StringRef, std::vector<jvs::CombinedCallSite>>
{
  llvm::DenseMap<llvm::StringRef, std::vector<jvs::CombinedCallSite>> results{};
  auto combinedSites = find_combined_call_sites(caller);
  while (!combinedSites.empty())
  {
    auto combinedSite = std::move(combinedSites.back());
    combinedSites.pop_back();
    auto calleeName = combinedSite.function_name();
    results[calleeName].push_back(std::move(combinedSite));
  }

  return results;
}

jvs::CombinedCallSite::CombinedCallSite(llvm::Function& caller,
  llvm::StringRef calleeName, llvm::SwitchInst& returnSwitch, 
  llvm::LoadInst& parentBlockIdLoad)
  : caller_(&caller),
  callee_name_(calleeName),
  return_switch_(&returnSwitch),
  parent_block_id_load_(&parentBlockIdLoad)
{
}

llvm::Function& jvs::CombinedCallSite::caller() const noexcept
{
  return *caller_;
}

std::vector<std::uint64_t> jvs::CombinedCallSite::get_block_ids() const noexcept
{
  std::vector<std::uint64_t> blockIds{};
  blockIds.reserve(return_switch_->getNumCases());
  llvm::transform(return_switch_->cases(), std::back_inserter(blockIds),
    [](const llvm::SwitchInst::CaseHandle& c)
    {
      return c.getCaseValue()->getZExtValue();
    });
  return blockIds;
}

jvs::IdStoreMap jvs::CombinedCallSite::get_block_id_stores() const noexcept
{
  IdStoreMap idStoreMap{};
  auto condAlloca =
    llvm::cast<llvm::Instruction>(parent_block_id_load_->getPointerOperand());
  for (auto user : condAlloca->users())
  {
    if (auto storeInst = llvm::dyn_cast<llvm::StoreInst>(user))
    {
      auto storeBlock = storeInst->getParent();
      auto predRange = llvm::predecessors(parent_block_id_load_->getParent());
      if (llvm::is_contained(predRange, storeBlock))
      {
        if (auto idConstant =
          llvm::dyn_cast<llvm::ConstantInt>(storeInst->getValueOperand()))
        {
          // Found a predecessor that sets its branch ID.
          idStoreMap.emplace(idConstant->getZExtValue(), storeInst);
        }
      }
    }
  }

  return idStoreMap;
}

llvm::AllocaInst* jvs::CombinedCallSite::get_block_id_pointer() const noexcept
{
  return llvm::dyn_cast<llvm::AllocaInst>(
    parent_block_id_load_->getPointerOperand());
}

std::uint64_t jvs::CombinedCallSite::get_max_block_id() const noexcept
{
  auto blockIds = get_block_ids();
  if (blockIds.empty())
  {
    return ~static_cast<std::uint64_t>(0);
  }

  return *std::max_element(blockIds.begin(), blockIds.end());
}

jvs::IdBlockMap jvs::CombinedCallSite::get_branching_blocks()
  const noexcept
{
  IdBlockMap idBranchBlockMap{};
  auto condAlloca = 
    llvm::cast<llvm::Instruction>(parent_block_id_load_->getPointerOperand());
  for (auto user : condAlloca->users())
  {
    if (auto storeInst = llvm::dyn_cast<llvm::StoreInst>(user))
    {
      auto storeBlock = storeInst->getParent();
      auto predRange = llvm::predecessors(parent_block_id_load_->getParent());
      if (llvm::is_contained(predRange, storeBlock))
      {
        if (auto idConstant = 
          llvm::dyn_cast<llvm::ConstantInt>(storeInst->getValueOperand()))
        {
          // Found a predecessor that sets its branch ID.
          idBranchBlockMap.emplace(idConstant->getZExtValue(), storeBlock);
        }
      }
    }
  }
  
  return idBranchBlockMap;
}

llvm::BasicBlock* jvs::CombinedCallSite::get_branching_block(
  std::uint64_t blockId) const noexcept
{
  auto branchBlockMap = get_branching_blocks();
  if (auto foundIt = branchBlockMap.find(blockId);
    foundIt != branchBlockMap.end())
  {
    return foundIt->second;
  }

  return nullptr;
}

jvs::IdBlockMap jvs::CombinedCallSite::get_return_blocks() const
  noexcept
{  
  auto idBlockRange = llvm::map_range(return_switch_->cases(),
    [](const llvm::SwitchInst::CaseHandle& c)
    {
      return std::make_pair(
        c.getCaseValue()->getZExtValue(), c.getCaseSuccessor());
    });
  IdBlockMap idReturnBlockMap{};
  idReturnBlockMap.insert(idBlockRange.begin(), idBlockRange.end());
  return idReturnBlockMap;
}

llvm::BasicBlock* jvs::CombinedCallSite::get_return_block(std::uint64_t blockId)
  const noexcept
{
  auto returnBlockMap = get_return_blocks();
  if (auto foundIt = returnBlockMap.find(blockId);
    foundIt != returnBlockMap.end())
  {
    return foundIt->second;
  }

  return nullptr;
}

jvs::ArgIdxAllocaMap jvs::CombinedCallSite::get_argument_pointers() const 
  noexcept
{
  ArgIdxAllocaMap argPtrMap{};
  llvm::Function* func = parent_block_id_load_->getFunction();
  for (llvm::Instruction* inst : 
    jvs::find_metadata(*func, jvs::FuseFunctionArgIdx))
  {
    if (auto funcNameBuffer = jvs::get_metadata(*inst, jvs::FuseFunctionName))
    {
      if (funcNameBuffer->equals(callee_name_))
      {
        auto argIdxStr = jvs::get_metadata(*inst, jvs::FuseFunctionArgIdx);
        // Bad aliasing, but meh...
        std::size_t argIdx =
          *reinterpret_cast<const std::size_t*>(argIdxStr->data());
        // If the block is well formed, all the argument index instructions
        // will be load instructions. That's all we're going to support 
        // anyway.
        llvm::LoadInst* argLoadInst = llvm::cast<llvm::LoadInst>(inst);
        argPtrMap[argIdx] = 
          llvm::cast<llvm::AllocaInst>(argLoadInst->getPointerOperand());
      }
    }
  }

  return argPtrMap;
}

llvm::AllocaInst* jvs::CombinedCallSite::get_argument_pointer(
  std::size_t argIdx) 
  const noexcept
{
  auto argPtrMap = get_argument_pointers();
  if (auto foundIt = argPtrMap.find(argIdx); foundIt != argPtrMap.end())
  {
    return foundIt->second;
  }

  return nullptr;
}

llvm::AllocaInst* jvs::CombinedCallSite::get_return_pointer() const noexcept
{
  llvm::Instruction* blockFront = &*return_switch_->getParent()->begin();
  auto prevNode = return_switch_->getPrevNode();
  while (prevNode != blockFront)
  {
    if (llvm::StoreInst* storeInst = llvm::dyn_cast<llvm::StoreInst>(prevNode))
    {
      auto retNameBuf = jvs::get_metadata(
        *llvm::cast<llvm::Instruction>(storeInst->getPointerOperand()),
        jvs::FuseFunctionRet);
      if (retNameBuf && retNameBuf->equals(callee_name_))
      {
        return llvm::cast<llvm::AllocaInst>(storeInst->getPointerOperand());
      }
    }

    prevNode = prevNode->getPrevNode();
  }

  return nullptr;
}

bool jvs::CombinedCallSite::combine_call(llvm::CallInst& callInst) noexcept
{
  if (callInst.getFunction() != caller_)
  {
    return false;
  }

  if (!callInst.getCalledFunction() ||
    callInst.getCalledFunction()->getName() != callee_name_)
  {
    return false;
  }

  auto argCount = callInst.getNumArgOperands();
  auto argPtrMap = get_argument_pointers();
  if (argPtrMap.size() != argCount)
  {
    return false;
  }

  // Create a new block ID.
  std::uint64_t blockId = get_max_block_id() + 1;
  auto blockIdConst = llvm::ConstantInt::get(llvm::IntegerType::get(
    callInst.getModule()->getContext(), 32), blockId);
  // Store the block ID in the appropriate allocation.
  new llvm::StoreInst(blockIdConst, parent_block_id_load_->getPointerOperand(),
    &callInst);
  // Store the call arguments (if any).
  if (!argPtrMap.empty())
  {
    for (unsigned int i = 0; i < argCount; ++i)
    {
      new llvm::StoreInst(callInst.getArgOperand(i),
        argPtrMap.at(i), &callInst);
    }
  }

  // Load the return value (if the call site expects it and if the
  // callee doesn't return void).
  llvm::LoadInst* loadRetInst{nullptr};
  if (!callInst.getFunctionType()->getReturnType()->isVoidTy())
  {
    if (auto retPtr = 
      llvm::dyn_cast_or_null<llvm::AllocaInst>(get_return_pointer()))
    {
      loadRetInst = new llvm::LoadInst(retPtr->getAllocatedType(),retPtr, "", 
        &callInst);
    }
  }

  // Split the block at the call site and replace the generated branch
  // instruction with one that points to the existing call block.
  auto parentBlock = callInst.getParent();
  auto retBlock =
    parentBlock->splitBasicBlock(callInst.getNextNode());
  auto* callBranch =
    llvm::cast<llvm::BranchInst>(parentBlock->getTerminator());
  callBranch->setSuccessor(0, retBlock);
  if (loadRetInst)
  {
    callInst.moveBefore(retBlock->getFirstNonPHI());
    loadRetInst->moveBefore(&callInst);
    callInst.replaceAllUsesWith(loadRetInst);
  }

  // We no longer need the existing call site.
  callInst.eraseFromParent();

  // Add a new switch case for the return block.
  return_switch_->addCase(blockIdConst, retBlock);
  return true;
}

bool jvs::CombinedCallSite::combine(CombinedCallSite& other) noexcept
{
  if (other.callee_name_ != callee_name_)
  {
    return false;
  }

  bool sameFunc = other.parent_block_id_load_->getFunction() == 
    parent_block_id_load_->getFunction();
  sameFunc &= other.return_switch_->getFunction() ==
    return_switch_->getFunction();
  if (!sameFunc)
  {
    return false;
  }

  // We have to adjust all the block IDs for the incoming CombinedCallSite so
  // there is no overlap with the current block IDs. Unfortunately, we can't
  // just change the value in the store instruction (as far as I can tell), so
  // we have to create a new one and erase the old one.
  // We also have to add cases to our return switch to handle the new block IDs
  // and update the successors of all the branching blocks to point to our
  // call block.
  std::uint64_t startBlockId = get_max_block_id() + 1;
  auto& ctx = return_switch_->getContext();
  for (auto& [blockId, blockIdStore] : other.get_block_id_stores())
  {
    std::uint64_t newBlockId = blockId + startBlockId;
    auto oldIdConstant = 
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), blockId);
    auto idConstant = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 
      newBlockId);
    new llvm::StoreInst(idConstant, blockIdStore->getPointerOperand(), 
      blockIdStore);
    llvm::BranchInst* blockBranch = llvm::cast<llvm::BranchInst>(
      blockIdStore->getParent()->getTerminator());
    blockBranch->setSuccessor(0, parent_block_id_load_->getParent());
    blockIdStore->eraseFromParent();
    return_switch_->addCase(idConstant, 
      other.return_switch_->findCaseValue(oldIdConstant)->getCaseSuccessor());
  }

  // Replace the switch instruction in the other call block with an 
  // unreachable instruction.
  new llvm::UnreachableInst(ctx, other.return_switch_->getParent());
  auto oldDefaultBlock = other.return_switch_->getDefaultDest();
  other.return_switch_->setDefaultDest(return_switch_->getDefaultDest());
  other.return_switch_->eraseFromParent();
  if (llvm::pred_empty(oldDefaultBlock))
  {
    llvm::DeleteDeadBlock(oldDefaultBlock);
  }

  if (llvm::pred_empty(other.parent_block_id_load_->getParent()))
  {
    llvm::DeleteDeadBlock(other.parent_block_id_load_->getParent());
  }

  return true;
}
