#include "passes/promote-blocks.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"

#include "support/value-util.h"

namespace
{

static constexpr char PromoteBlocksPassName[] = "promote-blocks";
static constexpr char PromoteInstsPassName[] = "promote-instructions";
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
          if (name.equals(PromoteBlocksPassName))
          {
            mpm.addPass(jvs::PromoteBlocksPass());
            return true;
          }

          if (name.equals(PromoteInstsPassName))
          {
            mpm.addPass(jvs::PromoteBlocksPass(/*perInstruction =*/ true));
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

jvs::PromoteBlocksPass::PromoteBlocksPass(bool perInstruction /*= false*/)
  : PerInstruction(perInstruction)
{
}

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
    if (!PerInstruction)
    {
      blocksToPromote.reserve(func.size());
      std::transform(func.begin(), func.end(),
        std::back_inserter(blocksToPromote),
        [](llvm::BasicBlock& block)
        {
          return &block;
        });
    }
    else
    {
      blocksToPromote.reserve(std::accumulate(func.begin(),
        func.end(), 0, 
        [](std::size_t n, llvm::BasicBlock& b)
        {
          return n + b.sizeWithoutDebug();
        }));

      // Mapping of instructions to their parent block name (if the parent block
      // has a name).
      std::unordered_map<llvm::Instruction*, std::string> instBlockMap{};
      // Collect the instructions for the block before modifying anything.
      std::vector<llvm::Instruction*> insts{};
      for (llvm::BasicBlock& block : func)
      {
        insts.reserve(block.sizeWithoutDebug());
        if (&block == &block.getParent()->getEntryBlock())
        {
          // Certain instructions such as "alloca" and "llvm.localescape" *must*
          // remain in the entry block of the function. We check for the entry 
          // block and call llvm::PrepareToSplitEntryBlock() to ensure all the
          // required instructions stay in the entry block.
          llvm::PrepareToSplitEntryBlock(block, block.begin());
        }

        auto instPtrs = llvm::map_range(block, 
          [](auto&& inst) { return &inst; });
        std::copy_if(instPtrs.begin(), instPtrs.end(),
          std::back_inserter(insts),
          [&instBlockMap](llvm::Instruction* inst)
          {
            // Shouldn't split blocks on any of these types of instructions.
            if (!is_any<llvm::AllocaInst, llvm::PHINode, llvm::CatchPadInst,
              llvm::LandingPadInst, llvm::DbgInfoIntrinsic>(inst) &&
              !inst->isTerminator())
            {
              instBlockMap.emplace(inst, inst->getParent()->hasName()
                ? inst->getParent()->getName().str()
                : "split");
              return true;
            }

            return false;
          });
      }

      // Split every block on the given instructions.
      std::transform(insts.begin(), insts.end(),
        std::back_inserter(blocksToPromote),
        [&instBlockMap](llvm::Instruction* inst)
        {
          return llvm::SplitBlock(inst->getParent(), inst, 
            /*DT*/ nullptr, 
            /*LI*/ nullptr, 
            /*MSSAU*/ nullptr,
            // Function names can get unwieldy *really quick* if we let
            // llvm::SplitBlock() pick the name of the new blocks, so we keep 
            // the original name and let LLVM add numbers to make them 
            // distinct.
            [&, inst]
            {
              auto [instNamePair, wasEmplaced] =
                instBlockMap.emplace(inst, "split");
              return instNamePair->second;
            }());
        });
    }
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
