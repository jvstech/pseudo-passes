#include "passes/resize-malloc.h"

#include <cstddef>
#include <optional>
#include <tuple>

#include "llvm/ADT/SmallString.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
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
#include "llvm/Transforms/Scalar/ADCE.h"
#include "llvm/Transforms/Scalar/SCCP.h"
#include "support/type-util.h"
#include "support/value-util.h"

namespace
{

static constexpr char PluginName[] = "ResizeMalloc";

// Pass registration
static llvm::PassPluginLibraryInfo getResizeMallocPluginInfo()
{
  return
  {
    LLVM_PLUGIN_API_VERSION,
    PluginName,
    LLVM_VERSION_STRING,
    [](llvm::PassBuilder& passBuilder)
    {
      // Function passes
      passBuilder.registerAnalysisRegistrationCallback(
        [&](llvm::FunctionAnalysisManager& fam)
        {
          fam.registerPass([] { return llvm::TargetLibraryAnalysis(); });
          fam.registerPass([] { return llvm::DominatorTreeAnalysis(); });
          fam.registerPass([] { return llvm::PassInstrumentationAnalysis(); });
          fam.registerPass([] { return llvm::PostDominatorTreeAnalysis(); });
        });

      passBuilder.registerPipelineParsingCallback(
        [&](llvm::StringRef name, llvm::FunctionPassManager& fpm,
          llvm::ArrayRef<llvm::PassBuilder::PipelineElement>)
        {
          if (name.equals("resize-malloc"))
          {
            fpm.addPass(llvm::SCCPPass());
            fpm.addPass(llvm::ADCEPass());
            fpm.addPass(jvs::ResizeMallocPass());
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
  return getResizeMallocPluginInfo();
}

namespace
{

enum class MemAllocFunctionId
{
  None,
  Malloc,
  Calloc,
  Mmap,
  WindowsVirtualAlloc,
  WindowsVirtualAllocEx,
  WindowsVirtualAllocExNuma,
  WindowsHeapAlloc,
  WindowsCoTaskMemAlloc,
  WindowsGlobalAlloc,
  WindowsLocalAlloc,
  SystemVNew,
  ItaniumNew
};

using MemAllocInfo = 
  std::tuple<MemAllocFunctionId, unsigned int, llvm::CallBase*>;

static MemAllocFunctionId is_mem_alloc(llvm::CallBase& callSite)
{
  llvm::Function* callee = callSite.getCalledFunction();
  if (!callee)
  {
    return MemAllocFunctionId::None;
  }

  if (!callee->hasExternalLinkage())
  {
    return MemAllocFunctionId::None;
  }

  llvm::FunctionType* calleeType = callee->getFunctionType();
  llvm::Module& m = *callSite.getModule();
  auto* mallocFuncType = 
    jvs::create_type<void*(jvs::ir_types::Size)>(m);
  if (calleeType == mallocFuncType)
  {
    if (callee->getName().equals("malloc"))
    {
      return MemAllocFunctionId::Malloc;
    }

    if (callee->getName().equals("CoTaskMemAlloc"))
    {
      return MemAllocFunctionId::WindowsCoTaskMemAlloc;
    }

    if (callee->getName().equals("_Znwm"))
    {
      return MemAllocFunctionId::SystemVNew;
    }

    if (callee->getName().equals("??2@YAPEAX_K@Z"))
    {
      return MemAllocFunctionId::ItaniumNew;
    }
  }

  auto* callocFuncType =
    jvs::create_type<void*(jvs::ir_types::Size, jvs::ir_types::Size)>(m);
  if (callee->getName().equals("calloc") && calleeType == callocFuncType)
  {
    return MemAllocFunctionId::Calloc;
  }

  auto* mmapFuncType = jvs::create_type<
    void*(void*, jvs::ir_types::Size, jvs::ir_types::Int<32>, 
      jvs::ir_types::Int<32>, jvs::ir_types::Int<32>, jvs::ir_types::Size)>(m);
  if (callee->getName().equals("mmap") && calleeType == mmapFuncType)
  {
    return MemAllocFunctionId::Mmap;
  }

  auto virtAllocFuncType = jvs::create_type<
    void*(void*, jvs::ir_types::Size, jvs::ir_types::Int<32>, 
      jvs::ir_types::Int<32>)>(m);
  if (callee->getName().equals("VirtualAlloc") &&
    calleeType == virtAllocFuncType)
  {
    return MemAllocFunctionId::WindowsVirtualAlloc;
  }

  auto virtAllocExFuncType = jvs::create_type<
    void*(void*, void*, jvs::ir_types::Size, jvs::ir_types::Int<32>,
      jvs::ir_types::Int<32>)>(m);
  if (callee->getName().equals("VirtualAllocEx") &&
    calleeType == virtAllocExFuncType)
  {
    return MemAllocFunctionId::WindowsVirtualAllocEx;
  }

  auto virtAllocExNumaFuncType = jvs::create_type<
    void* (void*, jvs::ir_types::Size, jvs::ir_types::Int<32>,
      jvs::ir_types::Int<32>, jvs::ir_types::Int<32>)>(m);
  if (callee->getName().equals("VirtualAllocExNuma") && 
    calleeType == virtAllocExNumaFuncType)
  {
    return MemAllocFunctionId::WindowsVirtualAllocExNuma;
  }

  auto heapAllocFuncType = jvs::create_type<
    void*(void*, jvs::ir_types::Int<32>, jvs::ir_types::Size)>(m);
  if (callee->getName().equals("HeapAlloc") && calleeType == heapAllocFuncType)
  {
    return MemAllocFunctionId::WindowsHeapAlloc;
  }

  auto globalLocalFuncType = 
    jvs::create_type<void*(void*, jvs::ir_types::Size)>(m);
  if (calleeType == globalLocalFuncType)
  {
    if (callee->getName().equals("GlobalAlloc"))
    {
      return MemAllocFunctionId::WindowsGlobalAlloc;
    }

    if (callee->getName().equals("LocalAlloc"))
    {
      return MemAllocFunctionId::WindowsLocalAlloc;
    }
  }

  return MemAllocFunctionId::None;
}

static MemAllocInfo get_size_arg(llvm::CallBase& callInst) noexcept
{
  auto memCall = is_mem_alloc(callInst);
  
  switch (memCall)
  {
  case MemAllocFunctionId::None:
    return std::make_tuple(
      memCall, ~static_cast<unsigned int>(0), &callInst);

  case MemAllocFunctionId::Malloc:
  case MemAllocFunctionId::ItaniumNew:
  case MemAllocFunctionId::SystemVNew:
  case MemAllocFunctionId::WindowsCoTaskMemAlloc:
    return std::make_tuple(memCall, 0U, &callInst);

  case MemAllocFunctionId::Calloc:
  case MemAllocFunctionId::Mmap:
  case MemAllocFunctionId::WindowsLocalAlloc:
  case MemAllocFunctionId::WindowsGlobalAlloc:
  case MemAllocFunctionId::WindowsVirtualAlloc:
    return std::make_tuple(memCall, 1U, &callInst);

  case MemAllocFunctionId::WindowsVirtualAllocEx:
  case MemAllocFunctionId::WindowsVirtualAllocExNuma:
  case MemAllocFunctionId::WindowsHeapAlloc:
    return std::make_tuple(memCall, 2U, &callInst);
  }
}

} // namespace


llvm::PreservedAnalyses jvs::ResizeMallocPass::run(llvm::Function& f, 
  llvm::FunctionAnalysisManager& manager)
{
  // Skip functions marked [[optnone]] and declarations.
  if (f.isDeclaration() || f.hasFnAttribute(llvm::Attribute::OptimizeNone))
  {
    return llvm::PreservedAnalyses::all();
  }

  std::vector<MemAllocInfo> memAllocCalls{};
  for (llvm::Instruction& inst : llvm::instructions(f))
  {
    if (auto* callInst = llvm::dyn_cast<llvm::CallInst>(&inst))
    {
      auto memCallTup = get_size_arg(*callInst);
      if (std::get<0>(memCallTup) != MemAllocFunctionId::None)
      {
        memAllocCalls.push_back(std::move(memCallTup));
      }
    }
  }

  for (auto& [memCall, arg, callInst] : memAllocCalls)
  {
    std::uint64_t memSize{0};
    if (memCall == MemAllocFunctionId::Calloc)
    {
      // Special handling for calloc() since it uses two arguments.
      auto elemSize = get_int_constant(callInst->getArgOperand(0));
      if (elemSize)
      {
        auto elemCount = get_int_constant(callInst->getArgOperand(arg));
        if (elemCount)
        {
          memSize = *elemSize * *elemCount;
        }
      }
    }
    else
    {
      auto sizeConst = get_int_constant(callInst->getArgOperand(arg));
      if (sizeConst)
      {
        memSize = *sizeConst;
      }
    }

    if (memSize > 0)
    {
      // Adjust the memory size to be a multiple of 8KB.
      memSize += (0x2000 - (memSize % 0x2000));
      auto memSizeConst = llvm::ConstantInt::get(
        callInst->getArgOperand(arg)->getType(), memSize);
      if (memCall == MemAllocFunctionId::Calloc)
      {
        auto oneConst = 
          llvm::ConstantInt::get(callInst->getArgOperand(0)->getType(), 1);
        callInst->setArgOperand(0, oneConst);
      }

      callInst->setArgOperand(arg, memSizeConst);
    }
  }

  llvm::PreservedAnalyses preservedAnalyses{};
  preservedAnalyses.preserveSet<llvm::CFGAnalyses>();
  return preservedAnalyses;
}
