#if !defined(JVS_PSEUDO_PASSES_SUPPORT_UNQUALIFIED_H_)
#define JVS_PSEUDO_PASSES_SUPPORT_UNQUALIFIED_H_

#include <type_traits>

namespace jvs
{

template <typename T>
struct Unqualified
{
  using type = std::remove_cv_t<T>;
};

template <typename T>
struct Unqualified<T*>
{
  using type = typename Unqualified<T>::type;
};

template <typename T>
struct Unqualified<T&>
{
  using type = typename Unqualified<T>::type;
};

template <typename T>
struct Unqualified<T&&>
{
  using type = typename Unqualified<T>::type;
};

template <typename T>
using UnqualifiedType = typename Unqualified<T>::type;

} // namespace jvs


#endif // !JVS_PSEUDO_PASSES_SUPPORT_UNQUALIFIED_H_
