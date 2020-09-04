#include "passes/function-name-trace.h"

#include <string>

#include "llvm/ADT/DenseMap.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "support/type-util.h"
#include "support/value-util.h"

namespace
{

static constexpr char PluginName[] = "FunctionNameTrace";

// Pass registration
static llvm::PassPluginLibraryInfo getFunctionNameTracePluginInfo()
{
  return
  {
    LLVM_PLUGIN_API_VERSION,
    PluginName,
    LLVM_VERSION_STRING,
    [](llvm::PassBuilder& passBuilder)
    {
      passBuilder.registerPipelineParsingCallback(
        [&](llvm::StringRef name, llvm::ModulePassManager& mpm,
          llvm::ArrayRef<llvm::PassBuilder::PipelineElement>)
        {
          if (name.equals("function-name-trace"))
          {
            mpm.addPass(jvs::FunctionNameTracePass());
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
  return getFunctionNameTracePluginInfo();
}

namespace
{
//!
//! Gets an llvm::FunctionCallee for the puts() function.
//!
//! @param [in,out] m
//!   the llvm::Module to process.
//!
//! @returns
//!   The puts() llvm::FunctionCallee.
//!
static llvm::FunctionCallee get_puts(llvm::Module& m) noexcept
{
  auto* putsType = 
    jvs::create_type<jvs::ir_types::Int<32>(jvs::ir_types::Int<8>*)>(m);
  auto putsCallee = m.getOrInsertFunction("puts", putsType);
  llvm::cast<llvm::Function>(putsCallee.getCallee())->setDSOLocal(true);
  return putsCallee;
}

} // namespace 


llvm::PreservedAnalyses jvs::FunctionNameTracePass::run(llvm::Module& m, 
  llvm::ModuleAnalysisManager& manager)
{
  llvm::DenseMap<llvm::Function*, llvm::GlobalVariable*> enteringMap{};
  llvm::DenseMap<llvm::Function*, llvm::GlobalVariable*> leavingMap{};
  // Get a FunctionCallee that refers to the "puts" function.
  auto putsCallee = get_puts(m);
  if (!putsCallee)
  {
    // WEIRD.
    m.getContext().emitError("puts() function wasn't found.");
    LLVM_BUILTIN_UNREACHABLE;
  }

  auto putsFunc = llvm::cast<llvm::Function>(putsCallee.getCallee());
  llvm::IRBuilder<> builder(m.getContext());
  for (llvm::Function& f : m)
  {
    if (f.empty() || f.hasFnAttribute(llvm::Attribute::OptimizeNone))
    {
      continue;
    }

    if (auto* putsFunc = llvm::dyn_cast<llvm::Function>(putsCallee.getCallee()))
    {
      if (putsFunc == &f)
      {
        continue;
      }
    }

    // Make sure the entry block for this function is arranged properly.
    auto insertPt = llvm::PrepareToSplitEntryBlock(f.getEntryBlock(),
      f.getEntryBlock().begin());

    auto zeroConst = 
      llvm::ConstantInt::get(jvs::create_type<jvs::ir_types::Int<64>>(m), 
        static_cast<std::uint64_t>(0));
    llvm::Constant* enteringConst{nullptr};
    llvm::GlobalVariable* enteringVar{nullptr};
    auto funcNameIter = enteringMap.find(&f);
    if (funcNameIter != enteringMap.end())
    {
      enteringVar = funcNameIter->second;
    }
    else
    {
      // Generate a string for this entering point.
      std::string enterName = llvm::formatv(
        "\n[>] Entering {0}\n", llvm::demangle(f.getName().str()));
      enteringConst =
        jvs::create_string_constant(m, llvm::StringRef(enterName));
      enteringVar = new llvm::GlobalVariable(m, enteringConst->getType(), 
        true, llvm::GlobalValue::LinkageTypes::LinkOnceODRLinkage, 
        enteringConst, enterName);
      enteringVar->setDSOLocal(true);
      enteringVar->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
      auto [insertIter, wasInserted] = enteringMap.try_emplace(&f, enteringVar);
    }

    // Create the entry function call.
    builder.SetInsertPoint(&*insertPt);
    builder.CreateCall(putsCallee,
      builder.CreateInBoundsGEP(enteringVar, {zeroConst, zeroConst}));

    // Create the exit points string (if it doesn't exist).
    llvm::Constant* exitingConst{nullptr};
    llvm::GlobalVariable* exitingVar{nullptr};
    funcNameIter = leavingMap.find(&f);
    if (funcNameIter != leavingMap.end())
    {
      exitingVar = funcNameIter->second;
    }
    else
    {
      // Generate a string for this entering point.
      std::string exitName =
        llvm::formatv("\n[<] Leaving {0}\n", llvm::demangle(f.getName().str()));
      exitingConst = jvs::create_string_constant(m, llvm::StringRef(exitName));
      exitingVar = new llvm::GlobalVariable(m, exitingConst->getType(),
        true, llvm::GlobalVariable::LinkageTypes::LinkOnceODRLinkage, 
        exitingConst, exitName);
      exitingVar->setDSOLocal(true);
      exitingVar->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
      auto [insertIter, wasInserted] = enteringMap.try_emplace(&f, exitingVar);
    }

    for (llvm::Instruction& inst : llvm::instructions(f))
    {
      if (auto* retInst = llvm::dyn_cast<llvm::ReturnInst>(&inst))
      {
        // Create an exit function call.
        builder.SetInsertPoint(retInst);
        builder.CreateCall(putsCallee,
          builder.CreateInBoundsGEP(exitingVar, {zeroConst, zeroConst}));
      }
    }
  }

  return llvm::PreservedAnalyses::none();
}
