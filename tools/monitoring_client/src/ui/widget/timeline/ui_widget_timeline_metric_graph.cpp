// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#include "ui/widget/timeline/ui_widget_timeline_metric_graph.h"

#include "ui/ui_colors.h"

namespace
{

using namespace ui::helpers;

static constexpr double MARKER_RADIUS = 2.0;

struct Marker
{
    lm::dvec2 position = {};
    const data::Metric* metric;
    const data::MetricUpdate<double>* update;
};

// Returns whether the marker is hovered.
bool _process_marker(
    const Marker& marker,
    const Rect& main_area,
    std::span<const data::Thread> threads)
{
    Rect button_rect = {
        {marker.position.x - MARKER_RADIUS, main_area.p0.y},
        {marker.position.x + MARKER_RADIUS, main_area.p1.y}
    };

    ImGui::SetCursorScreenPos(button_rect.p0);
    ImGui::InvisibleButton("##Marker button", button_rect.size());

    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (hovered)
    {
        ImGui::BeginTooltip();

        auto& buffer = static_fmt_memory_buffer();

        format_buffer(buffer, "Value: {}", MetricValue(marker.update->value, marker.metric->unit));
        ImGui::Text("%s", buffer.data());

        format_buffer(buffer, "at {}", (Duration)marker.update->timestamp);
        ImGui::Text("%s", buffer.data());

        ImGui::EndTooltip();
    }

    return hovered;
}

void _draw_marker(const Marker& marker, const Rect& main_area, bool hovered, double scale)
{
    auto* draw_list = ImGui::GetWindowDrawList();

    auto marker_color = (hovered) ? ui::color_set::RED.active : ui::color_set::MAGENTA.base;
    auto marker_radius = (hovered) ? MARKER_RADIUS + 3.0 : MARKER_RADIUS;

    marker_radius *= scale;

    // Darken the background to improve visibility when hovered
    if (hovered)
    {
        auto rect = Rect(
            {marker.position.x - marker_radius, main_area.p0.y},
            {marker.position.x + marker_radius, main_area.p1.x});

        draw_list->AddRectFilled(rect.p0, rect.p1, ui::color::get_hover_shadow());
    }

    if (marker_radius > 0.9)
    {
        draw_list->AddCircleFilled(marker.position, marker_radius, marker_color);
    }
}

} // namespace

