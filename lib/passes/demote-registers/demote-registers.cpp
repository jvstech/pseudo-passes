#include "passes/demote-registers.h"

#include <cassert>
#include <list>

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "support/value-util.h"

namespace
{

static bool valueEscapes(const llvm::Instruction* inst)
{
  const llvm::BasicBlock* bb = inst->getParent();
  for (const llvm::User* u : inst->users())
  {
    const llvm::Instruction* userInst = llvm::cast<llvm::Instruction>(u);
    if (userInst->getParent() != bb || llvm::isa<llvm::PHINode>(userInst))
    {
      return true;
    }
  }

  return false;
}

static llvm::AllocaInst* demote_immediate(llvm::Use& operand,
  llvm::Instruction* insertPt)
{
  llvm::Value* opVal = operand.get();
  if (!llvm::isa<llvm::Instruction>(opVal) &&
    jvs::is_any<
      llvm::ConstantFP, 
      llvm::ConstantInt
    >(opVal))
  {
    llvm::Instruction* user = llvm::cast<llvm::Instruction>(operand.getUser());
    if (!user->isEHPad() && !user->isAtomic() &&
      !jvs::is_any<
        llvm::AllocaInst,
        llvm::IntrinsicInst,
        llvm::GetElementPtrInst,
        llvm::SwitchInst
      >(user))
    {
      llvm::Constant* constantVal = llvm::cast<llvm::Constant>(opVal);
      auto immAlloca = new llvm::AllocaInst(opVal->getType(), 0,
        user->getName() + ".imm2mem." + std::to_string(operand.getOperandNo()), 
        insertPt);
      auto immStore = new llvm::StoreInst(constantVal, immAlloca, insertPt);
      immStore->moveAfter(insertPt);
      
      // Create a load instruction for the global operand.
      llvm::LoadInst* loadInst{nullptr};
      if (auto phi = llvm::dyn_cast<llvm::PHINode>(user))
      {
        // Insert the load into the predecessor block.
        loadInst = new llvm::LoadInst(opVal->getType(), immAlloca, "",
          phi->getIncomingBlock(operand.getOperandNo())->getTerminator());
        phi->setIncomingValue(operand.getOperandNo(), loadInst);
      }
      else
      {
        loadInst = new llvm::LoadInst(opVal->getType(), immAlloca, "", user);
        user->setOperand(operand.getOperandNo(), loadInst);
      }
  
      return immAlloca;
    }
  }

  return nullptr;
}

} // namespace

#define DEBUG_TYPE "demote-registers"

STATISTIC(NumRegsDemoted, "Number of registers demoted");
STATISTIC(NumPhisDemoted, "Number of phi-nodes demoted");


llvm::PreservedAnalyses jvs::DemoteRegistersPass::run(llvm::Function& f,
  llvm::FunctionAnalysisManager& manager)
{
  if (f.isDeclaration() || f.hasFnAttribute(llvm::Attribute::OptimizeNone))
  {
    return llvm::PreservedAnalyses::all();
  }

  auto demoted = jvs::DemoteRegisters(f);
  if (!demoted.DemotedInstructionCount && 
    !demoted.DemotedPhiNodeCount && demoted.Allocas.empty())
  {
    return llvm::PreservedAnalyses::none();
  }

  NumRegsDemoted = demoted.DemotedInstructionCount;
  NumPhisDemoted = demoted.DemotedPhiNodeCount;

  llvm::PreservedAnalyses preservedAnalyses{};
  preservedAnalyses.preserveSet<llvm::CFGAnalyses>();
  return preservedAnalyses;
}

jvs::DemotedInstructions jvs::DemoteRegisters(llvm::Function& f,
  bool demoteOperands /*= false*/)
{
  std::size_t demotedInstCount{0};
  std::size_t demotedPhiNodes{0};
  std::vector<llvm::AllocaInst*> generatedAllocas{};

  // Insert all new allocas into the entry block.
  llvm::BasicBlock* bbEntry = &f.getEntryBlock();
  assert(pred_empty(bbEntry) &&
    "Entry block to function must not have predecessors!");

  // Find the first non-alloca instruction and create an insertion point. This 
  // is safe if the block is well-formed: it will always have a terminator --
  // otherwise we'll trigger an assertion.
  llvm::BasicBlock::iterator it = bbEntry->begin();
  for (; llvm::isa<llvm::AllocaInst>(it); ++it)
  {
  }

  llvm::CastInst* allocaInsertionPoint = new llvm::BitCastInst(
    llvm::Constant::getNullValue(llvm::Type::getInt32Ty(f.getContext())),
    llvm::Type::getInt32Ty(f.getContext()), "reg2mem alloca point", &*it);

  std::list<llvm::Instruction*> workList;

  // Demote immediate operands (if requested).
  if (demoteOperands)
  {
    for (llvm::BasicBlock& block : f)
    {
      for (llvm::BasicBlock::iterator iib = block.begin(), iie = block.end();
        iib != iie; ++iib)
      {
        if (!llvm::isa<llvm::AllocaInst>(iib) && !iib->isEHPad())
        {
          workList.push_front(&*iib);
        }
      }
    }
  
    for (llvm::Instruction* ilb : workList)
    {
      for (llvm::Use& operand : ilb->operands())
      {
        auto immAlloca = demote_immediate(operand, allocaInsertionPoint);
        if (immAlloca)
        {
          generatedAllocas.push_back(immAlloca);
        }
      }
    }

    workList.clear();
  }

  // Find the escaped instructions but don't create stack slots for
  // allocas in the entry block.
  for (llvm::BasicBlock& ibb : f)
  {
    for (llvm::BasicBlock::iterator iib = ibb.begin(), iie = ibb.end();
      iib != iie; ++iib)
    {
      if (!(llvm::isa<llvm::AllocaInst>(iib) && iib->getParent() == bbEntry) &&
        valueEscapes(&*iib) && !iib->isEHPad())
      {
        workList.push_front(&*iib);
      }
    }
  }

  // Demote escaped instructions.
  demotedInstCount += workList.size();
  for (llvm::Instruction* ilb : workList)
  {
    generatedAllocas.push_back(
      llvm::DemoteRegToStack(*ilb, false, allocaInsertionPoint));
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
        workList.push_front(&*iib);
      }
    }
  }

  // Demote phi nodes.
  demotedPhiNodes += workList.size();
  for (llvm::Instruction* ilb : workList)
  {
    generatedAllocas.push_back(llvm::DemotePHIToStack(
      llvm::cast<llvm::PHINode>(ilb), allocaInsertionPoint));
  }

  DemotedInstructions demoted(demotedInstCount, demotedPhiNodes, 
    std::move(generatedAllocas));
  return demoted;
}
