//!
//! @file include/support/metadata-util.h.
//!
//! Declares the metadata utility class
//!
#if !defined(JVS_PSEUDO_PASSES_SUPPORT_METADATA_UTIL_H_)
#define JVS_PSEUDO_PASSES_SUPPORT_METADATA_UTIL_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "llvm/ADT/StringRef.h"

// forward declarations
namespace llvm
{

class BasicBlock;
class Function;
class Instruction;
class MDNode;
class MDString;

} // namespace llvm


namespace jvs
{

using StringPredicate = std::function<bool(const llvm::StringRef&)>;

//!
//! Attach custom metadata to the given instruction.
//!
//! @param [in,out] inst
//!   The instruction to which metadata will be attached.
//! @param          name
//!   Metadata name.
//! @param          value
//!   Metadata value.
//!
//! @returns
//!   A pointer to a llvm::MDNode created by this function.
//!
llvm::MDNode* attach_metadata(llvm::Instruction& inst, llvm::StringRef name,
  llvm::StringRef value);

llvm::MDNode* attach_metadata(llvm::Instruction& inst, llvm::StringRef name,
  std::uint64_t value);

template <typename... InstructionTs>
void attach_metadata(llvm::StringRef name, llvm::StringRef value,
  InstructionTs&&... inst)
{
  ((attach_metadata(std::forward<InstructionTs>(inst), name, value)), ...);
}

template <typename... InstructionTs>
void attach_metadata(llvm::StringRef name, std::uint64_t value,
  InstructionTs&&... inst)
{
  ((attach_metadata(std::forward<InstructionTs>(inst), name, value)), ...);
}

//!
//! Creates metadata marker
//!
//! @param [in,out] insertAtEnd
//!   The insert at end.
//!
//! @returns
//!   Null if it fails, else the new metadata marker.
//!
llvm::Instruction* create_metadata_marker(llvm::BasicBlock& insertAtEnd);

//!
//! Creates metadata marker
//!
//! @param [in,out] insertBefore
//!   The insert before.
//!
//! @returns
//!   Null if it fails, else the new metadata marker.
//!
llvm::Instruction* create_metadata_marker(llvm::Instruction& insertBefore);

//!
//! Creates a metadata
//!
//! @param [in,out] insertBefore
//!   The insert before.
//! @param          name
//!   The name.
//! @param          value
//!   The value.
//!
//! @returns
//!   Null if it fails, else the new metadata.
//!
llvm::Instruction* create_metadata(llvm::Instruction& insertBefore, 
  llvm::StringRef name, llvm::StringRef value);

//!
//! Creates a metadata
//!
//! @param [in,out] insertAtEnd
//!   The insert at end.
//! @param          name
//!   The name.
//! @param          value
//!   The value.
//!
//! @returns
//!   Null if it fails, else the new metadata.
//!
llvm::Instruction* create_metadata(llvm::BasicBlock& insertAtEnd,
  llvm::StringRef name, llvm::StringRef value);

//!
//! Searches for the first metadata markers
//!
//! @param [in,out] block
//!   The block.
//!
//! @returns
//!   Null if it fails, else the found metadata markers.
//!
std::vector<llvm::Instruction*> find_metadata_markers(llvm::BasicBlock& block);

//!
//! Searches for the first metadata markers
//!
//! @param [in,out] f
//!   A llvm::Function to process.
//!
//! @returns
//!   Null if it fails, else the found metadata markers.
//!
std::vector<llvm::Instruction*> find_metadata_markers(llvm::Function& f);

//!
//! Searches for the first metadata markers
//!
//! @param [in,out] block
//!   The block.
//! @param          name
//!   The name.
//!
//! @returns
//!   Null if it fails, else the found metadata markers.
//!
std::vector<llvm::Instruction*> find_metadata_markers(llvm::BasicBlock& block,
  llvm::StringRef name);

//!
//! Searches for the first metadata markers
//!
//! @param [in,out] f
//!   A llvm::Function to process.
//! @param          name
//!   The name.
//!
//! @returns
//!   Null if it fails, else the found metadata markers.
//!
std::vector<llvm::Instruction*> find_metadata_markers(llvm::Function& f, 
  llvm::StringRef name);

//!
//! Searches for the first metadata
//!
//! @param [in,out] block
//!   The block.
//! @param          name
//!   The name.
//!
//! @returns
//!   Null if it fails, else the found metadata.
//!
std::vector<llvm::Instruction*> find_metadata(llvm::BasicBlock& block, 
  llvm::StringRef name);

//!
//! Searches for the first metadata
//!
//! @param [in,out] f
//!   A llvm::Function to process.
//! @param          name
//!   The name.
//!
//! @returns
//!   Null if it fails, else the found metadata.
//!
std::vector<llvm::Instruction*> find_metadata(llvm::Function& f,
  llvm::StringRef name);

//!
//! Searches for the first metadata
//!
//! @param [in,out] block
//!   The block.
//! @param          name
//!   The name.
//! @param [in,out] filterPredicate
//!   The filter predicate.
//!
//! @returns
//!   Null if it fails, else the found metadata.
//!
std::vector<llvm::Instruction*> find_metadata(llvm::BasicBlock& block,
  llvm::StringRef name, StringPredicate&& filterPredicate);

//!
//! Searches for the first metadata
//!
//! @param [in,out] f
//!   A llvm::Function to process.
//! @param          name
//!   The name.
//! @param [in,out] filterPredicate
//!   The filter predicate.
//!
//! @returns
//!   Null if it fails, else the found metadata.
//!
std::vector<llvm::Instruction*> find_metadata(llvm::Function& f,
  llvm::StringRef name, StringPredicate&& filterPredicate);

//!
//! Searches for the first metadata
//!
//! @param [in,out] block
//!   The block.
//! @param          name
//!   The name.
//! @param          value
//!   The value.
//!
//! @returns
//!   Null if it fails, else the found metadata.
//!
std::vector<llvm::Instruction*> find_metadata(llvm::BasicBlock& block,
  llvm::StringRef name, llvm::StringRef value);

//!
//! Searches for the first metadata
//!
//! @param [in,out] f
//!   A llvm::Function to process.
//! @param          name
//!   The name.
//! @param          value
//!   The value.
//!
//! @returns
//!   Null if it fails, else the found metadata.
//!
std::vector<llvm::Instruction*> find_metadata(llvm::Function& f,
  llvm::StringRef name, llvm::StringRef value);

//!
//! Searches for the first metadata markers
//!
//! @param [in,out] block
//!   The block.
//! @param          name
//!   The name.
//! @param [in,out] filterPredicate
//!   The filter predicate.
//!
//! @returns
//!   Null if it fails, else the found metadata markers.
//!
std::vector<llvm::Instruction*> find_metadata_markers(llvm::BasicBlock& block, 
  llvm::StringRef name, StringPredicate&& filterPredicate);

//!
//! Searches for the first metadata markers
//!
//! @param [in,out] f
//!   A llvm::Function to process.
//! @param          name
//!   The name.
//! @param [in,out] filterPredicate
//!   The filter predicate.
//!
//! @returns
//!   Null if it fails, else the found metadata markers.
//!
std::vector<llvm::Instruction*> find_metadata_markers(llvm::Function& f,
  llvm::StringRef name, StringPredicate&& filterPredicate);

//!
//! Searches for the first metadata markers
//!
//! @param [in,out] block
//!   The block.
//! @param          name
//!   The name.
//! @param          value
//!   The value.
//!
//! @returns
//!   Null if it fails, else the found metadata markers.
//!
std::vector<llvm::Instruction*> find_metadata_markers(llvm::BasicBlock& block,
  llvm::StringRef name, llvm::StringRef value);

//!
//! Searches for the first metadata markers
//!
//! @param [in,out] f
//!   A llvm::Function to process.
//! @param          name
//!   The name.
//! @param          value
//!   The value.
//!
//! @returns
//!   Null if it fails, else the found metadata markers.
//!
std::vector<llvm::Instruction*> find_metadata_markers(llvm::Function& f,
  llvm::StringRef name, llvm::StringRef value);

//!
//! Searches for the first metadata marker
//!
//! @param [in,out] block
//!   The block.
//! @param          name
//!   The name.
//! @param [in,out] filterPredicate
//!   The filter predicate.
//!
//! @returns
//!   Null if it fails, else the found metadata marker.
//!
llvm::Instruction* find_metadata_marker(llvm::BasicBlock& block,
  llvm::StringRef name, StringPredicate&& filterPredicate);

//!
//! Searches for the first metadata marker
//!
//! @param [in,out] f
//!   A llvm::Function to process.
//! @param          name
//!   The name.
//! @param [in,out] filterPredicate
//!   The filter predicate.
//!
//! @returns
//!   Null if it fails, else the found metadata marker.
//!
llvm::Instruction* find_metadata_marker(llvm::Function& f,
  llvm::StringRef name, StringPredicate&& filterPredicate);

//!
//! Searches for the first metadata marker
//!
//! @param [in,out] block
//!   The block.
//! @param          name
//!   The name.
//!
//! @returns
//!   Null if it fails, else the found metadata marker.
//!
llvm::Instruction* find_metadata_marker(llvm::BasicBlock& block,
  llvm::StringRef name);

//!
//! Searches for the first metadata marker
//!
//! @param [in,out] f
//!   A llvm::Function to process.
//! @param          name
//!   The name.
//!
//! @returns
//!   Null if it fails, else the found metadata marker.
//!
llvm::Instruction* find_metadata_marker(llvm::Function& f, 
  llvm::StringRef name);

//!
//! Searches for the first metadata marker
//!
//! @param [in,out] block
//!   The block.
//! @param          name
//!   The name.
//! @param          value
//!   The value.
//!
//! @returns
//!   Null if it fails, else the found metadata marker.
//!
llvm::Instruction* find_metadata_marker(llvm::BasicBlock& block,
  llvm::StringRef name, llvm::StringRef value);

//!
//! Searches for the first metadata marker
//!
//! @param [in,out] f
//!   A llvm::Function to process.
//! @param          name
//!   The name.
//! @param          value
//!   The value.
//!
//! @returns
//!   Null if it fails, else the found metadata marker.
//!
llvm::Instruction* find_metadata_marker(llvm::Function& f, llvm::StringRef name,
  llvm::StringRef value);

//!
//! Gets a metadata
//!
//! @param [in,out] inst
//!   The instance.
//! @param          name
//!   The name.
//!
//! @returns
//!   The metadata.
//!
std::optional<llvm::StringRef> get_metadata(llvm::Instruction& inst,
  llvm::StringRef name);

std::optional<std::uint64_t> get_uint64_metadata(llvm::Instruction& inst,
  llvm::StringRef name);

} // namespace jvs


#endif // !JVS_PSEUDO_PASSES_SUPPORT_METADATA_UTIL_H_
