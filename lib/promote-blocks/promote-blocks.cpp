#include "promote-blocks/promote-blocks.h"

#include <algorithm>
#if defined(_MSC_VER) && \
  !defined(_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#endif
#include <iterator>
#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"

namespace
{

static constexpr char PassName[] = "promote-blocks";
static constexpr char PluginName[] = "PromoteBlocks";

// Pass registration
static llvm::PassPluginLibraryInfo getPromoteBlocksPluginInfo()
{
  return 
  {
    LLVM_PLUGIN_API_VERSION, 
    PluginName,
    LLVM_VERSION_STRING,
    [](llvm::PassBuilder& passBuilder)
    {
      passBuilder.registerPipelineParsingCallback(
        [](llvm::StringRef name, llvm::ModulePassManager& mpm,
          llvm::ArrayRef<llvm::PassBuilder::PipelineElement>)
        {
          if (name.equals(PassName))
          {
            mpm.addPass(jvs::PromoteBlocksPass());
            return true;
          }

          return false;
        });
    }
  };
}


} // namespace 

#define DEBUG_TYPE "promote-blocks"
STATISTIC(NumPromotedBlocks, "Number of blocks promoted to functions");
STATISTIC(NumCandidateBlocks, "Total number of candidate blocks for promotion");
STATISTIC(NumIneligleBlocks, 
  "Number of blocks that were ineligible for promotion");
STATISTIC(NumFailedBlocks, 
  "Number of blocks that couldn't otherwise be promoted");


llvm::PreservedAnalyses jvs::PromoteBlocksPass::run(llvm::Module& m, 
  llvm::ModuleAnalysisManager& manager)
{
  // Contains the list of basic blocks in the module which are candidates for
  // promotion to functions.
  std::vector<llvm::BasicBlock*> blocksToPromote{};

  for (llvm::Function& func : m)
  {
    // We shouldn't be modifying any functions with the [[optnone]] attribute.
    // Since function declarations and empty functions don't have any blocks,
    // we can't do anything with them anyway.
    if (func.hasFnAttribute(llvm::Attribute::OptimizeNone) ||
      func.empty() || func.isDeclaration())
    {
      continue;
    }

    // Copy all the blocks to our block list so we're not creating functions
    // while iterating over them.
    blocksToPromote.reserve(func.size());
    std::transform(func.begin(), func.end(),
      std::back_inserter(blocksToPromote),
      [](llvm::BasicBlock& block)
      {
        return &block;
      });
  }

  // Update the candidate block count statistic.
  NumCandidateBlocks = blocksToPromote.size();

  // Now we can start the work of running code extraction on the blocks which 
  // will [hopefully] promote all of them to their own functions.
  for (llvm::BasicBlock* block : blocksToPromote)
  {
    if (block->isEHPad() || block->isLandingPad())
    {
      // We don't touch exception-handling blocks. Too many side-effects from
      // outlining.
      ++NumIneligleBlocks;
      continue;
    }

    llvm::CodeExtractorAnalysisCache extractionCache{*block->getParent()};
    llvm::CodeExtractor extractor(llvm::makeArrayRef({block}));
    // Find data dependencies that can't be converted into function arguments 
    // for a  promoted block.
    llvm::SetVector<llvm::Value*> inputSet{};
    llvm::SetVector<llvm::Value*> ignoredOutputSet{};
    llvm::SetVector<llvm::Value*> ignoredAllocaSet{};
    extractor.findInputsOutputs(inputSet, ignoredOutputSet, ignoredAllocaSet);
    for (const llvm::Value* v : inputSet)
    {
      llvm::Type* inputType = v->getType();
      if (!inputType->isFirstClassType() || inputType->isMetadataTy() ||
        inputType->isTokenTy())
      {
        ++NumIneligleBlocks;
        continue;
      }
    }

    // Check to make sure the extraction scope is otherwise eligible.
    if (!extractor.isEligible())
    {
      ++NumIneligleBlocks;
      continue;
    }

    llvm::Function* promotedBlockFunc = 
      extractor.extractCodeRegion(extractionCache);
    if (!promotedBlockFunc)
    {
      // Code extraction failed for some reason.
      ++NumFailedBlocks;
    }
    else
    {
      ++NumPromotedBlocks;
      LLVM_DEBUG(llvm::dbgs() << "Promoted block to function: " 
        << promotedBlockFunc->getName() << '\n');
    }
  }

  return llvm::PreservedAnalyses::none();
}

// This function is required for `opt` to be able to recognize this pass when
// requested in the pass pipeline.
extern "C" LLVM_ATTRIBUTE_WEAK auto llvmGetPassPluginInfo()
-> ::llvm::PassPluginLibraryInfo
{
  return getPromoteBlocksPluginInfo();
}
