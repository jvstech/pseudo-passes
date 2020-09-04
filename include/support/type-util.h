#if !defined(JVS_PSEUDO_PASSES_SUPPORT_TYPE_UTIL_H_)
#define JVS_PSEUDO_PASSES_SUPPORT_TYPE_UTIL_H_

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "llvm/ADT/Triple.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

namespace jvs
{
namespace ir_types
{

struct Size {};

template <std::size_t N>
struct Int
{
  static constexpr std::size_t Bits = N;
};

using Bool = Int<1>;

struct IntN
{
  std::size_t Bits{};

  IntN(std::size_t bitCount)
    : Bits(bitCount)
  {
  }
};

template <typename T>
struct Ptr
{
  using value_type = T;
};

using VoidPtr = Ptr<Int<8>>;

template <typename T>
struct Func
{
};

template <typename ReturnT, typename... ArgsT>
struct Func<ReturnT(ArgsT...)>
{
  using return_type = ReturnT;
  using arg_types = std::tuple<ArgsT...>;
};

template <typename ReturnT, typename... ArgsT>
struct Func<ReturnT(*)(ArgsT...)>
{
  using return_type = ReturnT;
  using arg_types = std::tuple<ArgsT...>;
};

} // namespace ir_types

namespace detail
{

template <typename T>
struct TypeCreator
{
};

template <std::size_t N>
struct TypeCreator<ir_types::Int<N>>
{
  llvm::IntegerType* operator()(llvm::Module& m) const noexcept
  {
    return llvm::IntegerType::get(m.getContext(), N);
  }
};

template <>
struct TypeCreator<ir_types::IntN>
{
  llvm::IntegerType* operator()(llvm::Module& m, std::size_t bitCount) const
    noexcept
  {
    return llvm::IntegerType::get(m.getContext(), bitCount);
  }

  llvm::IntegerType* operator()(llvm::Module& m, ir_types::IntN intNValue)
    const noexcept
  {
    return llvm::IntegerType::get(m.getContext(), intNValue.Bits);
  }
};

template <>
struct TypeCreator<void*>
{
  llvm::PointerType* operator()(llvm::Module& m) const noexcept
  {
    return llvm::IntegerType::get(m.getContext(), 8)->getPointerTo();
  }
};

template <typename T>
struct TypeCreator<T*>
{
  llvm::PointerType* operator()(llvm::Module& m) const noexcept
  {
    return TypeCreator<T>{}(m)->getPointerTo();
  }
};

template <>
struct TypeCreator<ir_types::Size>
{
  llvm::IntegerType* operator()(llvm::Module& m) const noexcept
  {
    // This is the naive way of doing this.
    llvm::Triple triple(m.getTargetTriple());
    if (triple.isArch64Bit())
    {
      return TypeCreator<ir_types::Int<64>>{}(m);
    }

    if (triple.isArch16Bit())
    {
      return TypeCreator<ir_types::Int<16>>{}(m);
    }

    return TypeCreator<ir_types::Int<32>>{}(m);
  }
};

template <typename ReturnT, typename... ArgsT>
struct TypeCreator<ReturnT(ArgsT...)>
{
  llvm::FunctionType* operator()(llvm::Module& m) const noexcept
  {
    return llvm::FunctionType::get(TypeCreator<ReturnT>{}(m),
      {(TypeCreator<ArgsT>{}(m), ...)}, false);
  }
};

} // namespace detail

template <typename T>
static auto create_type(llvm::Module& m)
-> decltype(std::declval<detail::TypeCreator<T>>()(
  std::declval<llvm::Module&>()))
{
  return detail::TypeCreator<T>{}(m);
}

static auto create_type(llvm::Module& m, std::size_t bitCount)
{
  return detail::TypeCreator<ir_types::IntN>{}(m, bitCount);
}

} // namespace jvs


#endif // !JVS_PSEUDO_PASSES_SUPPORT_TYPE_UTIL_H_
