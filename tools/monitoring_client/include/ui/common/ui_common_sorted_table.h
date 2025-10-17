#pragma once

#include "ui/ui_helpers.h"

#include <imgui.h>

#include <algorithm>

namespace sorted_table
{
template<typename Entry>
using TableColumnContentFunction = std::function<void(const Entry&)>;

// Should return the difference between the first and second element.
template<typename Entry>
using TableColumnCompareFunction = std::function<double(const Entry&, const Entry&)>;

template<typename Entry>
struct ColumnDesc
{
    std::string id;
    ImGuiTableColumnFlags flags;
    double initial_width;

    TableColumnContentFunction<Entry> display_fn;
    TableColumnCompareFunction<Entry> compare_fn;
};

template<typename Entry>
class SortedTable
{
public:
    SortedTable(const char* id, ImGuiTableFlags flags) : _id(id), _flags(flags)
    {
        _default_sort = [](const Entry& a, const Entry& b)
        {
            static size_t count = 0;
            return (void*)&a < (void*)&b;
        };
    }

    void push_column(ColumnDesc<Entry>&& column) { _columns.push_back(column); }

    void pop_column() { _columns.pop_back(); }

    void replace_column(std::string_view column_id, ColumnDesc<Entry>&& new_column)
    {
        for (auto& column : _columns)
        {
            if (column.id == column_id)
            {
                column = std::move(new_column);
                return;
            }
        }
        assert(false);
    }

    void erase_column(std::string_view column_id)
    {
        for (auto it = _columns.begin(); it != _columns.end(); it++)
        {
            if (it->id == column_id)
            {
                _columns.erase(it);
                return;
            }
        }
        assert(false);
    }

    std::optional<size_t> get_hovered_entry() const { return _hovered_entry_index; }

    std::optional<size_t> get_selected_entry() const { return _selected_entry_index; }

    void select_entry(size_t index) { _selected_entry_index = index; }

    void scroll_to_entry(size_t index) { _should_scroll_to_entry = index; }

    void draw(
        std::span<const Entry> entries,
        bool data_is_dirty,
        std::optional<hrz::flat_hash_set<size_t>> filter = std::nullopt,
        lm::dvec2 outer_size = {0.0, 0.0})
    {
        assert(_columns.size() > 0);

        if (ImGui::BeginTable(_id, _columns.size(), _flags, outer_size))
        {
            // Prevent the headers row from scrolling
            ImGui::TableSetupScrollFreeze(0, 1);

            for (const auto& column : _columns)
            {
                ImGui::TableSetupColumn(column.id.c_str(), column.flags, column.initial_width);
            }

            ImGui::TableHeadersRow();

            // Check if number of entries has changed since draw call
            data_is_dirty |= (entries.size() != _sorted_entries.size());

            // Update sorted cells if they need to
            auto* sort_specs = ImGui::TableGetSortSpecs();
            if (sort_specs)
            {
                bool should_sort = data_is_dirty || entries.size() != _sorted_entries.size()
                    || sort_specs->SpecsDirty;
                if (should_sort)
                {
                    _sort_entries(entries, sort_specs);
                    sort_specs->SpecsDirty = false;
                }
            }

            // If data has changed, indices kept between calls must be reset
            if (data_is_dirty)
            {
                _hovered_entry_index = std::nullopt;
                _selected_entry_index = std::nullopt;
                _should_scroll_to_entry = std::nullopt;
            }

            // Draw rows
            bool one_entry_hovered = false;
            for (const auto& entry_index : _sorted_entries)
            {
                const auto& entry = entries[entry_index];

                if (filter.has_value() && !filter.value().contains(entry_index))
                {
                    continue;
                }

                ImGui::TableNextRow();
                ImGui::PushID(entry_index);

                if (_should_scroll_to_entry.has_value()
                    && _should_scroll_to_entry.value() == entry_index)
                {
                    ImGui::SetScrollHereY();
                    _should_scroll_to_entry = std::nullopt;
                }

                for (size_t column_index = 0; column_index < _columns.size(); column_index++)
                {
                    const auto& column = _columns[column_index];

                    ImGui::TableNextColumn();
                    column.display_fn(entry);

                    if (column_index == 0)
                    {
                        ImGui::SameLine();

                        bool is_selected = _selected_entry_index.has_value()
                            && _selected_entry_index.value() == entry_index;
                        if (ImGui::Selectable(
                                "##Entry", is_selected, ImGuiSelectableFlags_SpanAllColumns))
                        {
                            _selected_entry_index = entry_index;
                        }

                        if (ImGui::IsItemHovered())
                        {
                            one_entry_hovered = true;
                            _hovered_entry_index = entry_index;
                        }
                    }
                }

                ImGui::PopID();
            }

            if (!one_entry_hovered)
            {
                _hovered_entry_index = std::nullopt;
            }

            ImGui::EndTable();
        }
    }

private:
    const char* _id;
    ImGuiTableFlags _flags;
    std::function<bool(const Entry& a, const Entry& b)> _default_sort;

    std::vector<ColumnDesc<Entry>> _columns;
    std::vector<size_t> _sorted_entries;

    std::optional<size_t> _hovered_entry_index;
    std::optional<size_t> _selected_entry_index;
    std::optional<size_t> _should_scroll_to_entry;

    void _sort_entries(std::span<const Entry> entries, ImGuiTableSortSpecs* sort_specs)
    {
        _sorted_entries.resize(entries.size());
        for (size_t i = 0; i < entries.size(); i++)
        {
            _sorted_entries[i] = i;
        }

        std::sort(
            _sorted_entries.begin(), _sorted_entries.end(),
            [&](size_t a, size_t b)
            {
                const Entry& entry_a = entries[a];
                const Entry& entry_b = entries[b];

                for (size_t i = 0; i < sort_specs->SpecsCount; i++)
                {
                    const auto& spec = sort_specs->Specs[i];
                    assert(spec.ColumnIndex < _columns.size());

                    double delta = _columns[spec.ColumnIndex].compare_fn(entry_a, entry_b);

                    if (delta != 0.0)
                    {
                        return (spec.SortDirection == ImGuiSortDirection_Ascending) ? delta < 0
                                                                                    : delta > 0;
                    }
                }

                return _default_sort(entry_a, entry_b);
            });
    }
};
} // namespace sorted_table
