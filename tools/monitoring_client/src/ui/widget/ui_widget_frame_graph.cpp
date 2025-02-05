#include "ui/widget/ui_widget_frame_graph.h"

#include "ui/ui_colors.h"

#include <imgui_internal.h>

#include <cmath>
#include <limits>

namespace
{
using namespace ui::helpers;

uint32_t _get_frame_color(int render_types)
{
    static constexpr uint32_t colors[] = {
        0xffddaadd, // 0 : no render
        0xff2211ff, // 1 : visual
        0xff22bb11, // 2 : picking
        0xff22ddee, // 3 : visual & picking
        0xffee7777, // 4 : planet_feedback
        0xffff22dd, // 5 : planet_feedback & visual
        0xffdddd55, // 6 : planet_feedback & picking
        0xffffdddd, // 7 : all
    };

    return (render_types >= 0 && render_types < IM_ARRAYSIZE(colors)) ? colors[render_types] : 0;
}

std::unique_ptr<ui::context::Action> _focus_on_frame_action(
    size_t frame_index,
    const data::Database& database)
{
    const auto& frame = database.get_frames()[frame_index];
    if (frame_index + 1 < database.get_frames().size())
    {
        const auto& next_frame = database.get_frames()[frame_index + 1];
        return ui::context::Action::focus_in_timeline(frame.begin, next_frame.begin);
    }
    else
    {
        return ui::context::Action::focus_in_timeline(frame.begin, frame.end);
    }
}

struct FrameFilter
{
    const char* name;
    std::function<bool(const data::Frame&)> function;

    gsl::not_null<bool*> p_active;

    bool pass(const data::Frame& frame) const { return function(frame); }
};

struct FilterDrawResult
{
    Rect view_area;
    std::vector<Rect> filter_areas;
};

// Draws all rows corresponding to the given filters, from bottom to top.
// Returns the area where ***THE VIEW*** of the framegraph should be drawn.
FilterDrawResult _draw_filter_rows(
    ImDrawList* draw_list,
    const Rect& area,
    gsl::span<FrameFilter> filters)
{
    const double font_size = ImGui::GetFontSize();
    const auto spacing = ImGui::GetStyle().ItemSpacing;
    const auto inner_spacing = ImGui::GetStyle().ItemInnerSpacing;

    // You should calc the max of the text sizes
    const double label_width = ImGui::CalcTextSize("Planet Feedback").x;

    Rect sub_area = area.subrect(Direction::Down, filters.size() * font_size + spacing.x);
    lm::dvec2 position = {sub_area.p0.x, sub_area.p1.y};

    FilterDrawResult res;
    res.filter_areas.resize(filters.size());
    double view_area_begin = area.p0.x;

    for (int i = filters.size() - 1; i >= 0; --i)
    {
        auto& filter = filters[i];

        // Advance cursor
        position.x = area.p0.x;
        position.y -= font_size + inner_spacing.y;

        // Draw text
        position.x += label_width;
        uint32_t text_color = (*filter.p_active) ? ImGui::GetColorU32(ImGuiCol_Text)
                                                 : ImGui::GetColorU32(ImGuiCol_TextDisabled);
        ui::helpers::draw_text_right_aligned(draw_list, position, text_color, filter.name);

        // Draw checkbox
        position.x += spacing.x;
        ImGui::SetCursorScreenPos(position);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
        ImGui::PushID(filter.name);
        ImGui::Checkbox("##Filter checkbox", filter.p_active);
        ImGui::PopID();
        ImGui::PopStyleVar();

        // Draw row bg
        ImGui::SameLine();

        position.x = ImGui::GetCursorScreenPos().x;
        Rect row_bg_area = {position, position + lm::dvec2(available_rect().size().x, font_size)};

        uint32_t row_bg_color;
        const auto window_color = ImGui::GetColorU32(ImGuiCol_WindowBg);
        const auto bg_color = ui::view::get_default_background_color();

        if (*filter.p_active)
            row_bg_color = ui::color::mix(window_color, bg_color, 0.5);
        else
            row_bg_color = ui::color::mix(window_color, bg_color, 0.2);

        draw_list->AddRectFilled(row_bg_area.p0, row_bg_area.p1, row_bg_color);
        res.filter_areas[i] = row_bg_area;

        view_area_begin = row_bg_area.p0.x;
    }

    res.view_area = Rect({view_area_begin, area.p0.y}, {area.p1.x, position.y});
    return res;
}

struct FrameRender
{
    double value;
    double min;
    bool opaque;

