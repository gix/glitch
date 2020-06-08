#pragma once
#include <type_traits>

namespace gt
{

/// <summary>
///   If <typeparamref name="T"/> is a complete type, provides the member
///   constant <c>value</c> equal <see langword="true"/>. For any other type,
///   <c>value</c> is <see langword="false"/>.
/// </summary>
/// <remarks>
///   This trait is designed for one use only: to trigger a hard error (via
///   static_assert) when a template is accidentally instantiated on an
///   incomplete type. Any other use case will cause ODR violations as the
///   "completeness" of type T may vary at different points in the current
///   translation unit, as well as across translations units. In particular this
///   trait should never ever be used to change code paths depending on the
///   completeness of a type.
/// </remarks>
template<typename T, typename = std::size_t>
struct is_complete : std::false_type
{};

template<typename T>
struct is_complete<T, decltype(sizeof(T))> : std::true_type
{};

/// <summary>Variable template for <see cref="is_complete"/>.</summary>
template<typename T>
inline constexpr bool is_complete_v = is_complete<T>::value;

} // namespace gt
