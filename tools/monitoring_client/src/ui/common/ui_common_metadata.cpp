#include "ui/common/ui_common_metadata.h"

#include "data.h"
#include "ui/ui_helpers.h"

#include <hrz_fnd_string_utils.h>

#include <gsl/gsl-lite.hpp>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <rapidjson/document.h>

#include <optional>
#include <string>
#include <string_view>

namespace
{
std::optional<double> _string_to_double(const std::string& string)
{
    char* end;
    double result = std::strtod(string.data(), &end);

    return (*end == '\0') ? std::make_optional(result) : std::nullopt;
}

struct Comparison
{
    using NumericalCompare = std::function<bool(double, double)>;
    using AlphanumericalCompare = std::function<bool(std::string_view, std::string_view)>;

    const char* symbol;
    std::optional<NumericalCompare> numerical_compare;
    std::optional<AlphanumericalCompare> alphanumerical_compare;
};

static constexpr size_t METADATA_COMPARISON_COUNT = 7;
static const Comparison METADATA_COMPARISONS[] = {
    {"==", [](double a, double b) { return a == b; },
     [](std::string_view a, std::string_view b) { return a == b; }},
    {"!=", [](double a, double b) { return a != b; },
     [](std::string_view a, std::string_view b) { return a != b; }},

    {">=", [](double a, double b) { return a >= b; }, std::nullopt},
    {"<=", [](double a, double b) { return a <= b; }, std::nullopt},

    {"contains", std::nullopt,
     [](std::string_view a, std::string_view b) { return a.find(b) != std::string_view::npos; }},
    {"starts with", std::nullopt,
     [](std::string_view a, std::string_view b) { return hrz::str::starts_with(a, b); }},
    {"ends with", std::nullopt,
     [](std::string_view a, std::string_view b) { return hrz::str::ends_with(a, b); }}};

} // namespace

