#include "hrz_fnd_json_utils.h"

namespace hrz::json
{
size_t copy_array_values(gsl::span<int> values, const rapidjson::Value& array, int default_value)
{
    size_t index = 0;
    if (array.IsArray())
    {
        for (const auto& value : array.GetArray())
        {
            if (index == values.size()) break;
            values[index++] = as_int(value).value_or(default_value);
        }
    }

    size_t num_copied_values = index;
    for (size_t i = index; i < values.size(); ++i)
    {
        values[i] = default_value;
    }

    return num_copied_values;
}

size_t copy_array_values(
    gsl::span<float> values,
    const rapidjson::Value& array,
    float default_value)
{
    size_t index = 0;
    if (array.IsArray())
    {
        for (const auto& value : array.GetArray())
        {
            if (index == values.size()) break;
            values[index++] = as_float(value).value_or(default_value);
        }
    }

    size_t num_copied_values = index;
    for (size_t i = index; i < values.size(); ++i)
    {
        values[i] = default_value;
    }

    return num_copied_values;
}

size_t copy_array_values(
    gsl::span<double> values,
    const rapidjson::Value& array,
    double default_value)
{
    size_t index = 0;
    if (array.IsArray())
    {
        for (const auto& value : array.GetArray())
        {
            if (index == values.size()) break;
            values[index++] = as_double(value).value_or(default_value);
        }
    }

    size_t num_copied_values = index;
    for (size_t i = index; i < values.size(); ++i)
    {
        values[i] = default_value;
    }

    return num_copied_values;
}

const rapidjson::Value& get_nth_member_or_null(
    const rapidjson::Value& node,
    const char* array_name,
    int index)
{
    const auto& child = get_member_or_null(node, array_name);
    return get_nth_or_null(child, index);
}

const rapidjson::Value& get_nth_or_null(const rapidjson::Value& array, int index)
{
    if (!array.IsArray() || (size_t)index >= array.GetArray().Size())
    {
        return NullValue;
    }
    else
    {
        return array.GetArray()[index];
    }
}

} // namespace hrz::json
