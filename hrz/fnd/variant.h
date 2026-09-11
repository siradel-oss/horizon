// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <variant>

namespace hrz
{

namespace internal
{

template<typename>
struct tag
{
};

template<typename T, typename V>
struct get_index;

template<typename T, typename... Ts>
struct get_index<T, ::std::variant<Ts...>>
{
    using Variant = ::std::variant<internal::tag<Ts>...>;

    static constexpr size_t index() { return Variant(internal::tag<T>{}).index(); }
};

} // namespace internal

template<typename V, typename T>
static constexpr size_t index_of_variant()
{
    return internal::get_index<T, V>::index();
}

} // namespace hrz
