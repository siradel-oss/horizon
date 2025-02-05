#pragma once

#include "data.h"
#include "ui/widget/timeline/ui_widget_timeline_widget.h"

namespace ui::widget
{
class SnapshotGraph : public TimelineWidget
{
public:
    void draw(const view::Layout&, const data::Database&, view::ViewEvents&, context::ActionBus&)
        override;

    void draw_header_column_contents(const helpers::Rect& area) override;

    const char* get_name() const override { return "Snapshot graph"; }

    double get_preferred_height() const override { return 0.0; }

    void set_time_range(double min, double max) override { _view.set_visible_range_x(min, max); }

private:
    view::View _view;

    size_t _cached_gpu_snapshot_count = 0;
    size_t _cached_blob_snapshot_count = 0;
};

} // namespace ui::widget
