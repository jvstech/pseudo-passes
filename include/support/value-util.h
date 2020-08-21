#if !defined(JVS_PSEUDO_PASSES_SUPPORT_VALUE_UTIL_H_)
#define JVS_PSEUDO_PASSES_SUPPORT_VALUE_UTIL_H_

#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"

namespace jvs
{

template <typename... ValueTs>
static bool is_any(const llvm::Value* v)
{
  return (llvm::isa<ValueTs>(v) || ...);
}

} // namespace jvs


#endif // !JVS_PSEUDO_PASSES_SUPPORT_VALUE_UTIL_H_
