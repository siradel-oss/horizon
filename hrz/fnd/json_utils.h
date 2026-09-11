// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <rapidjson/document.h>

#include <optional>
#include <span>

namespace hrz::json
{

const rapidjson::Value NullValue;

inline const rapidjson::Value& get_member_or_null(const rapidjson::Value& node, const char* name)
{
    if (!node.IsObject()) return NullValue;

    auto it = node.FindMember(name);
    if (it == node.MemberEnd())
    {
        return NullValue;
    }
    else
    {
        return it->value;
    }
}

inline const rapidjson::Value& get_nested_member_or_null(
    const rapidjson::Value& node,
    std::initializer_list<const char*> path)
{
    const rapidjson::Value* node_ptr = &node;
    for (const char* member : path)
    {
        node_ptr = &get_member_or_null(*node_ptr, member);
        if (!node_ptr || node_ptr->IsNull()) return *node_ptr;
    }
    return *node_ptr;
}

inline std::optional<bool> as_bool(const rapidjson::Value& node)
{
    if (!node.IsBool())
    {
        return std::nullopt;
    }
    else
    {
        return node.GetBool();
    }
}

inline std::optional<float> as_float(const rapidjson::Value& node)
{
    if (!node.IsNumber())
    {
        return std::nullopt;
    }
    else
    {
        return node.GetFloat();
    }
}

inline std::optional<double> as_double(const rapidjson::Value& node)
{
    if (!node.IsNumber())
    {
        return std::nullopt;
    }
    else
    {
        return node.GetDouble();
    }
}

inline std::optional<int> as_int(const rapidjson::Value& node)
{
    if (!node.IsNumber())
    {
        return std::nullopt;
    }
    else if (!node.IsInt())
    {
        return int(node.GetFloat());
    }
    else
    {
        return node.GetInt();
    }
}

inline std::optional<unsigned int> as_uint(const rapidjson::Value& node)
{
    if (!node.IsNumber())
    {
        return std::nullopt;
    }
    else if (!node.IsUint())
    {
        return static_cast<unsigned int>(node.GetFloat());
    }
    else
    {
        return node.GetUint();
    }
}

inline std::optional<uint64_t> as_uint64(const rapidjson::Value& node)
{
    if (!node.IsNumber())
    {
        return std::nullopt;
    }
    else if (!node.IsUint64())
    {
        return uint64_t(node.GetDouble());
    }
    else
    {
        return node.GetUint64();
    }
}

inline std::optional<const char*> as_str(const rapidjson::Value& node)
{
    if (!node.IsString())
    {
        return std::nullopt;
    }
    else
    {
        return node.GetString();
    }
}

template<typename T>
struct EnumVariant
{
    const char* name;
    T value;
};

template<typename T>
inline std::optional<T> as_str_enum(
    const rapidjson::Value& node,
    std::span<const EnumVariant<T>> variants)
{
    std::optional<const char*> str_opt = as_str(node);
    if (!str_opt.has_value())
    {
        return std::nullopt;
    }
    for (int i = 0; i < variants.size(); i++)
    {
        if (strcmp(variants[i].name, str_opt.value()) == 0)
        {
            return variants[i].value;
        }
    }
    return std::nullopt;
}

// Returns the number of values copied from the json array.
size_t copy_array_values(
    std::span<int> values,
    const rapidjson::Value& array,
    int default_value = 0);

size_t copy_array_values(
    std::span<float> values,
    const rapidjson::Value& array,
    float default_value = 0.0F);

size_t copy_array_values(
    std::span<double> values,
    const rapidjson::Value& array,
    double default_value = 0.0);

template<typename T, size_t N>
inline bool copy_array_values_fixed(
    std::span<T, N> values,
    const rapidjson::Value& array,
    T default_value = T{})
{
    return copy_array_values(values, array, default_value) == N;
}

template<typename T, size_t N>
inline bool copy_array_values_fixed(
    T (&values)[N],
    const rapidjson::Value& array,
    T default_value = T{})
{
    return copy_array_values_fixed(std::span<T, N>(values), array, default_value);
}

inline std::optional<int> get_int(const rapidjson::Value& node, const char* name)
{
    return as_int(get_member_or_null(node, name));
}

inline int get_int_or(const rapidjson::Value& node, const char* name, int default_value)
{
    return get_int(node, name).value_or(default_value);
}

inline std::optional<unsigned int> get_uint(const rapidjson::Value& node, const char* name)
{
    return as_uint(get_member_or_null(node, name));
}

inline unsigned int get_uint_or(
    const rapidjson::Value& node,
    const char* name,
    unsigned int default_value)
{
    return get_uint(node, name).value_or(default_value);
}

inline std::optional<uint64_t> get_uint64(const rapidjson::Value& node, const char* name)
{
    return as_uint64(get_member_or_null(node, name));
}

inline uint64_t get_uint64_or(
    const rapidjson::Value& node,
    const char* name,
    uint64_t default_value)
{
    return get_uint64(node, name).value_or(default_value);
}

inline std::optional<bool> get_bool(const rapidjson::Value& node, const char* name)
{
    return as_bool(get_member_or_null(node, name));
}

inline bool get_bool_or(const rapidjson::Value& node, const char* name, bool default_value)
{
    return get_bool(node, name).value_or(default_value);
}

inline std::optional<float> get_float(const rapidjson::Value& node, const char* name)
{
    return as_float(get_member_or_null(node, name));
}

inline float get_float_or(const rapidjson::Value& node, const char* name, float default_value)
{
    return get_float(node, name).value_or(default_value);
}

inline std::optional<double> get_double(const rapidjson::Value& node, const char* name)
{
    return as_double(get_member_or_null(node, name));
}

inline double get_double_or(const rapidjson::Value& node, const char* name, double default_value)
{
    return get_double(node, name).value_or(default_value);
}

inline std::optional<const char*> get_str(const rapidjson::Value& node, const char* name)
{
    const auto& child = get_member_or_null(node, name);
    return as_str(child);
}

inline const char* get_str_or(
    const rapidjson::Value& node,
    const char* name,
    const char* default_value)
{
    return get_str(node, name).value_or(default_value);
}

template<typename T>
inline std::optional<T> get_str_enum(
    const rapidjson::Value& node,
    const char* name,
    std::span<const EnumVariant<T>> variants)
{
    return as_str_enum(get_member_or_null(node, name), variants);
}

template<typename T>
inline T get_str_enum_or(
    const rapidjson::Value& node,
    const char* name,
    std::span<const EnumVariant<T>> variants,
    T default_value)
{
    return get_str_enum(node, name, variants).value_or(default_value);
}

inline size_t get_array_size(const rapidjson::Value& node, const char* array_name)
{
    const auto& child = get_member_or_null(node, array_name);
    if (child.IsArray())
    {
        return child.GetArray().Size();
    }
    else
    {
        return 0;
    }
}

const rapidjson::Value& get_nth_member_or_null(
    const rapidjson::Value& node,
    const char* array_name,
    int index);

const rapidjson::Value& get_nth_or_null(const rapidjson::Value& array, int index);

} // namespace hrz::json