    uint32_t frame_color;
    uint32_t marker_color; // Frame and marker colors can differ when merging frame
                           // (markers are drawn in the filter rows below the frame graph)
};

std::vector<FrameRender> _compute_frames_render(
    size_t from,
    size_t count,
    std::optional<data::Metric> metric,
    gsl::span<const data::Frame> frames,
    gsl::span<const FrameFilter> filters,
    const data::MetricsSystem& metrics)
{
    count = std::min(count, frames.size() - from);

    std::vector<FrameRender> result(count);

    for (size_t i = 0; i < count; ++i)
    {
        const auto& frame = frames[from + i];

        for (const auto& filter : filters)
        {
            if (*filter.p_active && filter.pass(frame))
            {
                result[i].opaque = true;
                break;
            }
        }

        if (!metric) result[i].value = frame.end - frame.begin;

        result[i].min = result[i].value;
        result[i].frame_color = _get_frame_color(frame.render_types);
        result[i].marker_color = result[i].frame_color;
    }

    if (!metric) return result;

    // If the values of the frame should be determined using a metric for reference,
    // computing the values require more work as metric values are stored as updates

    // The next two timestamp represent the two extremities delimiting the area where
    // we should look for metric updates
    int64_t first_timestamp = frames[from].begin;
    int64_t last_timestamp = frames[from + count - 1].end;
    if (from + count < frames.size()) last_timestamp = frames[from + count].begin;

    // First, find the first metric update that defines the value of the metric for the visible
    // frames
    auto initial_update_id = metrics.find_update_at(*metric, first_timestamp);

    double current_value = 0;
    const data::MetricUpdate<double>* next_update;

    if (!initial_update_id)
    {
        current_value = 0;
        next_update = &metrics.get_update({*metric, 0});
    }
    else
    {
        current_value = metrics.get_update(*initial_update_id).value;
        next_update = &metrics.get_update(*initial_update_id) + 1;
    }

    // Now, go through all of the VISIBLE frames, and register the metric value for this
    // frame. We skip this if the first update appears after the visible area, since there
    // is no value to show
    if (initial_update_id || next_update->timestamp < last_timestamp)
    {
        const auto& updates = metrics.get_updates_map().at(*metric);
        const auto* end_ptr = &(updates.back()) + 1;

        int64_t frame_timestamp = 0;
        std::optional<double> opt_frame_max_value = std::nullopt;

        for (size_t i = from; i < from + count; ++i)
        {
            frame_timestamp = (i + 1 < frames.size()) ? frames[i + 1].begin : frames[i].end;
            opt_frame_max_value = std::nullopt;

            while (next_update != end_ptr && next_update->timestamp < frame_timestamp)
            {
                current_value = next_update->value;
                next_update++;

                if (!opt_frame_max_value.has_value() || current_value > opt_frame_max_value.value())
                {
                    opt_frame_max_value = current_value;
                }
            }

            // If there has been one or more updates during the same frame, we want to display
            // the biggest value out of all of them.
            // If there has been no update during a frame, we want to display the value of the
            // previous frame.
            result[i - from].value = opt_frame_max_value.value_or(current_value);
            result[i - from].min = result[i - from].value;
        }
    }

    return result;
}

FrameRender _get_merged_frames_render(gsl::span<const FrameRender> frames)
{
    FrameRender result = {};

    result.value = 0.0;
    result.marker_color = frames[0].marker_color;
    std::optional<double> min = std::nullopt;
    for (const auto& frame : frames)
    {
        result.opaque |= frame.opaque;
        if (frame.opaque && frame.value > result.value)
        {
            result.frame_color = frame.frame_color;
            result.value = frame.value;
        }
        else if (frame.opaque && ((min && frame.value < *min) || !min))
        {
            min = frame.value;
        }
    }

    result.min = min.value_or(result.value);
    return result;
}
} // namespace

