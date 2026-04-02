#include "ui/widget/ui_widget_metrics_pie.h"

#include "hrz/fnd/flat_hash_set.h"
#include "ui/ui_colors.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace
{

double _value_to_angle(double value)
{
    return value * 2.0 * lm::PI;
}

double _angle_to_value(double angle)
{
    return angle / (2.0 * lm::PI);
}

// Returns the center of the drawn slice, useful for drawing text above the slice
lm::dvec2 _draw_pie_slice(
    ImDrawList* draw_list,
    lm::dvec2 center,
    double radius,
    double a,
    double b,
    uint32_t color)
{
    const bool perfect_circle = (b - a > 0.999);

    static constexpr float MIN_SEGMENT_COUNT = 16.0F;
    static constexpr float MAX_SEGMENT_COUNT = 128.0F;
    static constexpr double MAX_SEGMENT_RADIUS = 800.0;

    const size_t full_segment_count = std::ceil(
        ui::helpers::lerp(
            MIN_SEGMENT_COUNT, MAX_SEGMENT_COUNT, std::min(radius / MAX_SEGMENT_RADIUS, 1.0)));
    const size_t segment_count = std::ceil(full_segment_count * (b - a));

    const auto line_col = ui::color::multiply(color, 0.8);

    lm::dvec2 text_center = center;
    if (!perfect_circle)
    {
        std::vector<ImVec2> points = {center};

        for (size_t i = 0; i < segment_count + 1; ++i)
        {
            double value = a + (b - a) * ((double)i / segment_count);
            double angle = _value_to_angle(value);
            auto to_point = lm::dvec2{std::cos(angle), std::sin(angle)} * radius;
            points.push_back(center + to_point);
        }

        draw_list->AddConvexPolyFilled(points.data(), points.size(), color);
        draw_list->AddPolyline(points.data(), points.size(), line_col, ImDrawFlags_Closed, 1.0);

        double mid_angle = _value_to_angle((a + b) / 2.0);
        text_center = center + lm::dvec2(std::cos(mid_angle), std::sin(mid_angle)) * radius * 0.58;
    }

    else
    {
        draw_list->AddCircleFilled(center, radius, color, segment_count);
        draw_list->AddCircle(center, radius, line_col, segment_count);
    }

    return text_center;
}

} // namespace

