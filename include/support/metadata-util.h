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
class Value;

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

//!
//! Attach custom metadata to the given instruction.
//!
//! @param [in,out] inst
//!   The instruction to which metadata will be attached.
//! @param name
//!   Metadata name.
//! @param value
//!   Metadata value.
//!
//! @returns
//!   A pointer to a llvm::MDNode created by this function.
//!
llvm::MDNode* attach_metadata(llvm::Instruction& inst, llvm::StringRef name,
  std::uint64_t value);

//!
//! Attach custom metadata to multiple instructions.
//!
template <typename... InstructionTs>
void attach_metadata(llvm::StringRef name, llvm::StringRef value,
  InstructionTs&&... inst)
{
  ((attach_metadata(std::forward<InstructionTs>(inst), name, value)), ...);
}

//!
//! Attach custom metadata to multiple instructions.
//!
template <typename... InstructionTs>
void attach_metadata(llvm::StringRef name, std::uint64_t value,
  InstructionTs&&... inst)
{
  ((attach_metadata(std::forward<InstructionTs>(inst), name, value)), ...);
}

//!
//! Creates no-op instruction for attaching metadata for pass purposes.
//!
llvm::Instruction* create_metadata_marker(llvm::BasicBlock& insertAtEnd);

//!
//! Creates no-op instruction for attaching metadata for pass purposes.
//!
llvm::Instruction* create_metadata_marker(llvm::Instruction& insertBefore);

//!
//! Creates metadata no-op instruction with the given metadata already attached.
//!
llvm::Instruction* create_metadata(llvm::Instruction& insertBefore, 
  llvm::StringRef name, llvm::StringRef value);

//!
//! Creates metadata no-op instruction with the given metadata already attached.
//!
llvm::Instruction* create_metadata(llvm::BasicBlock& insertAtEnd,
  llvm::StringRef name, llvm::StringRef value);

//!
//! Searches for all metadata no-op instructions in the given block.
//!
std::vector<llvm::Instruction*> find_metadata_markers(llvm::BasicBlock& block);

//!
//! Searches for all metadata no-op instructions in the given function.
//!
std::vector<llvm::Instruction*> find_metadata_markers(llvm::Function& f);

//!
//! Searches for metadata no-op instructions with the given metadata name in the
//! given block.
//!
std::vector<llvm::Instruction*> find_metadata_markers(llvm::BasicBlock& block,
  llvm::StringRef name);

//!
//! Searches for metadata no-op instructions with the given metadata name in the
//! given function.
//!
std::vector<llvm::Instruction*> find_metadata_markers(llvm::Function& f, 
  llvm::StringRef name);

std::vector<llvm::Instruction*> find_metadata(llvm::BasicBlock& block, 
  llvm::StringRef name);

std::vector<llvm::Instruction*> find_metadata(llvm::Function& f,
  llvm::StringRef name);

std::vector<llvm::Instruction*> find_metadata(llvm::BasicBlock& block,
  llvm::StringRef name, StringPredicate&& filterPredicate);

std::vector<llvm::Instruction*> find_metadata(llvm::Function& f,
  llvm::StringRef name, StringPredicate&& filterPredicate);

std::vector<llvm::Instruction*> find_metadata(llvm::BasicBlock& block,
  llvm::StringRef name, llvm::StringRef value);

std::vector<llvm::Instruction*> find_metadata(llvm::Function& f,
  llvm::StringRef name, llvm::StringRef value);

std::vector<llvm::Instruction*> find_metadata_markers(llvm::BasicBlock& block, 
  llvm::StringRef name, StringPredicate&& filterPredicate);

std::vector<llvm::Instruction*> find_metadata_markers(llvm::Function& f,
  llvm::StringRef name, StringPredicate&& filterPredicate);

std::vector<llvm::Instruction*> find_metadata_markers(llvm::BasicBlock& block,
  llvm::StringRef name, llvm::StringRef value);

std::vector<llvm::Instruction*> find_metadata_markers(llvm::Function& f,
  llvm::StringRef name, llvm::StringRef value);

llvm::Instruction* find_metadata_marker(llvm::BasicBlock& block,
  llvm::StringRef name, StringPredicate&& filterPredicate);

llvm::Instruction* find_metadata_marker(llvm::Function& f,
  llvm::StringRef name, StringPredicate&& filterPredicate);

llvm::Instruction* find_metadata_marker(llvm::BasicBlock& block,
  llvm::StringRef name);

llvm::Instruction* find_metadata_marker(llvm::Function& f, 
  llvm::StringRef name);

llvm::Instruction* find_metadata_marker(llvm::BasicBlock& block,
  llvm::StringRef name, llvm::StringRef value);

llvm::Instruction* find_metadata_marker(llvm::Function& f, llvm::StringRef name,
  llvm::StringRef value);

std::optional<llvm::StringRef> get_metadata(llvm::Instruction& inst,
  llvm::StringRef name);

std::optional<std::uint64_t> get_uint64_metadata(llvm::Instruction& inst,
  llvm::StringRef name);

} // namespace jvs


#endif // !JVS_PSEUDO_PASSES_SUPPORT_METADATA_UTIL_H_
