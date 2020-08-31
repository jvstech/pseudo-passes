#include "support/metadata-util.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

#include "support/unqualified.h"

namespace
{

template <typename InsertPointT>
static llvm::Instruction* create_metadata_marker(InsertPointT& insertPt)
{
  using UnqualifiedInsertPoint = jvs::UnqualifiedType<InsertPointT>;
  static_assert(!std::is_pointer_v<InsertPointT> &&
    (std::is_base_of_v<llvm::BasicBlock, UnqualifiedInsertPoint> ||
      std::is_base_of_v<llvm::Instruction, UnqualifiedInsertPoint>),
    "Insertion point must be a reference/pointer to an instruction or a "
    "basic block.");
  llvm::IRBuilder<> noOpBuilder{&insertPt};
  return noOpBuilder.CreateIntrinsic(llvm::Intrinsic::donothing, {}, {});
}

template <typename InsertPointT>
static llvm::Instruction* create_metadata(InsertPointT& insertPt,
  llvm::StringRef name, llvm::StringRef value)
{
  llvm::Instruction* markerInst = ::create_metadata_marker(insertPt);
  jvs::attach_metadata(*markerInst, name, value);
  return markerInst;
}


} // namespace


llvm::MDNode* jvs::attach_metadata(llvm::Instruction& inst, 
  llvm::StringRef name, llvm::StringRef value)
{
  auto& ctx = inst.getContext();
  llvm::MDNode* node = llvm::MDNode::get(ctx, llvm::MDString::get(ctx, value));
  inst.setMetadata(name, node);
  return node;
}

llvm::MDNode* jvs::attach_metadata(llvm::Instruction& inst, 
  llvm::StringRef name, std::uint64_t value)
{
  llvm::StringRef strAlias(reinterpret_cast<const char*>(&value), 
    sizeof(value));
  return attach_metadata(inst, name, strAlias);
}

llvm::Instruction* jvs::create_metadata_marker(llvm::Instruction& insertBefore)
{
  return ::create_metadata_marker(insertBefore);
}

llvm::Instruction* jvs::create_metadata_marker(llvm::BasicBlock& insertAtEnd)
{
  return ::create_metadata_marker(insertAtEnd);
}

llvm::Instruction* jvs::create_metadata(llvm::Instruction& insertBefore,
  llvm::StringRef name, llvm::StringRef value)
{
  return ::create_metadata(insertBefore, name, value);
}

llvm::Instruction* jvs::create_metadata(llvm::BasicBlock& insertAtEnd,
  llvm::StringRef name, llvm::StringRef value)
{
  return ::create_metadata(insertAtEnd, name, value);
}


std::vector<llvm::Instruction*> jvs::find_metadata(
  llvm::BasicBlock& block, llvm::StringRef name)
{
  std::vector<llvm::Instruction*> results{};
  for (llvm::Instruction& inst : block)
  {
    if (inst.hasMetadataOtherThanDebugLoc())
    {
      if (inst.getMetadata(name))
      {
        results.push_back(&inst);
      }
    }
  }

  return results;
}

std::vector<llvm::Instruction*> jvs::find_metadata(llvm::Function& f,
  llvm::StringRef name)
{
  std::vector<llvm::Instruction*> results{};
  for (llvm::BasicBlock& block : f)
  {
    auto markedInsts = find_metadata(block, name);
    std::copy(markedInsts.begin(), markedInsts.end(),
      std::back_inserter(results));
  }

  return results;
}

std::vector<llvm::Instruction*> jvs::find_metadata(llvm::BasicBlock& block, 
  llvm::StringRef name, StringPredicate&& filterPredicate)
{
  auto metadataInsts = find_metadata(block, name);
  auto matchingInsts = llvm::make_filter_range(metadataInsts,
    [&](llvm::Instruction* inst)
    {
      llvm::StringRef nodeValue = llvm::cast<llvm::MDString>(
        inst->getMetadata(name)->getOperand(0))->getString();
      return filterPredicate(nodeValue);
    });

  std::vector<llvm::Instruction*> results(
    matchingInsts.begin(), matchingInsts.end());
  return results;
}

