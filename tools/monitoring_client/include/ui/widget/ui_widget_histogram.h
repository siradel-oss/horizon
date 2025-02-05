#pragma once

#include "data.h"
#include "ui/ui_context.h"
#include "ui/ui_helpers.h"
#include "ui/ui_view.h"

namespace ui::widget
{
// Because of the limitations of view::Ruler, which has to represent a uniform range
// of finite size, we use a custom ruler drawing logic for the horizontal ruler of an
// histogram.
struct HistogramRuler
{
    std::vector<double> graduations;

    void draw_horizontal(const view::Layout&);
};

class Histogram
{
public:
    Histogram();

    void draw(const helpers::Rect& area, const data::Database&, context::ActionBus&);

    void set_metric(const std::optional<data::Metric>& metric);

private:
    std::optional<data::Metric> _selected_metric;

    view::View _view;
    HistogramRuler _horizontal_ruler;
    view::Ruler _vertical_ruler;

    bool _refresh_horizontal_graduations = false;
};

} // namespace ui::widget
