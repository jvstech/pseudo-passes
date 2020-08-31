#if !defined(JVS_PSEUDO_PASSES_FUSE_FUNCTIONS_COMBINED_CALL_SITE_H_)
#define JVS_PSEUDO_PASSES_FUSE_FUNCTIONS_COMBINED_CALL_SITE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"

// forward declarations
namespace llvm
{

class AllocaInst;
class BasicBlock;
class CallInst;
class Function;
class FunctionType;
class LoadInst;
class StoreInst;
class SwitchInst;
class Value;

} // namespace llvm


namespace jvs
{

// String constants for metadata names.
static constexpr char FuseFunctionName[] = "fuse.function";
static constexpr char FuseFunctionStart[] = "fuse.function.start";
static constexpr char FuseFunctionEnd[] = "fuse.function.end";
static constexpr char FuseFunctionArgIdx[] = "fuse.function.argidx";
static constexpr char FuseFunctionRet[] = "fuse.function.ret";
static constexpr char FuseFunctionBlockId[] = "fuse.function.blockid";

using IdBlockPair = std::pair<std::uint64_t, llvm::BasicBlock*>;
using IdBlockMap = std::map<std::uint64_t, llvm::BasicBlock*>;
using IdStoreMap = std::map<std::uint64_t, llvm::StoreInst*>;
using ArgIdxAllocaMap = std::map<std::size_t, llvm::AllocaInst*>;

class CombinedCallSite
{
  CombinedCallSite(llvm::Function& caller, llvm::StringRef calleeName,
    llvm::SwitchInst& returnSwitch, llvm::LoadInst& parentBlockIdLoad);

public:
  
  CombinedCallSite() = delete;
  CombinedCallSite(const CombinedCallSite&) = delete;
  CombinedCallSite(CombinedCallSite&&) = default;
  CombinedCallSite& operator=(const CombinedCallSite&) = delete;
  CombinedCallSite& operator=(CombinedCallSite&&) = default;

  llvm::Function& caller() const noexcept;

  inline const llvm::StringRef& function_name() const noexcept
  {
    return callee_name_;
  }

  std::vector<std::uint64_t> get_block_ids() const noexcept;
  IdStoreMap get_block_id_stores() const noexcept;
  llvm::AllocaInst* get_block_id_pointer() const noexcept;

  std::uint64_t get_max_block_id() const noexcept;

  IdBlockMap get_branching_blocks() const noexcept;
  llvm::BasicBlock* get_branching_block(std::uint64_t blockId) const noexcept;

  IdBlockMap get_return_blocks() const noexcept;
  llvm::BasicBlock* get_return_block(std::uint64_t blockId) const
    noexcept;

  ArgIdxAllocaMap get_argument_pointers() const noexcept;
  llvm::AllocaInst* get_argument_pointer(std::size_t argIdx) const noexcept;

  llvm::AllocaInst* get_return_pointer() const noexcept;

  bool combine_call(llvm::CallInst& callInst) noexcept;
  bool combine(CombinedCallSite& other) noexcept;

  friend std::vector<CombinedCallSite> find_combined_call_sites(
    llvm::Function& caller);

private:
  llvm::Function* caller_;
  llvm::StringRef callee_name_;
  llvm::SwitchInst* return_switch_{nullptr};
  llvm::LoadInst* parent_block_id_load_{nullptr};
};

std::vector<CombinedCallSite> find_combined_call_sites(
  llvm::Function& caller);

auto map_combined_call_sites(llvm::Function& caller) noexcept
-> llvm::DenseMap<llvm::StringRef, std::vector<jvs::CombinedCallSite>>;

} // namespace jvs


#endif // !JVS_PSEUDO_PASSES_FUSE_FUNCTIONS_COMBINED_CALL_SITE_H_