namespace ui::widget
{
using namespace helpers;

FrameGraph::FrameGraph() : _view({{0.0, 0.0}, {120.0, 75.0}}, false, true)
{
    Rect bounds = {{0.0, 0.0}, {std::numeric_limits<double>::max(), _view.get_visible_area().p1.y}};
    _view.set_bounds(bounds);
    Rect zoom_bounds = {{0.01, 1.0}, {10.0, 1.0}};
    _view.set_zoom_bounds(zoom_bounds);

    _horizontal_ruler.unit = data::MetricUnit::None;
    _horizontal_ruler.special_formatter = [](fmt::memory_buffer& buffer, MetricValue value)
    {
        // The horizontal ruler represents frame indices, so we want no visible decimals.
        format_buffer(buffer, "{:.0Lf}", value.value);
    };

    _vertical_ruler.unit = data::MetricUnit::Microsecond;
    _vertical_ruler.special_formatter = [](fmt::memory_buffer& buffer, MetricValue value)
    {
        if (value.unit == data::MetricUnit::Microsecond)
        {
            // Force ms display with no decimals to avoid taking preserve space.
            format_buffer(buffer, "{:.0Lf} ms", value.value / 1000.0);
        }
        else
        {
            format_buffer(buffer, "{}", value);
        }
    };
}

void FrameGraph::on_database_clear()
{
    _selected_metric = std::nullopt;
}

void FrameGraph::draw(
    const Rect& area,
    const data::Database& database,
    context::ActionBus& action_bus)
{
    auto* draw_list = ImGui::GetWindowDrawList();
    fmt::memory_buffer buffer;

    std::vector<FrameFilter> filters = {
        {"Visual", [](const data::Frame& f) { return f.render_types & data::Frame::VisualRender; },
         &_show_visual_frames},
        {"Picking",
         [](const data::Frame& f) { return f.render_types & data::Frame::PickingRender; },
         &_show_picking_frames},
        {"Planet Feedback",
         [](const data::Frame& f) { return f.render_types & data::Frame::PlanetFeedbackRender; },
         &_show_planet_feedback_frames},
        {"No Render", [](const data::Frame& f) { return f.render_types == 0; },
         &_show_renderless_frames},
    };

    auto res = _draw_filter_rows(draw_list, area, filters);
    Rect& view_area = res.view_area;
    auto& filter_areas = res.filter_areas;

    // Custom layout to match the view rect returned previously
    view::Layout layout;
    view_area.split(
        Direction::Down, view::Layout::RULER_HEIGHT, &layout.horizontal_ruler, &layout.main);
    layout.vertical_ruler = layout.main.offset(-view::Layout::RULER_WIDTH, 0.0)
                                .subrect(Direction::Left, view::Layout::RULER_WIDTH);

    // Vertical bounds slider: exclusively for time metrics
    if (!_selected_metric || _selected_metric->unit == data::MetricUnit::Microsecond)
    {
        Rect slider_rect =
            layout.vertical_ruler.offset(-22.0, 0.0)
                .subrect(Direction::Left, 16.0)
                .trim(Direction::Up, ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y);

        ImGui::SetCursorScreenPos(slider_rect.p0);
        static constexpr double min_time_axis_max_value = 5'000.0;
        static constexpr double max_time_axis_max_value = 60'000.0;
        ImGui::VSliderScalar(
            "##Bound slider", slider_rect.size(), ImGuiDataType_Double, &_time_axis_max_value,
            &min_time_axis_max_value, &max_time_axis_max_value, "");

        // Bounds text indicator, above slider
        lm::dvec2 indicator_pos = {slider_rect.p1.x, layout.vertical_ruler.p0.y};
        draw_text_right_aligned(
            draw_list, indicator_pos, (ImColor)ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
            "{}", (Duration)_time_axis_max_value);
    }

    // Metric overlay
    // Events related to the overlay have to be caught before the main view events,
    // while drawing must be done after everything else
    const char* overlay_metric_name =
        (_selected_metric) ? _selected_metric->display_string.c_str() : "Frame time (No metric)";
    const lm::dvec2 overlay_padding = ImGui::GetStyle().FramePadding;
    lm::dvec2 overlay_size =
        (lm::dvec2)ImGui::CalcTextSize(overlay_metric_name) + overlay_padding * 2.0;
    Rect overlay_area = layout.main.subrect(Direction::Right, overlay_size.x)
                            .subrect(Direction::Up, overlay_size.y)
                            .offset(-6.0, 6.0);

    ImGui::SetCursorScreenPos(overlay_area.p0);
    bool overlay_clicked = ImGui::InvisibleButton("##Overlay", overlay_size);
    bool overlay_active = ImGui::IsItemActive();
    bool overlay_hovered = ImGui::IsItemHovered();

    // Main view events
    const auto events = _view.catch_events({layout.main.p0, area.p1});
    _view.process_events(events);
    _view.process_smoothing();

    const auto& metrics = database.get_metrics();

    // Update bounds
    auto bounds = _view.get_bounds();
    if (!_selected_metric || _selected_metric->unit == data::MetricUnit::Microsecond)
        bounds->p1.y = _time_axis_max_value;
    else
        bounds->p1.y = 1.0 + 1.1 * (1.0 + metrics.get_maximum(*_selected_metric));

    _view.set_bounds(bounds);
    _view.set_visible_range_y(bounds->p0.y, bounds->p1.y);

    view::draw_background(layout);
    _horizontal_ruler.draw_horizontal(layout, _view);
    _vertical_ruler.draw_vertical(layout, _view);

    const auto& frames = database.get_frames();

    const double frame_width = 1.0 / _view.unit_per_px(layout.main.size()).x;
    const double frame_marker_rounding = (frame_width > 2.0) ? 2.0 : 0.0;

    // We don't want to draw more than one frame per pixel
    const int frame_skip = 0 + std::floor(1.0 / frame_width);

    const auto visible_area = _view.get_visible_area();
    size_t min_i = std::floor(std::max(0.01, visible_area.p0.x));
    size_t max_i = std::min<size_t>(std::ceil(visible_area.p1.x), frames.size());

    std::vector<FrameRender> frame_renders;
    if (min_i < max_i)
    {
        frame_renders = _compute_frames_render(
            min_i, max_i - min_i, _selected_metric, frames, filters, metrics);
    }

    // For each filter, we will want to keep track of the pixel where its last marker was drawn
    std::vector<int> last_drawn_marker_pixel(filters.size(), -1);

    data::MetricUnit unit = data::MetricUnit::Microsecond;
    if (_selected_metric) unit = _selected_metric->unit;

    ImGui::PushClipRect(layout.main.p0, area.p1, true);
    bool open_frame_context_menu = false;

    for (size_t i = min_i; i < max_i; ++i)
    {
        const auto& frame = frames[i];
        auto frame_render = frame_renders[i - min_i];

        const bool is_skipped_frame = (i) % (frame_skip + 1) > 0;

        size_t merge_size = frame_skip + 1;
        if (merge_size > 1 && i + merge_size >= max_i) merge_size = max_i - i;

        // Frame skip means we merge frames appearing within the same pixel, only considering the
        // frame with the highest "value" out of the group of merged frames
        if (frame_skip > 0 && !is_skipped_frame)
        {
            gsl::span<const FrameRender> merged_frames(
                frame_renders.data() + i - min_i, merge_size);
            frame_render = _get_merged_frames_render(merged_frames);
        }

        Rect frame_rect = Rect({(double)i, 0.0}, {(double)i + 1.0, frame_render.value});
        frame_rect = _view.unit_to_px(frame_rect, layout.main);

        Rect interact_rect = frame_rect;
        interact_rect.p0.y = layout.main.p0.y;
        interact_rect.p1.y = area.p1.y;

        // We want to trim the side of the visual rect while keeping a minimum width
        frame_rect.p1.x = std::max(frame_rect.p0.x + 2.0, frame_rect.p1.x - 1.0);

        bool hovered = false;
        if (!is_skipped_frame)
        {
            // When skipping frames, we need to expand the size of the interaction rect to contain
            // the area of the skipped frames
            if (frame_skip > 0 && !is_skipped_frame)
            {
                interact_rect.p1.x += (interact_rect.size().x * frame_skip);
            }

            ImGui::SetCursorScreenPos(interact_rect.p0);
            ImGui::InvisibleButton("##Event catcher", interact_rect.size());
            hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            if (hovered)
            {
                ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePreviousTooltip, 0);

                ImGui::Text("Frame %zu", i);

                if (!frame_render.opaque) ImGui::BeginDisabled(true);

                format_buffer(buffer, "Value: {}", MetricValue{frame_render.value, unit});
                ImGui::Text("%s", buffer.data());

                // @Todo Format as timestamp
                format_buffer(buffer, "Began at: {}", (Duration)frame.begin);
                ImGui::Text("%s", buffer.data());

                ImGui::Separator();

                for (const auto& filter : filters)
                {
                    if (filter.pass(frame)) ImGui::BulletText("%s", filter.name);
                }

                if (!frame_render.opaque) ImGui::EndDisabled();

                ImGui::EndTooltip();

                action_bus.push_back(context::Action::highlight_frame_in_timeline(i));

                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    action_bus.push_back(_focus_on_frame_action(i, database));
                    action_bus.push_back(context::Action::select_gpu_passes_frame(i));
                }

                if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                {
                    open_frame_context_menu = true;
                    _context_menu_frame_index = i;
                }
            }
        }

