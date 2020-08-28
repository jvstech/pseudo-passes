#include "fuse-functions/fuse-functions.h"

#include <cassert>
#include <list>

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/Local.h"

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

  // Insert all new allocas into entry block.
  llvm::BasicBlock* bbEntry = &f.getEntryBlock();
  assert(pred_empty(bbEntry) &&
    "Entry block to function must not have predecessors!");

  // Find first non-alloca instruction and create insertion point. This is
  // safe if block is well-formed: it always have terminator, otherwise
  // we'll get and assertion.
  llvm::BasicBlock::iterator it = bbEntry->begin();
  for (; llvm::isa<llvm::AllocaInst>(it); ++it)
  {
  }
    

  llvm::CastInst* allocaInsertionPoint = new llvm::BitCastInst(
    llvm::Constant::getNullValue(llvm::Type::getInt32Ty(f.getContext())),
    llvm::Type::getInt32Ty(f.getContext()), "reg2mem alloca point", &*it);

  // Find the escaped instructions. But don't create stack slots for
  // allocas in entry block.
  std::list<llvm::Instruction*> workList;
  for (llvm::BasicBlock& ibb : f)
  {
    for (llvm::BasicBlock::iterator iib = ibb.begin(), iie = ibb.end(); 
      iib != iie; ++iib)
    {
      if (!(llvm::isa<llvm::AllocaInst>(iib) && iib->getParent() == bbEntry) &&
        valueEscapes(&*iib))
      {
        workList.push_front(&*iib);
      }
    }
  }

  // Demote escaped instructions
  NumRegsDemoted += workList.size();
  for (llvm::Instruction* ilb : workList)
    llvm::DemoteRegToStack(*ilb, false, allocaInsertionPoint);

  workList.clear();

  // Find all phi's
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

  // Demote phi nodes
  NumPhisDemoted += workList.size();
  for (llvm::Instruction* ilb : workList)
  {
    llvm::DemotePHIToStack(
      llvm::cast<llvm::PHINode>(ilb), allocaInsertionPoint);
  }

  return llvm::PreservedAnalyses::none();
}
