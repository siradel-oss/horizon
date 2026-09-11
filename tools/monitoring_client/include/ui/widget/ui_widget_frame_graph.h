// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "data.h"
#include "ui/ui_context.h"
#include "ui/ui_helpers.h"
#include "ui/ui_view.h"

namespace ui::widget
{

class FrameGraph
{
public:
    FrameGraph();

    void draw(const helpers::Rect& area, const data::Database&, context::ActionBus&);

    void on_database_clear();

    void set_highlighted_frame(size_t frame_index) { _highlighted_frame = frame_index; }

private:
    view::View _view;
    view::Ruler _horizontal_ruler;
    view::Ruler _vertical_ruler;

    std::optional<data::Metric> _selected_metric = std::nullopt;
    std::optional<size_t> _highlighted_frame = std::nullopt;

    double _time_axis_max_value = 30'000.0;
    size_t _context_menu_frame_index = 0;

    bool _show_visual_frames = true;
    bool _show_picking_frames = true;
    bool _show_planet_feedback_frames = true;
    bool _show_renderless_frames = false;
};

} // namespace ui::widget