        if (frame_render.opaque)
        {
            if (!is_skipped_frame)
            {
                // If frames are skipped, we render the merged frames as two different rects, to
                // represent the range between the highest and lowest frames
                if (frame_skip > 0)
                {
                    const uint32_t max_rect_color =
                        color::with_alpha(frame_render.frame_color, 0.22);
                    const uint32_t min_rect_color = frame_render.frame_color;
                    const double min_rect_height =
                        frame_rect.size().y * (frame_render.min / frame_render.value);

                    if (std::ceil(frame_rect.p0.x) == std::ceil(frame_rect.p1.x))
                    {
                        frame_rect.p0.x = std::ceil(frame_rect.p0.x);
                        frame_rect.p1.x = frame_rect.p0.x + 1.0;
                    }

                    Rect frame_max_rect, frame_min_rect;
                    frame_rect.split(
                        Direction::Down, min_rect_height, &frame_min_rect, &frame_max_rect);

                    draw_list->AddRectFilled(frame_max_rect.p0, frame_max_rect.p1, max_rect_color);
                    draw_list->AddRectFilled(frame_min_rect.p0, frame_min_rect.p1, min_rect_color);
                }
                else
                {
                    draw_list->AddRectFilled(
                        frame_rect.p0, frame_rect.p1, frame_render.frame_color);
                }
            }

            const int marker_pixel = std::floor(frame_rect.p0.x);

            // Draw markers based on the frame's render types
            for (size_t i = 0; i < filter_areas.size(); ++i)
            {
                // To avoid drawing a line several times per pixel when zoomed out, we don't want
                // to draw a marker if one was already placed at the same place.
                // A better optimization is possible by drawing rectangles instead of lines when
                // zoomed out, although this check alone still reduces the amount of triangles
                // by a lot
                if (marker_pixel == last_drawn_marker_pixel[i]) continue;

                if (*filters[i].p_active && filters[i].pass(frame))
                {
                    Rect marker_rect = frame_rect;
                    marker_rect.p0.y = filter_areas[i].p0.y;
                    marker_rect.p1.y = filter_areas[i].p1.y;

                    draw_list->AddRectFilled(
                        marker_rect.p0, marker_rect.p1, frame_render.marker_color,
                        frame_marker_rounding);

                    last_drawn_marker_pixel[i] = marker_pixel;
                }
            }
        }

