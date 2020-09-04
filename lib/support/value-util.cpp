#include "support/value-util.h"

#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

std::optional<std::uint64_t> jvs::get_int_constant(const llvm::Value* v)
  noexcept
{
  if (auto* constantInt = llvm::dyn_cast_or_null<llvm::ConstantInt>(v))
  {
    std::optional<std::uint64_t> result(
      std::in_place, constantInt->getZExtValue());
    return result;
  }

  return {};
}

std::optional<llvm::StringRef> jvs::get_string_constant(const llvm::Value* v) 
  noexcept
{
  const llvm::ConstantDataArray* stringConst{nullptr};
  if (auto* globalVar = llvm::dyn_cast<llvm::GlobalVariable>(v))
  {
    if (globalVar->isConstant() && globalVar->hasUniqueInitializer())
    {
      stringConst =
        llvm::dyn_cast<llvm::ConstantDataArray>(globalVar->getInitializer());
    }
  }
  
  if (!stringConst)
  {
    stringConst = llvm::dyn_cast<llvm::ConstantDataArray>(v);
  }

  std::optional<llvm::StringRef> result{};
  if (stringConst)
  {
    if (stringConst->isCString())
    {
      result.emplace(stringConst->getAsCString());
    }
    else if (stringConst->isString())
    {
      result.emplace(stringConst->getAsString());
    }
  }

  return result;
}

llvm::Constant* jvs::create_string_constant(llvm::Module& m,
  llvm::StringRef s) noexcept
{
  return 
    llvm::ConstantDataArray::getString(m.getContext(), s, /*AddNull*/ true);
}
