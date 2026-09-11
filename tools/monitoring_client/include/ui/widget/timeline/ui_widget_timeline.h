// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "data.h"
#include "ui/ui_context.h"
#include "ui/ui_helpers.h"
#include "ui/ui_view.h"
#include "ui/widget/timeline/ui_widget_timeline_widget.h"

namespace ui::widget
{

using TimelineWidgetStorage = std::vector<std::unique_ptr<TimelineWidget>>;

class Timeline
{
public:
    Timeline();

    void draw(const helpers::Rect& area, const data::Database&, context::ActionBus&);

    void set_focus(int64_t min, int64_t max);
    void set_focus(data::SampleId, int64_t entry, int64_t exit);

    void set_highlighted_frame(size_t frame_index) { _highlighted_frame_index = frame_index; }

    void on_database_clear();

private:
    view::View _shared_view;
    view::Ruler _ruler;

    TimelineWidgetStorage _widgets;

    bool _widget_addition_popup_open_requested;
    std::optional<size_t> _highlighted_frame_index;

    void _draw_footer(const helpers::Rect& area, const data::Database&);

    // Returns the total height used by the widgets.
    double _draw_widgets(
        const helpers::Rect& area,
        const data::Database&,
        context::ActionBus& action_bus);

    void _add_widget(std::unique_ptr<TimelineWidget> widget);
    void _widget_addition_popup(bool open, const data::Database&);

    void _scale_indicator(const lm::dvec2& position, double width, const data::Database&);
};

} // namespace ui::widget