std::vector<llvm::Instruction*> jvs::find_metadata(llvm::Function& f,
  llvm::StringRef name, StringPredicate&& filterPredicate)
{
  std::vector<llvm::Instruction*> results{};
  for (llvm::BasicBlock& block : f)
  {
    auto matchingInsts = find_metadata(block, name,
      std::forward<StringPredicate>(filterPredicate));
    std::copy(matchingInsts.begin(), matchingInsts.end(),
      std::back_inserter(results));
  }

  return results;
}

std::vector<llvm::Instruction*> jvs::find_metadata(llvm::BasicBlock& block,
  llvm::StringRef name, llvm::StringRef value)
{
  return find_metadata(block, name,
    [&](const llvm::StringRef& nodeValue)
    {
      return nodeValue.equals(value);
    });
}

std::vector<llvm::Instruction*> jvs::find_metadata(llvm::Function& f,
  llvm::StringRef name, llvm::StringRef value)
{
  std::vector<llvm::Instruction*> results{};
  for (llvm::BasicBlock& block : f)
  {
    auto matchingInsts = find_metadata(block, name, value);
    std::copy(matchingInsts.begin(), matchingInsts.end(),
      std::back_inserter(results));
  }

  return results;
}

std::vector<llvm::Instruction*> jvs::find_metadata_markers(
  llvm::BasicBlock& block)
{
  auto markerCalls = llvm::map_range(
    llvm::make_filter_range(block,
      [](llvm::Instruction& inst)
      {
        if (auto* callInst = llvm::dyn_cast<llvm::CallInst>(&inst))
        {
          if (callInst->getCalledFunction() &&
            callInst->getIntrinsicID() == llvm::Intrinsic::donothing &&
            callInst->hasMetadataOtherThanDebugLoc())
          {
            return true;
          }
        }

        return false;
      }),
    [](llvm::Instruction& inst)
      {
        return &inst;
      });

  std::vector<llvm::Instruction*> results(
    markerCalls.begin(), markerCalls.end());
  return results;
}

std::vector<llvm::Instruction*> jvs::find_metadata_markers(llvm::Function& f)
{
  std::vector<llvm::Instruction*> results{};
  for (llvm::BasicBlock& block : f)
  {
    auto markerCalls = find_metadata_markers(block);
    std::copy(markerCalls.begin(), markerCalls.end(),
      std::back_inserter(results));
  }

  return results;
}

std::vector<llvm::Instruction*> jvs::find_metadata_markers(
  llvm::BasicBlock& block, llvm::StringRef name)
{
  auto blockMarkers = find_metadata_markers(block);
  auto markerInsts = llvm::make_filter_range(blockMarkers,
    [&](llvm::Instruction* markerInst)
    {
      if (markerInst->hasMetadataOtherThanDebugLoc())
      {
        return static_cast<bool>(markerInst->getMetadata(name));
      }

      return false;
    });

  std::vector<llvm::Instruction*> results(
    markerInsts.begin(), markerInsts.end());
  return results;
}

std::vector<llvm::Instruction*> jvs::find_metadata_markers(
  llvm::Function& f, llvm::StringRef name)
{
  std::vector<llvm::Instruction*> results{};
  for (llvm::BasicBlock& block : f)
  {
    auto markerCalls = find_metadata_markers(block, name);
    std::copy(markerCalls.begin(), markerCalls.end(),
      std::back_inserter(results));
  }

  return results;
}

std::vector<llvm::Instruction*> jvs::find_metadata_markers(
  llvm::BasicBlock& block, llvm::StringRef name,
  jvs::StringPredicate&& filterPredicate)
{
  auto blockMarkers = find_metadata_markers(block, name);
  auto markerCalls = llvm::make_filter_range(blockMarkers,
    [&](llvm::Instruction* inst)
    {
      llvm::StringRef nodeValue = llvm::cast<llvm::MDString>(
        inst->getMetadata(name)->getOperand(0))->getString();
      return filterPredicate(nodeValue);
    });

  std::vector<llvm::Instruction*> results(
    markerCalls.begin(), markerCalls.end());
  return results;
}