namespace ui::widget
{

using namespace helpers;

MetricsPie::MetricsPie()
{
    _view.set_bounds(Rect{{0.0, 0.0}, {1.0, 1.0}});
    _view.set_zoom_bounds(Rect{{1.0, 1.0}, {10.0, 10.0}});
}

void MetricsPie::draw(
    const Rect& area,
    const data::Database& database,
    context::ActionBus& action_bus)
{
    if (database.get_frames().empty())
    {
        ImGui::TextDisabled("No frame information has been received yet.");
        return;
    }

    bool sort_scheduled = false;
    if (_slices.empty() || _update_slices)
    {
        _update_slices = false;
        _slices = _compute_slices(database, database.get_frames()[_selected_frame].end);

        _total_duration = 0;
        for (const auto& slice : _slices)
        {
            _total_duration += slice.value;
        }

        if (!_slices.empty()) sort_scheduled = true;
    }

    if (ImGui::BeginTabBar("Visualization mode"))
    {
        if (ImGui::BeginTabItem("Pie"))
        {
            display_mode = DisplayMode::PieChart;
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Table"))
        {
            display_mode = DisplayMode::Table;
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    bool open_metrics_selector = ImGui::Button("Change metrics...");

    ImGui::SameLine();
    if (_selected_metrics.empty())
        ImGui::TextDisabled("No metrics selected");
    else
        ImGui::Text("%zu metrics selected", _selected_metrics.size());

    Rect main_area = {ImGui::GetCursorScreenPos(), area.p1};
    main_area = _footer(main_area, database);

    if (_slices.empty())
    {
        lm::dvec2 center = (main_area.p0 + main_area.p1) / 2.0;
        draw_text_centered(
            ImGui::GetWindowDrawList(), center,
            (ImColor)ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
            "No values to display for this frame.");
    }
    else
    {
        if (display_mode == DisplayMode::PieChart)
            _draw_pie(main_area);
        else if (display_mode == DisplayMode::Table)
            _draw_table(main_area, sort_scheduled);
    }

    const auto& all_metrics = database.get_metrics().get_known_metrics();
    _selection_vector.resize(all_metrics.size());
    bool change = filtered_metric_multiselector(
        "Select a set of metrics to compare", open_metrics_selector, all_metrics,
        database.get_threads(), _selection_vector, true);

    if (change)
    {
        _update_slices = true;
        _selected_metrics.clear();
        for (size_t i = 0; i < _selection_vector.size(); ++i)
        {
            if (_selection_vector[i]) _selected_metrics.push_back(all_metrics[i]);
        }
    }
}

void MetricsPie::on_database_clear()
{
    _selected_frame = 0;
    _selected_metrics.clear();
    _selection_vector.clear();
    _update_slices = true;
}

std::vector<MetricsPie::Slice> MetricsPie::_compute_slices(
    const data::Database& database,
    int64_t timestamp) const
{
    std::vector<Slice> result;
    const auto& system = database.get_metrics();

    // Compute the list of prohibited names: no two slices should have the same name.
    hrz::flat_hash_set<std::string> found_names;
    hrz::flat_hash_set<std::string> prohibited_names;
    for (const auto& metric : _selected_metrics)
    {
        const auto& name = metric.name;
        if (found_names.contains(name))
            prohibited_names.insert(name);
        else
            found_names.insert(name);

        for (auto it = metric.labels.begin(); it != metric.labels.end(); ++it)
        {
            const auto& label_value = it->second;
            if (found_names.contains(it->second))
                prohibited_names.insert(label_value);
            else
                found_names.insert(label_value);
        }
    }

    for (const auto& metric : _selected_metrics)
    {
        const auto update_id = system.find_update_at(metric, timestamp);
        if (update_id)
        {
            Slice slice = {};
            slice.value = system.get_update(*update_id).value;
            slice.update_id = *update_id;

            // Compute the slice's name to be displayed by using either the name of its
            // corresponding metrics, or one of its label values if it was flagged as prohibited
            if (prohibited_names.contains(metric.name))
            {
                for (auto it = metric.labels.begin(); it != metric.labels.end(); ++it)
                {
                    if (!prohibited_names.contains(it->second))
                    {
                        slice.label = it->second;
                        break;
                    }
                }
            }
            else
            {
                slice.label = metric.name;
            }

            if (slice.label.empty()) slice.label = metric.display_string;

            slice.color = color::from_string(slice.label, 0.50F, 0.90F);
            result.push_back(slice);
        }
    }

    return result;
}

void MetricsPie::_sort_slices(const ImGuiTableSortSpecs* sort_specs)
{
    auto compare = [&](const Slice& a, const Slice& b)
    {
        size_t spec_count = (sort_specs) ? sort_specs->SpecsCount : 0;
        for (size_t n = 0; n < spec_count; ++n)
        {
            const auto& spec = sort_specs->Specs[n];
            double delta = 0.0;
            switch (spec.ColumnUserID)
            {
                case (ImGuiID)ColumnId::Value: delta = a.value - b.value; break;

                case (ImGuiID)ColumnId::Label:
                    delta = strcmp(a.label.c_str(), b.label.c_str());
                    break;

                default:;
            }

            if (delta != 0.0)
            {
                return (spec.SortDirection == ImGuiSortDirection_Ascending) ? delta < 0 : delta > 0;
            }
        }

        return a.label < b.label;
    };

    std::sort(_slices.begin(), _slices.end(), compare);
}

void MetricsPie::_draw_pie(const Rect& area)
{
    lm::dvec2 default_center = (area.p0 + area.p1) / 2.0;
    double default_radius = std::min(area.size().x, area.size().y) / 2.0 - 8.0;

    auto* draw_list = ImGui::GetWindowDrawList();

    double total = 0.0;
    for (const auto& slice : _slices)
    {
        total += slice.value;
    }

    if (total < 0.001)
    {
        draw_text_centered(
            draw_list, default_center, ImGui::GetColorU32(ImGuiCol_TextDisabled),
            "All metric values are null.");
        return;
    }

    auto layout = view::make_layout(area);
    auto events = _view.catch_events(layout.main);
    _view.process_events(events);
    _view.process_smoothing();

    lm::dvec2 virtual_center = _view.unit_to_px(lm::dvec2{0.5, 0.5}, layout.main);
    double virtual_radius = default_radius * _view.get_current_zoom().x;

    bool hovered = false;
    double hover_value = 0.0;

    if (area.size().x > 1.0 && area.size().y > 1.0)
    {
        ImGui::SetCursorScreenPos(area.p0);
        ImGui::InvisibleButton("Pie interaction", area.size());

        if (ImGui::IsItemHovered())
        {
            lm::dvec2 mouse_pos = ImGui::GetIO().MousePos;
            lm::dvec2 to_mouse = mouse_pos - virtual_center;
            if (lm::length(to_mouse) <= virtual_radius)
            {
                hovered = true;
                // We want an angle value in [0, 2pi] instead of [-pi; pi]
                double hover_angle = std::atan2(-to_mouse.y, -to_mouse.x) + lm::PI;
                hover_value = _angle_to_value(hover_angle);
            }
        }
    }

    draw_list->PushClipRect(area.p0, area.p1);
    double progress = 0.0;
    std::vector<lm::dvec2> text_centers(_slices.size());
    for (size_t i = 0; i < _slices.size(); ++i)
    {
        const auto& slice = _slices[i];

        double a = progress;
        double b = a + slice.value / total;

        bool hovered_slice = (hovered && hover_value > a && hover_value <= b);
        if (hovered_slice) _slice_tooltip(slice);

        uint32_t color = slice.color;
        if (hovered_slice)
            color = color::multiply(color, 1.2);
        else if (hovered)
            color = color::with_alpha(color, 0.2);

        text_centers[i] = _draw_pie_slice(draw_list, virtual_center, virtual_radius, a, b, color);
        progress = b;
    }

    // Draw the text, after all of the slices so it's not covered
    if (_show_labels)
    {
        const auto text_color = ImGui::GetColorU32(ImGuiCol_Text);
        for (size_t i = 0; i < text_centers.size(); ++i)
        {
            const auto& slice = _slices[i];
            // Rough way of discarding smaller slices to avoid cluttering the pie
            // with overlapped text. A cleaner way that adapts to the available
            // screen space would work better
            if (slice.value / total < 0.025) continue;

            draw_text_centered(draw_list, text_centers[i], text_color, "{}", slice.label.c_str());
        }
    }
    draw_list->PopClipRect();
}

void MetricsPie::_draw_table(const Rect& area, bool sort_scheduled)
{
    ImGui::SetCursorScreenPos(area.p0);
    if (ImGui::BeginChild("Metric list", area.size()))
    {
        ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit
            | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable
            | ImGuiTableFlags_NoBordersInBody;
        if (ImGui::BeginTable("Table", 2, flags))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(
                "Metric", ImGuiTableColumnFlags_WidthStretch, 0.0F, (ImGuiID)ColumnId::Label);
            ImGui::TableSetupColumn("Value", 0, 100.0, (ImGuiID)ColumnId::Value);
            ImGui::TableHeadersRow();

            auto* sort_specs = ImGui::TableGetSortSpecs();
            if (sort_scheduled || (sort_specs && sort_specs->SpecsDirty))
            {
                _sort_slices(sort_specs);

                if (sort_specs) sort_specs->SpecsDirty = false;
            }

            for (const auto& slice : _slices)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                ImGui::Selectable(slice.label.c_str(), false, 0);
                if (ImGui::IsItemHovered()) _slice_tooltip(slice);
                ImGui::TableNextColumn();

                auto& buffer = static_fmt_memory_buffer();
                format_buffer(buffer, "{}", MetricValue(slice.value, slice.update_id.metric.unit));
                ImGui::Text("%s", buffer.data());
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

Rect MetricsPie::_footer(const Rect& available_area, const data::Database& database)
{
    const double font_size = ImGui::GetFontSize();
    const lm::dvec2 spacing = ImGui::GetStyle().ItemSpacing;

    lm::dvec2 cursor = {available_area.p0.x, available_area.p1.y};
    cursor.y -= font_size;
    ImGui::SetCursorScreenPos(cursor);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0F, 0.0F));
    const size_t slider_min = 0;
    const size_t slider_max = database.get_frames().size() - 1;

    help_marker("Right click > \"GPU Passes\" on a frame in the frame graph.");
    ImGui::SameLine();
    ImGui::Text("Selected frame:");
    ImGui::SameLine();

    ImGui::PushButtonRepeat(true);
    if (ImGui::ArrowButton("Previous frame", ImGuiDir_Left))
    {
        _selected_frame -= (_selected_frame > slider_min) ? 1 : 0;
        _update_slices = true;
    }
    ImGui::SameLine(0.0F, 2.0F);
    if (ImGui::ArrowButton("Next frame", ImGuiDir_Right))
    {
        _selected_frame += (_selected_frame < slider_max) ? 1 : 0;
        _update_slices = true;
    }
    ImGui::PopButtonRepeat();

    ImGui::SameLine();
    ImGui::PushItemWidth(120.0F);

    std::string slider_label =
        fmt::format("{}###Frame", (Duration)database.get_frames()[_selected_frame].begin);
    std::string slider_format = fmt::format("%zu / {}", slider_max);
    if (ImGui::DragScalar(
            slider_label.c_str(), ImGuiDataType_U64, &_selected_frame, 1.0F, &slider_min,
            &slider_max, slider_format.c_str(), ImGuiSliderFlags_AlwaysClamp))
    {
        _update_slices = true;
    }

    ImGui::PopItemWidth();

    cursor.y -= font_size + spacing.y;
    ImGui::SetCursorScreenPos(cursor);

    ImGui::Text("Total:");
    ImGui::SameLine();
    if (_selected_metrics.empty())
    {
        ImGui::TextDisabled("-");
    }
    else
    {
        auto& buffer = static_fmt_memory_buffer();
        format_buffer(buffer, "{}", MetricValue(_total_duration, _selected_metrics.front().unit));

        ImGui::Text("%s", buffer.data());
    }

    if (display_mode == DisplayMode::PieChart)
    {
        ImGui::SameLine(220.0F);
        ImGui::Checkbox("Show labels", &_show_labels);
    }

    ImGui::PopStyleVar();

    double total_height = available_area.p1.y - cursor.y;
    return available_area.trim(Direction::Down, total_height + spacing.y);
}

void MetricsPie::_slice_tooltip(const Slice& slice) const
{
    ImGui::BeginTooltip();

    ImGui::Text("%s", slice.update_id.metric.name.c_str());
    for (const auto& label : slice.update_id.metric.labels)
    {
        ImGui::Text("%s: %s", label.first.c_str(), label.second.c_str());
    }

    auto& buffer = static_fmt_memory_buffer();
    format_buffer(buffer, "{}", MetricValue(slice.value, slice.update_id.metric.unit));

    ImGui::Separator();
    ImGui::Text("%s", buffer.data());

    ImGui::EndTooltip();
}

} // namespace ui::widget