namespace ui::widget
{

using namespace helpers;

MetricGraph::MetricGraph(const std::optional<data::Metric>& metric) :
    _metric(metric), _view({{0, 0}, {1, 1}}, false, true)
{
}

void MetricGraph::draw_header_column_contents(const Rect& area)
{
    const Rect trimmed_area = area.trim(ImGui::GetStyle().WindowPadding);
    ImGui::SetCursorScreenPos(trimmed_area.p0);
    if (!_metric)
    {
        ImGui::TextDisabled("No metric");
        return;
    }

    if (ImGui::BeginChild(
            "##Metric graph header", trimmed_area.size(), false, ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::PushTextWrapPos(trimmed_area.size().x);

        ImGui::Text("%s", _metric->name.c_str());
        if (!_metric->labels.empty())
        {
            ImGui::Text("%s", _metric->labels.begin()->second.c_str());
        }

        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();

        ImGui::Text("%s", _metric->name.c_str());

        ImGui::Separator();

        if (_metric->labels.empty())
        {
            ImGui::TextDisabled("No additional labels");
        }
        else
        {
            for (const auto& label : _metric->labels)
            {
                ImGui::Text("%s: %s", label.first.c_str(), label.second.c_str());
            }
        }

        ImGui::EndTooltip();
    }
}

void MetricGraph::draw(
    const view::Layout& layout,
    const data::Database& database,
    view::ViewEvents& view_events,
    context::ActionBus& action_bus)
{
    if (layout.main.size().x < 1.0 || layout.main.size().y < 1.0) return;

    _cached_threads = database.get_threads();

    const auto& metrics_system = database.get_metrics();
    if (!_metric || !metrics_system.get_updates_map().contains(*_metric))
    {
        return;
    }

    _vertical_ruler.draw_vertical(layout, _view);

    const auto& metric = _metric.value();
    const auto& updates = metrics_system.get_updates_map().at(metric);
    const double max_value = metrics_system.get_maximum(metric);

    _fit_view(max_value);
    _update_ruler_format(max_value);

    auto opt_bounds = _find_id_bounds(metrics_system);
    if (!opt_bounds.has_value())
    {
        return;
    }

    auto lower_bound_id = opt_bounds.value().lower_bound;
    auto upper_bound_id = opt_bounds.value().upper_bound;

    const double marker_count = upper_bound_id.index - lower_bound_id.index;
    const double area_pixels = layout.main.size().x;
    const double markers_per_pixel = marker_count / area_pixels;
    const double marker_fade = map(markers_per_pixel, 0.1, 0.5, 0.0, 1.0);

    // Draw the graph
    auto* draw_list = ImGui::GetWindowDrawList();
    auto main_area = layout.main;
    bool hovering_marker = false;

    auto display_marker =
        [&](const Marker& marker, const std::optional<Marker>& last_drawn_marker, size_t index)
    {
        bool hovering_current_marker = false;

        if (!hovering_marker)
        {
            ImGui::PushID(index);

            hovering_current_marker = _process_marker(marker, main_area, database.get_threads());
            hovering_marker = hovering_current_marker;

            ImGui::PopID();
        }

        if (last_drawn_marker.has_value())
        {
            draw_list->AddLine(
                last_drawn_marker.value().position, marker.position, color_set::MAGENTA.base);
        }

        _draw_marker(marker, main_area, hovering_current_marker, 1.0 - marker_fade);
    };

    Marker current_marker;
    current_marker.metric = &metric;

    std::optional<Marker> last_drawn_marker = std::nullopt;

    ImGui::PushClipRect(main_area.p0, main_area.p1, true);

    for (size_t i = lower_bound_id.index; i <= upper_bound_id.index; ++i)
    {
        const auto& update = updates[i];
        const lm::dvec2 unit_pos = {(double)update.timestamp, update.value};
        const lm::dvec2 pixel_pos = _view.unit_to_px(unit_pos, main_area);

        if (i == lower_bound_id.index)
        {
            current_marker.position = pixel_pos;
            current_marker.update = &update;
            continue;
        }

        // For every update, either:
        // - we update the value of the previous marker if it is very close (to avoid drawing too
        //   many markers at low zoom values)
        // - or we draw the previous marker and setup a new one
        if (std::abs(current_marker.position.x - pixel_pos.x)
                + std::abs(current_marker.position.y - pixel_pos.y)
            > 1.0)
        {
            display_marker(current_marker, last_drawn_marker, i);

            last_drawn_marker = current_marker;
            current_marker.position = pixel_pos;
            current_marker.update = &update;
        }
        else if (update.value > current_marker.update->value)
        {
            current_marker.update = &update;
            current_marker.position.y = pixel_pos.y;
        }
    }

    display_marker(current_marker, last_drawn_marker, upper_bound_id.index);

    ImGui::PopClipRect();
}

void MetricGraph::_fit_view(double max)
{
    const double magnitude = std::abs(max);
    const double unit_offset = 0.1 * (magnitude + 10.0);

    const auto& bounds = _view.get_bounds();
    if (bounds && bounds->p1.y < max)
    {
        _view.set_bounds(
            Rect{{bounds->p0.x, 0.0 - unit_offset}, {bounds->p1.x, max + unit_offset}});
    }

    _view.set_visible_range_y(0.0 - unit_offset, max + unit_offset);
}

void MetricGraph::_update_ruler_format(double max)
{
    if (_metric)
    {
        _vertical_ruler.unit = _metric->unit;
    }
}

std::optional<MetricGraph::Bounds> MetricGraph::_find_id_bounds(
    const data::MetricsSystem& metrics) const
{
    auto lower_bound = metrics.find_update_at(*_metric, _view.get_visible_area().p0.x);
    auto upper_bound = metrics.find_update_at(*_metric, _view.get_visible_area().p1.x);

    // If first update is inside visible area: lower bound is first update
    if (!lower_bound && upper_bound)
    {
        lower_bound = {*_metric, 0};
    }

    // Use the first update after the last visible one as upper bound instead,
    // so that a line can be drawn from the last visible marker to next.
    if (upper_bound && upper_bound->index < metrics.get_updates_map().at(*_metric).size() - 1)
    {
        upper_bound->index++;
    }

    if (!lower_bound || !upper_bound)
    {
        return std::nullopt;
    }

    Bounds bounds = {lower_bound.value(), upper_bound.value()};
    return bounds;
}

void MetricGraph::on_database_clear()
{
    _cached_threads = {};
    _metric = std::nullopt;
}

} // namespace ui::widget
