// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "data.h"
#include "ui/widget/timeline/ui_widget_timeline_widget.h"

namespace ui::widget
{

class MetricGraph : public TimelineWidget
{
public:
    MetricGraph(const std::optional<data::Metric>& metric);

public:
    void draw(const view::Layout&, const data::Database&, view::ViewEvents&, context::ActionBus&)
        override;

    void draw_header_column_contents(const helpers::Rect& area) override;

    const char* get_name() const override { return "Metric graph"; }

    double get_preferred_height() const override { return _preferred_height; }

    bool can_resize() const override { return true; }

    void on_resize(double size) override { _preferred_height = size; }

    double get_vertical_ruler_preferred_width(double area_height) override
    {
        return _vertical_ruler.get_preferred_width_for_vertical(_view, area_height);
    };

    void set_time_range(double min, double max) override { _view.set_visible_range_x(min, max); }

    void on_database_clear() override;

private:
    std::optional<data::Metric> _metric;
    std::span<const data::Thread> _cached_threads;

    view::View _view;
    view::Ruler _vertical_ruler;

    double _preferred_height = 64.0;

    void _fit_view(double max);
    void _update_ruler_format(double max);

    struct Bounds
    {
        data::MetricUpdateId lower_bound;
        data::MetricUpdateId upper_bound;
    };

    std::optional<Bounds> _find_id_bounds(const data::MetricsSystem&) const;
};

} // namespace ui::widget
