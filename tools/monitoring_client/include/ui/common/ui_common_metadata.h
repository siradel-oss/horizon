#pragma once

#include "hrz/fnd/flat_hash_set.h"

#include <rapidjson/document.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace data
{
struct Metadata;
}

namespace metadata
{
enum class FilterComparisonId
{
    Equal = 0,
    NonEqual,

    // Numerical only
    Greater,
    Lesser,

    // Alphanumerical only
    Contains,
    StartsWith,
    EndsWith
};

struct Filter
{
    FilterComparisonId comparison_id;

    std::string label;
    std::string value_str;

    bool numeric;
    double value_num;

    bool pass(const std::string& metadata_label, const std::string& metadata_value) const;

    void push_json(rapidjson::Value& dst_array, rapidjson::Document&) const;
    void load_from_json(const rapidjson::Value& array);
};

// Returns a newly created filter if the user has clicked one of the metadata labels
std::optional<Filter> show_filter_addition_popup(
    bool open,
    const hrz::flat_hash_set<std::string>& metadata_labels);

// Returns whether the filter has been changed or not
bool draw_editable_filter_row(
    Filter& filter,
    const hrz::flat_hash_set<std::string>& metadata_labels);

// Draws a table with all the given metadata with buttons to copy any value to the clipboard
void draw_metadata_table(std::span<const data::Metadata>, const char* table_id = "##Metadata");
} // namespace metadata