        if (hovered || (_highlighted_frame && *_highlighted_frame == i))
        {
            Rect highlight_rect = interact_rect;

            // Make the highlight more visible at low zoom values
            highlight_rect.p0.x -= 1.0;
            highlight_rect.p1.x += 1.0;

            draw_list->AddRectFilled(
                highlight_rect.p0, highlight_rect.p1, color::get_hover_shadow());
        }
    }
    ImGui::PopClipRect();

    // Overlay drawing
    auto overlay_color = ImGui::GetColorU32(
        (overlay_active)        ? ImGuiCol_ButtonActive
            : (overlay_hovered) ? ImGuiCol_ButtonHovered
                                : ImGuiCol_WindowBg);
    draw_list->AddRectFilled(overlay_area.p0, overlay_area.p1, overlay_color);
    draw_list->AddRect(overlay_area.p0, overlay_area.p1, ImGui::GetColorU32(ImGuiCol_Border));
    ImGui::SetCursorScreenPos(overlay_area.p0 + overlay_padding);
    ImGui::Text("%s", overlay_metric_name);

    // Filtered metric selector
    bool metric_changed = filtered_metric_selector(
        "Select a metric to show in the frame graph", overlay_clicked, metrics.get_known_metrics(),
        database.get_threads(), _selected_metric, "Frame CPU time (No metric)");

    if (metric_changed)
    {
        auto unit = _selected_metric ? _selected_metric->unit : data::MetricUnit::Microsecond;
        _vertical_ruler.unit = unit;
    }

    if (open_frame_context_menu)
    {
        ImGui::OpenPopup("Frame context menu");
    }

    if (ImGui::BeginPopup("Frame context menu"))
    {
        if (ImGui::MenuItem("See in timeline"))
        {
            action_bus.push_back(_focus_on_frame_action(_context_menu_frame_index, database));
        }

        if (ImGui::MenuItem("See in pie chart"))
        {
            action_bus.push_back(
                context::Action::select_gpu_passes_frame(_context_menu_frame_index));
        }
        ImGui::EndPopup();
    }

    _highlighted_frame = std::nullopt;
}

} // namespace ui::widget
