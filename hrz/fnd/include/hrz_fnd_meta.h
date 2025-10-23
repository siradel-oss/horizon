#pragma once

#include <concepts>
#include <type_traits>

namespace hrz
{

template<typename U, typename V, bool kSelected>
struct select
{
    using type = U;
};

template<typename U, typename V>
struct select<U, V, true>
{
    using type = V;
};

// Selects the type U if kSelect is false, otherwise selects the type V.
template<typename U, typename V, bool kSelect>
using select_t = typename select<U, V, kSelect>::type;

static_assert(
    std::is_same_v<select_t<int, float, false>, int>,
    "select_t<int, float, false> is not int");
static_assert(
    std::is_same_v<select_t<int, float, true>, float>,
    "select_t<int, float, true> is not float");

struct empty
{
};

// If T is void, returns empty, otherwise returns T.
// This is useful for metaprogramming where a function argument would otherwise be void.
template<typename T>
using devoid_t = select_t<T, const empty, std::is_void_v<T>>;

/**
 * Helper to create overloaded lambdas for std::visit.
 * Usage:
 *     auto visitor = hrz::overload{
 *         [](TypeA a) { ... },
 *         [](TypeB b) { ... },
 *         ...
 *     };
 *     std::visit(visitor, variant);
 */

template<typename... Ts>
struct overload : Ts...
{
    using Ts::operator()...;
};

template<typename... Ts>
overload(Ts...) -> overload<Ts...>;

template<typename T, typename... Ts>
concept is_one_of = (std::same_as<T, Ts> || ...);

} // namespace hrz
