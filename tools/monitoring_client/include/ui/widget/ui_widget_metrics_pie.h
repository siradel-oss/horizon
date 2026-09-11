// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "data.h"
#include "ui/ui_context.h"
#include "ui/ui_helpers.h"
#include "ui/ui_view.h"

namespace ui::widget
{

class MetricsPie
{
public:
    MetricsPie();

    void draw(const helpers::Rect& area, const data::Database&, context::ActionBus&);

    void select_frame(size_t index)
    {
        _selected_frame = index;
        _update_slices = true;
    }

    void on_database_clear();

    enum class DisplayMode
    {
        PieChart,
        Table
    };

    DisplayMode display_mode = DisplayMode::PieChart;

private:
    view::View _view;

    bool _show_labels = true;
    bool _update_slices = true;

    std::vector<data::Metric> _selected_metrics;
    std::vector<bool> _selection_vector;

    enum class ColumnId
    {
        Label,
        Value
    };

    struct Slice
    {
        double value;
        std::string label;
        uint32_t color;
        data::MetricUpdateId update_id;
    };

    std::vector<Slice> _slices;
    size_t _total_duration = 0;
    size_t _selected_frame = 0;

    void _draw_pie(const helpers::Rect& area);
    void _draw_table(const helpers::Rect& area, bool sort_scheduled);

    // Returns the remaining area
    helpers::Rect _footer(const helpers::Rect& available_area, const data::Database&);

    void _slice_tooltip(const Slice& slice) const;

    std::vector<Slice> _compute_slices(const data::Database& database, int64_t timestamp) const;
    void _sort_slices(const ImGuiTableSortSpecs*);
};

} // namespace ui::widget
