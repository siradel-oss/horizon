// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "data.h"
#include "ui/ui_context.h"
#include "ui/ui_helpers.h"

#include <imgui.h>

namespace ui::widget
{

class SampleInspector
{
public:
    void draw(const helpers::Rect& area, const data::Database&, context::ActionBus&);

    void focus_on_sample(uint64_t record_hash, data::SampleId);

    void on_database_clear();

private:
    std::vector<uint64_t> _sorted_records;
    std::vector<data::SampleId> _sorted_samples;

    std::optional<uint64_t> _selected_record_hash;
    std::optional<data::SampleId> _selected_sample_id;

    bool _different_record_selected = false;
    bool _set_scroll_to_selected = false;

    float _inspector_width = 440.0F;

    std::vector<int64_t> _thread_total_durations;

    static constexpr float SEPARATOR_INTERACT_WIDTH = 6.0F;

    enum class RecordColumnId
    {
        SampleName,
        TotalInclusive,
        TotalExclusive,
        PercentageInclusive,
        PercentageExclusive,
        AverageInclusive,
        AverageExclusive
    };

    enum class SampleColumnId
    {
        Parent,
        Timestamp,
        TimeInclusive,
        TimeExclusive,
        PercentageInclusive,
        PercentageExclusive,
        Thread
    };

    bool _setup_record_table() const; // returns true if inside the table
    bool _setup_sample_table() const;

    void _draw_record_table_row(
        uint64_t record_hash,
        const hrz::flat_hash_map<uint64_t, data::SampleRecord>& records);
    void _draw_sample_table_row(
        const data::SampleId&,
        const data::SampleSystem&,
        std::span<const data::Thread>,
        context::ActionBus&);

    void _draw_inspector(
        uint64_t record_hash,
        const data::SampleSystem&,
        std::span<const data::Thread>,
        context::ActionBus&);
    void _draw_separator_line();

    void _extract_sorted_records(
        const hrz::flat_hash_map<uint64_t, data::SampleRecord>& records,
        const ImGuiTableSortSpecs*);
    void _sort_and_insert_samples(
        std::span<const data::SampleId>,
        const data::SampleSystem&,
        const ImGuiTableSortSpecs*);

    bool _compare_records(
        const data::SampleRecord&,
        const data::SampleRecord&,
        const ImGuiTableSortSpecs*) const;
    bool _compare_samples(
        const data::Sample&,
        const data::Sample&,
        const data::SampleSystem&,
        const ImGuiTableSortSpecs*) const;

    double _sample_parent_duration(const data::SampleId&, const data::SampleSystem&) const;
};

} // namespace ui::widget
