#pragma once

#include "data.h"
#include "ui/ui_context.h"
#include "ui/ui_helpers.h"
#include "ui/ui_view.h"

namespace ui::widget
{
class TimelineWidget
{
public:
    // Mandatory methods
    virtual void draw(
        const view::Layout&,
        const data::Database&,
        view::ViewEvents&,
        context::ActionBus&) = 0;

    virtual const char* get_name() const = 0;

    virtual double get_preferred_height() const = 0;
    virtual void set_time_range(double min, double max) = 0;

    // Optional methods
    virtual bool can_resize() const { return false; }

    virtual void on_resize(double vertical_size) {}

    virtual double get_vertical_ruler_preferred_width(double area_height) { return 0.0; }

    // There is no boundaries or clipping rect set when this function is called.
    // It is up to the widget to handle clipping / spacing / etc...
    // The given area is basically just a suggestion.
    virtual void draw_header_column_contents(const helpers::Rect& area) {}

    virtual void on_database_clear() {}

    virtual void focus_on_sample(data::SampleId) {}

    virtual ~TimelineWidget() = default;
};

} // namespace ui::widget