std::vector<llvm::Instruction*> jvs::find_metadata_markers(llvm::Function& f,
  llvm::StringRef name, jvs::StringPredicate&& filterPredicate)
{
  std::vector<llvm::Instruction*> results{};
  for (llvm::BasicBlock& block : f)
  {
    auto markerCalls = find_metadata_markers(block, name,
      std::forward<StringPredicate>(filterPredicate));
    std::copy(markerCalls.begin(), markerCalls.end(),
      std::back_inserter(results));
  }

  return results;
}

llvm::Instruction* jvs::find_metadata_marker(llvm::BasicBlock& block,
  llvm::StringRef name, jvs::StringPredicate&& filterPredicate)
{
  auto blockMarkers = find_metadata_markers(block, name);
  for (llvm::Instruction* markerInst :
    llvm::make_filter_range(blockMarkers,
      [&](llvm::Instruction* inst)
      {
        llvm::StringRef nodeValue = llvm::cast<llvm::MDString>(
          inst->getMetadata(name)->getOperand(0))->getString();
        return filterPredicate(nodeValue);
      }))
  {
    return markerInst;
  }

  return nullptr;
}

llvm::Instruction* jvs::find_metadata_marker(llvm::Function& f,
  llvm::StringRef name, jvs::StringPredicate&& filterPredicate)
{
  for (llvm::BasicBlock& block : f)
  {
    if (auto* markerInst = find_metadata_marker(block, name,
      std::forward<StringPredicate>(filterPredicate)))
    {
      return markerInst;
    }
  }

  return nullptr;
}

llvm::Instruction* jvs::find_metadata_marker(llvm::BasicBlock& block,
  llvm::StringRef name)
{
  return find_metadata_marker(block, name,
    [](const llvm::StringRef&) { return true; });
}

llvm::Instruction* jvs::find_metadata_marker(llvm::Function& f,
  llvm::StringRef name)
{
  return find_metadata_marker(f, name,
    [](const llvm::StringRef&) { return true; });
}

std::vector<llvm::Instruction*> jvs::find_metadata_markers(
  llvm::BasicBlock& block, llvm::StringRef name, llvm::StringRef value)
{
  return find_metadata_markers(block, name,
    [&](const llvm::StringRef& nodeValue)
    {
      return nodeValue.equals(value);
    });
}

std::vector<llvm::Instruction*> jvs::find_metadata_markers(llvm::Function& f,
  llvm::StringRef name, llvm::StringRef value)
{
  return find_metadata_markers(f, name,
    [&](const llvm::StringRef& nodeValue)
    {
      return nodeValue.equals(value);
    });
}

llvm::Instruction* jvs::find_metadata_marker(llvm::BasicBlock& block, 
  llvm::StringRef name, llvm::StringRef value)
{
  return find_metadata_marker(block, name,
    [&](const llvm::StringRef& nodeValue)
    {
      return nodeValue.equals(value);
    });
}

llvm::Instruction* jvs::find_metadata_marker(llvm::Function& f,
  llvm::StringRef name, llvm::StringRef value)
{
  return find_metadata_marker(f, name,
    [&](const llvm::StringRef& nodeValue)
    {
      return nodeValue.equals(value);
    });
}

std::optional<llvm::StringRef> jvs::get_metadata(llvm::Instruction& inst,
  llvm::StringRef name)
{
  if (auto* node = inst.getMetadata(name))
  {
    std::optional<llvm::StringRef> result(std::in_place,
      llvm::cast<llvm::MDString>(node->getOperand(0))->getString());
    return result;
  }

  return {};
}

std::optional<std::uint64_t> jvs::get_uint64_metadata(llvm::Instruction& inst,
  llvm::StringRef name)
{
  if (auto valueBuffer = get_metadata(inst, name))
  {
    llvm::StringRef stringValue = *valueBuffer;
    // This is bad, bad aliasing. Do not do this for real, kids.
    std::optional<std::uint64_t> result(std::in_place,
      *reinterpret_cast<const std::uint64_t*>(stringValue.data()));
    return result;
  }

  return {};
}
