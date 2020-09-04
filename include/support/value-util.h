#if !defined(JVS_PSEUDO_PASSES_SUPPORT_VALUE_UTIL_H_)
#define JVS_PSEUDO_PASSES_SUPPORT_VALUE_UTIL_H_

#include <cstdint>
#include <optional>

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"

// forward declarations
namespace llvm
{

class Constant;
class Module;

} // namespace llvm


namespace jvs
{

template <typename... ValueTs>
static bool is_any(const llvm::Value* v) noexcept
{
  return (llvm::isa<ValueTs>(v) || ...);
}

std::optional<std::uint64_t> get_int_constant(const llvm::Value* v) noexcept;

std::optional<llvm::StringRef> get_string_constant(const llvm::Value* v) 
  noexcept;

llvm::Constant* create_string_constant(llvm::Module& m,
  llvm::StringRef s) noexcept;

} // namespace jvs


#endif // !JVS_PSEUDO_PASSES_SUPPORT_VALUE_UTIL_H_