namespace metadata
{
bool Filter::pass(const std::string& metadata_label, const std::string& metadata_value) const
{
    if (metadata_label != label) return true;

    const auto& comparison = METADATA_COMPARISONS[(int)comparison_id];

    assert(
        (comparison.alphanumerical_compare && !numeric)
        || (comparison.numerical_compare && numeric));

    if (numeric)
    {
        std::optional<double> metadata_num_value = _string_to_double(metadata_value);
        if (!metadata_num_value.has_value())
        {
            return false;
        }

        return comparison.numerical_compare.value()(metadata_num_value.value(), value_num);
    }
    else
    {
        return comparison.alphanumerical_compare.value()(metadata_value, value_str);
    }
}

void Filter::push_json(rapidjson::Value& dst_array, rapidjson::Document& document) const
{
    rapidjson::Value own_array(rapidjson::kArrayType);

    rapidjson::Value v_label;
    v_label.SetString(label.c_str(), document.GetAllocator());

    own_array.PushBack(v_label, document.GetAllocator());
    own_array.PushBack((int)comparison_id, document.GetAllocator());

    if (numeric)
    {
        own_array.PushBack(value_num, document.GetAllocator());
    }
    else
    {
        rapidjson::Value v_value;
        v_value.SetString(value_str.c_str(), document.GetAllocator());

        own_array.PushBack(v_value, document.GetAllocator());
    }

    dst_array.PushBack(own_array, document.GetAllocator());
}

void Filter::load_from_json(const rapidjson::Value& array)
{
    if (!array.IsArray() || array.Size() != 3) return;

    const auto& v_label = array[0];
    if (!v_label.IsString()) return;

    const auto& v_comparison_id = array[1];
    if (!v_comparison_id.IsInt()) return;

    const auto& v_value = array[2];
    if (!v_value.IsString() && !v_value.IsDouble()) return;

    label = v_label.GetString();
    comparison_id = (FilterComparisonId)v_comparison_id.GetInt();
    numeric = v_value.IsDouble();

    if (numeric)
        value_num = v_value.GetDouble();
    else
        value_str = v_value.GetString();
}

std::optional<Filter> show_filter_addition_popup(
    bool open,
    const hrz::flat_hash_set<std::string>& metadata_labels)
{
    if (open) ImGui::OpenPopup("Metadata filter");

    if (ImGui::BeginPopup("Metadata filter"))
    {
        for (const auto& label : metadata_labels)
        {
            if (ImGui::BeginMenu(label.c_str()))
            {
                if (ImGui::MenuItem("Alphanumerical comparison"))
                {
                    Filter filter;
                    filter.label = label.c_str();
                    filter.numeric = false;
                    filter.comparison_id = FilterComparisonId::Equal;

                    ImGui::EndMenu();
                    ImGui::EndPopup();
                    return filter;
                }

                if (ImGui::MenuItem("Numerical comparison"))
                {
                    Filter filter;
                    filter.label = label.c_str();
                    filter.numeric = true;
                    filter.comparison_id = FilterComparisonId::Greater;

                    ImGui::EndMenu();
                    ImGui::EndPopup();
                    return filter;
                }

                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }

    return std::nullopt;
}

bool draw_editable_filter_row(
    Filter& filter,
    const hrz::flat_hash_set<std::string>& metadata_labels)
{
    bool change = false;

    // Metadata label selection
    ImGui::SetNextItemWidth(110.0);
    ImGuiComboFlags combo_flags = ImGuiComboFlags_NoArrowButton;
    if (ImGui::BeginCombo("##Metadata label", filter.label.c_str(), combo_flags))
    {
        for (const auto& label : metadata_labels)
        {
            if (ImGui::Selectable(label.c_str(), label == filter.label))
            {
                filter.label = label;
                change = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    // Comparison mode selection
    const char* combo_preview = METADATA_COMPARISONS[(int)filter.comparison_id].symbol;
    ImGui::SetNextItemWidth(70.0);
    if (ImGui::BeginCombo("##Metadata comparison", combo_preview, combo_flags))
    {
        for (size_t i = 0; i < METADATA_COMPARISON_COUNT; ++i)
        {
            const auto& comparison = METADATA_COMPARISONS[i];
            if ((filter.numeric && comparison.numerical_compare)
                || (!filter.numeric && comparison.alphanumerical_compare))
            {
                const char* symbol = METADATA_COMPARISONS[i].symbol;
                auto comparison_id = (FilterComparisonId)i;
                if (ImGui::Selectable(symbol, filter.comparison_id == comparison_id))
                {
                    filter.comparison_id = comparison_id;
                    change = true;
                }
            }
        }
        ImGui::EndCombo();
    }

    // Value input
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (filter.numeric)
    {
        static const double INPUT_STEP = 1.0;
        static const double INPUT_STEP_FAST = 10.0;
        change |= ImGui::InputScalar(
            "##Numeric value", ImGuiDataType_Double, &filter.value_num, &INPUT_STEP,
            &INPUT_STEP_FAST, "%.0lf");
    }
    else
    {
        change |= ImGui::InputText("##Alphanumeric value", &filter.value_str);
    }

    return change;
}

void draw_metadata_table(gsl::span<const data::Metadata> metadata, const char* table_id)
{
    if (ImGui::BeginTable(table_id, 3, ImGuiTableFlags_SizingFixedFit))
    {
        for (size_t i = 0; i < metadata.size(); i++)
        {
            ImGui::TableNextRow();
            const auto& entry = metadata[i];

            ImGui::PushID(i);

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", entry.name.c_str());

            ImGui::TableSetColumnIndex(1);
            // URL metadata is usually very long, so we enable wrapping the limit the size
            // of the popup.
            ImGui::PushTextWrapPos(350.0f);
            ImGui::Text("%s", entry.value.c_str());
            ImGui::PopTextWrapPos();

            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button("Copy to clipboard"))
            {
                ImGui::SetClipboardText(entry.value.c_str());
            }

            ImGui::SameLine();
            ui::helpers::help_marker("Clipboard is only supported on Windows!");

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

} // namespace metadata
