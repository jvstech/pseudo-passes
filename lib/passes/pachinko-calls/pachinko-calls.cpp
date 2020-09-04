#include "passes/pachinko-calls.h"

#include <cstddef>
#include <tuple>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

namespace
{

static constexpr char PluginName[] = "PachinkoCalls";

// Pass registration
static llvm::PassPluginLibraryInfo getPachinkoCallsPluginInfo()
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
        [](llvm::StringRef name, llvm::ModulePassManager& mpm,
          llvm::ArrayRef<llvm::PassBuilder::PipelineElement>)
        {
          if (name.equals("pachinko-calls"))
          {
            mpm.addPass(jvs::PachinkoCallsPass());
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
  return getPachinkoCallsPluginInfo();
}

namespace
{

static llvm::SwitchInst* create_px(llvm::Module& m)
{
  auto& ctx = m.getContext();
  auto* voidPtrTy = llvm::IntegerType::get(ctx, 8)->getPointerTo();
  auto* sizeTy = m.getDataLayout().getIntPtrType(ctx);
  auto* pxFuncTy = llvm::FunctionType::get(voidPtrTy, {sizeTy}, false);
  auto* pxFunc = llvm::Function::Create(pxFuncTy, 
    llvm::GlobalValue::LinkageTypes::InternalLinkage, "prize_exchange", m);
  auto* entryBlock = llvm::BasicBlock::Create(ctx, "", pxFunc);
  auto* defaultBlock = llvm::BasicBlock::Create(ctx, "", pxFunc);
  new llvm::UnreachableInst(ctx, defaultBlock);
  auto* switchInst = 
    llvm::SwitchInst::Create(pxFunc->getArg(0), defaultBlock, 0, entryBlock);
  return switchInst;
}

static llvm::SwitchInst* get_or_create_px(llvm::Module& m)
{
  static constexpr char PxFuncNode[] = "prize.exchange";
  llvm::SwitchInst* pxSwitch{nullptr};
  auto pxNode = m.getNamedMetadata(PxFuncNode);
  if (!pxNode)
  {
    pxNode = m.getOrInsertNamedMetadata(PxFuncNode);
    pxSwitch = create_px(m);
    pxNode->addOperand(llvm::MDNode::get(m.getContext(), 
      llvm::ValueAsMetadata::get(pxSwitch->getFunction())));
    return pxSwitch;
  }

  if (pxNode->getNumOperands() == 0)
  {
    return nullptr;
  }

  if (auto funcMD = 
    llvm::dyn_cast<llvm::ValueAsMetadata>(pxNode->getOperand(0)))
  {
    pxSwitch = llvm::cast<llvm::SwitchInst>(
      &*llvm::cast<llvm::Function>(funcMD->getValue())->front().begin());
  }

  return pxSwitch;
}

} // namespace 


llvm::PreservedAnalyses jvs::PachinkoCallsPass::run(llvm::Module& m, 
  llvm::ModuleAnalysisManager& manager)
{
  using CallPrizeMap =
    llvm::DenseMap<llvm::CallBase*,
    std::tuple<llvm::CallBase*, llvm::ConstantInt*, llvm::FunctionType*>>;
  llvm::SetVector<llvm::Function*> mappedFuncs{};
  CallPrizeMap callPrizeMap{};
  llvm::SwitchInst* pxSwitch = get_or_create_px(m);
  llvm::Function* pxFunc = pxSwitch->getFunction();
  auto& ctx = m.getContext();
  auto* voidPtrTy = llvm::IntegerType::get(ctx, 8)->getPointerTo();
  auto* sizeTy = m.getDataLayout().getIntPtrType(ctx);
  
  for (llvm::Function& f : m)
  {
    if (f.isDeclaration() || f.hasFnAttribute(llvm::Attribute::OptimizeNone))
    {
      continue;
    }

    if (&f == pxFunc)
    {
      continue;
    }

    for (llvm::BasicBlock& block : f)
    {
      //if (block.isEHPad())
      //{
      //  continue;
      //}

      for (llvm::Instruction& inst : block)
      {
        if (auto callInst = llvm::dyn_cast<llvm::CallBase>(&inst);
          callInst && callInst->getCalledFunction() && 
          !callInst->isInlineAsm() && 
          !callInst->getCalledFunction()->isIntrinsic() &&
          (callInst->getCalledFunction() != &f))
        {
          llvm::Function* callee = callInst->getCalledFunction();
          auto* calleeType = callee->getFunctionType();
          if (mappedFuncs.insert(callInst->getCalledFunction()))
          {
            llvm::ConstantInt* prizeId = llvm::ConstantInt::get(sizeTy, 
              static_cast<std::uint64_t>(mappedFuncs.size() - 1));
            auto funcBlock = llvm::BasicBlock::Create(ctx, "", pxFunc);
            pxSwitch->addCase(prizeId, funcBlock);
            auto funcPtrCast = llvm::CastInst::Create(
              llvm::Instruction::BitCast, callee, voidPtrTy, "", funcBlock);
            llvm::ReturnInst::Create(ctx, funcPtrCast, funcBlock);
            callPrizeMap.try_emplace(callInst, std::make_tuple(callInst,
              prizeId, calleeType));
          }
        }
      }
    }
  }

  for (auto& callPrizePair : callPrizeMap)
  {
    auto& [callInst, prizeId, calleeType] = callPrizePair.second;
    auto* prizeCall = llvm::CallInst::Create(pxFunc, {prizeId}, "", callInst);
    auto funcPtrCast = llvm::CastInst::Create(llvm::Instruction::BitCast,
      prizeCall, calleeType->getPointerTo(), "", callInst);
    callInst->setCalledOperand(funcPtrCast);
  }

  return llvm::PreservedAnalyses::none();
}
