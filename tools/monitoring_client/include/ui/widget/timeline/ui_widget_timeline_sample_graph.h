// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "data.h"
#include "ui/widget/timeline/ui_widget_timeline_widget.h"

namespace ui::widget
{

struct SampleColors
{
    uint32_t fill;
    uint32_t border;
    uint32_t aggregate;
};

using SampleColorMap = hrz::flat_hash_map<std::string, SampleColors>;

struct SampleGraphThread
{
    std::string name;

    bool visible;
    double current_size;
    double target_size;
    size_t max_depth;
};

class SampleGraph : public TimelineWidget
{
public:
    void draw(const view::Layout&, const data::Database&, view::ViewEvents&, context::ActionBus&)
        override;

    void draw_header_column_contents(const helpers::Rect& area) override;

    const char* get_name() const override { return "Sample graph"; }

    double get_preferred_height() const override;

    bool can_resize() const override { return true; }

    void on_resize(double size) override;

    void set_time_range(double min, double max) override { _view.set_visible_range_x(min, max); }

    void on_database_clear() override;
    void focus_on_sample(data::SampleId) override;

private:
    view::View _view;

    bool _synchronize = true;
    double _last_synchronization_timestamp = 0.0;

    std::optional<uint32_t> _view_context_menu_thread_id;
    data::SampleId _sample_context_menu_sample_id;

    double _current_total_size = 154.0;
    double _target_total_size = 154.0;

    std::vector<SampleGraphThread> _threads;
    SampleColorMap _sample_colors;

    void _view_context_menu(bool open, const data::SampleSystem&, view::ViewEvents&);
    void _sample_context_menu(bool open, const data::SampleSystem&, context::ActionBus&);

    void _sample_tree_popup(bool open, const data::SampleSystem&, const data::SampleId);
    void _sample_color_popup(bool open, const data::SampleSystem&, const data::SampleId);

    void _handle_automatic_scrolling(const data::SampleSystem&, view::ViewEvents&);
};

} // namespace ui::widget
