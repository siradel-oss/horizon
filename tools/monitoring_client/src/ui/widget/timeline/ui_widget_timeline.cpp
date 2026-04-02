#include "ui/widget/timeline/ui_widget_timeline.h"

#include "ui/ui_colors.h"
#include "ui/widget/timeline/ui_widget_timeline_metric_graph.h"
#include "ui/widget/timeline/ui_widget_timeline_sample_graph.h"
#include "ui/widget/timeline/ui_widget_timeline_snapshot_graph.h"

#include <imgui_internal.h>

namespace
{

using namespace ui::helpers;

constexpr double WIDGET_MIN_HEIGHT = 45.0;
constexpr double WIDGET_SPACING = 8.0;
constexpr double WIDGET_MAX_COL_WIDTH = 202.0; // Includes spacing & ruler width when needed
constexpr double WIDGET_MAX_VRULER_WIDTH = 101.0;
constexpr double WIDGET_COL_SPACING = 8.0;
constexpr double WIDGET_RESIZE_HANDLE_SIZE = 12.0;
constexpr double HRULER_OFFSET_FROM_TOP = 2.0;
constexpr double WIDGETS_OFFSET_FROM_HRULER = 4.0;
constexpr double FOOTER_HEIGHT = 42.0;

struct WidgetDrawContext
{
    const ui::view::View* shared_view;
    const ui::view::Ruler* shared_ruler;

    std::optional<double> hovered_pixel_x;
    std::optional<double> hovered_frame_x0;
    std::optional<double> hovered_frame_x1;

    const data::Database* database;
    ui::context::ActionBus* action_bus;
};

void _draw_widget_column(ui::widget::TimelineWidget* widget, const Rect& area)
{
    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(area.p0, area.p1, ui::view::get_default_background_color());

    Rect contents_area = area;
    widget->draw_header_column_contents(contents_area);
}

void _show_resize_handle(ui::widget::TimelineWidget* widget, const Rect& col_area)
{
    const Rect handle_rect(col_area.p1 - lm::dvec2(WIDGET_RESIZE_HANDLE_SIZE), col_area.p1);

    auto& io = ImGui::GetIO();
    auto* draw_list = ImGui::GetWindowDrawList();

    uint32_t grip_color = ImGui::GetColorU32(ImGuiCol_ResizeGrip);

    ImGui::SetCursorScreenPos(handle_rect.p0);
    ImGui::InvisibleButton("##Resize handle", handle_rect.size());

    if (ImGui::IsItemActive())
    {
        grip_color = ImGui::GetColorU32(ImGuiCol_ResizeGripActive);

        double target_height =
            std::max(io.MousePos.y - col_area.p0.y + handle_rect.size().y / 2.0, WIDGET_MIN_HEIGHT);
        widget->on_resize(target_height);
    }

    else if (ImGui::IsItemHovered())
    {
        grip_color = ImGui::GetColorU32(ImGuiCol_ResizeGripHovered);
    }

    const lm::dvec2 grip_p0 = handle_rect.p1;
    const lm::dvec2 grip_p1 = handle_rect.p1 - lm::dvec2(WIDGET_RESIZE_HANDLE_SIZE, 0);
    const lm::dvec2 grip_p2 = handle_rect.p1 - lm::dvec2(0, WIDGET_RESIZE_HANDLE_SIZE);
    draw_list->AddTriangleFilled(grip_p0, grip_p1, grip_p2, grip_color);
}

struct WidgetDrawResult
{
    bool close = false;
    ui::view::ViewEvents events;
};

WidgetDrawResult _draw_widget(
    ui::widget::TimelineWidget* widget,
    const Rect& area,
    const lm::dvec2& header_position,
    const WidgetDrawContext& ctx)
{
    WidgetDrawResult result;

    auto* draw_list = ImGui::GetWindowDrawList();

    const double widget_vruler_width =
        std::min(widget->get_vertical_ruler_preferred_width(area.size().y), WIDGET_MAX_COL_WIDTH);
    const double widget_col_width = WIDGET_MAX_COL_WIDTH - widget_vruler_width;

    Rect view_area = area.trim(Direction::Left, widget_col_width);
    Rect col_area = area.subrect(Direction::Left, widget_col_width - WIDGET_COL_SPACING);

    // View drawing
    auto layout = ui::view::make_custom_layout(view_area, widget_vruler_width, 0.0);
    ui::view::draw_background(layout);
    ctx.shared_ruler->draw_horizontal(layout, *ctx.shared_view, false, true);

    const auto visible_area = ctx.shared_view->get_visible_area();
    widget->set_time_range(visible_area.p0.x, visible_area.p1.x);

    // Highlight background corresponding to highlighted frame
    if (ctx.hovered_frame_x0.has_value() && ctx.hovered_frame_x1.has_value())
    {
        Rect hover_rect = Rect(
            {ctx.hovered_frame_x0.value(), view_area.p0.y},
            {ctx.hovered_frame_x1.value(), view_area.p1.y});

        auto* draw_list = ImGui::GetWindowDrawList();

        draw_list->PushClipRect(view_area.p0, view_area.p1, true);
        draw_list->AddRectFilled(hover_rect.p0, hover_rect.p1, ui::color::get_hover_shadow());
        draw_list->PopClipRect();
    }

    // Header
    const lm::dvec2 frame_padding = ImGui::GetStyle().FramePadding;
    const lm::dvec2 button_size = lm::dvec2(ImGui::GetFontSize()) + frame_padding * 0.0;

    auto button_id = ImGui::GetID("##Close button");
    result.close = ImGui::CloseButton(button_id, header_position);

    const char* widget_name = widget->get_name();
    draw_list->AddText(
        header_position + lm::dvec2(button_size.x + frame_padding.x + 4.0, frame_padding.y / 2.0),
        ImGui::GetColorU32(ImGuiCol_Text), widget_name);

    // Widget drawing
    _draw_widget_column(widget, col_area);
    widget->draw(layout, *ctx.database, result.events, *ctx.action_bus);

    if (widget->can_resize())
    {
        _show_resize_handle(widget, col_area);
    }

    if (ctx.hovered_pixel_x.has_value())
    {
        lm::dvec2 p0 = {ctx.hovered_pixel_x.value(), view_area.p0.y};
        lm::dvec2 p1 = {ctx.hovered_pixel_x.value(), view_area.p1.y};
        ImGui::GetWindowDrawList()->AddLine(p0, p1, 0xff593af2, 2.0F);
    }

    return result;
}

void _format_timeline_ruler_value(fmt::memory_buffer& buffer, MetricValue value, double range_us)
{
    if (range_us < 100'000)
    {
        format_buffer(buffer, "{:0.3Lf} ms", value.value / 1000.0);
    }
    else if (range_us < 1'000'000)
    {
        format_buffer(buffer, "{:0.2Lf} ms", value.value / 1000.0);
    }
    else if (range_us < 10'000'000)
    {
        format_buffer(buffer, "{:0.3Lf} s", value.value / 1'000'000.0);
    }
    else
    {
        format_buffer(buffer, "{:0.2Lf} s", value.value / 1'000'000.0);
    }
}

} // namespace

namespace ui::widget
{

using namespace helpers;

Timeline::Timeline() : _shared_view({{0.0, 0.0}, {30'000.0, 1.0}})
{
    _shared_view.set_bounds(Rect{{0.0, 0.0}, {std::numeric_limits<double>::max(), 1.0}});
    _shared_view.set_zoom_bounds(Rect({0.001, 1.0}, {1000.0, 1.0}));

    _add_widget(std::make_unique<SampleGraph>());
    _add_widget(std::make_unique<SnapshotGraph>());

    _ruler.unit = data::MetricUnit::Microsecond;
}

void Timeline::draw(
    const Rect& base_area,
    const data::Database& database,
    context::ActionBus& action_bus)
{
    // Adapt the ruler's graduations
    auto visible_area = _shared_view.get_visible_area();
    double range = visible_area.p1.x - visible_area.p0.x;

    _ruler.special_formatter = [range](fmt::memory_buffer& buffer, MetricValue value)
    { _format_timeline_ruler_value(buffer, value, range); };

    // HRULER_OFFSET_FROM_TOP is multiplied by two because ruler graduations are drawn at the center
    // of the given rect.
    Rect ruler_area =
        base_area
            .subrect(Direction::Up, HRULER_OFFSET_FROM_TOP * 2.0 + ImGui::GetTextLineHeight() / 2.0)
            .trim(Direction::Left, WIDGET_MAX_COL_WIDTH);

    view::Layout ruler_layout = {};
    ruler_layout.main = ruler_area;
    ruler_layout.horizontal_ruler = ruler_area;
    _ruler.draw_horizontal(ruler_layout, _shared_view, true, false);

    Rect widgets_area =
        base_area.trim(Direction::Up, ruler_area.size().y + WIDGETS_OFFSET_FROM_HRULER)
            .trim(Direction::Down, FOOTER_HEIGHT);
    double widgets_area_actual_height = 0;

    ImGui::SetCursorScreenPos(widgets_area.p0);
    if (ImGui::BeginChild("Widgets scroll area", widgets_area.size()))
    {
        widgets_area_actual_height = _draw_widgets(widgets_area, database, action_bus);
    }
    ImGui::EndChild();

    Rect footer_area = base_area.subrect(Direction::Down, FOOTER_HEIGHT);

    // Draw the footer right below the widgets when possible.
    if (widgets_area_actual_height < widgets_area.size().y)
    {
        footer_area = footer_area.offset(0.0, widgets_area_actual_height - widgets_area.size().y);
    }

    _draw_footer(footer_area, database);

    _highlighted_frame_index = std::nullopt;
}

void Timeline::set_focus(int64_t min, int64_t max)
{
    _shared_view.set_visible_range_x(min, max, true);
}

void Timeline::set_focus(data::SampleId sample_id, int64_t begin, int64_t exit)
{
    set_focus(begin, exit);

    for (auto& widget : _widgets)
    {
        widget->focus_on_sample(sample_id);
    }
}

void Timeline::on_database_clear()
{
    for (auto& widget : _widgets)
    {
        widget->on_database_clear();
    }
}

double Timeline::_draw_widgets(
    const helpers::Rect& area,
    const data::Database& database,
    context::ActionBus& action_bus)
{
    const auto widget_view_area = area.trim(Direction::Left, WIDGET_MAX_COL_WIDTH);

    auto events = _shared_view.catch_events(widget_view_area);
    _shared_view.process_events(events);
    _shared_view.process_smoothing();

    // Vertical scrolling is handled here because it is not related to the view itself, but to the
    // ImGui window instead.
    if (events.scroll.has_value())
    {
        const float target_scroll = ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y;
        ImGui::SetScrollY(clamp(target_scroll, 0.0F, ImGui::GetScrollMaxY()));
    }

    std::optional<double> hovered_pixel_x;
    if (ImGui::IsItemHovered())
    {
        hovered_pixel_x = ImGui::GetIO().MousePos.x;

        double hovered_timestamp =
            _shared_view.px_to_unit(lm::dvec2(*hovered_pixel_x, 0.0), widget_view_area).x;
        auto frame_index = database.find_last_frame(hovered_timestamp);
        if (frame_index)
        {
            action_bus.push_back(context::Action::highlight_frame_in_frame_graph(*frame_index));
            _highlighted_frame_index = frame_index;
        }
    }

    std::optional<double> hovered_frame_x0;
    std::optional<double> hovered_frame_x1;
    if (_highlighted_frame_index)
    {
        const auto& frame = database.get_frames()[*_highlighted_frame_index];

        hovered_frame_x0 = (double)frame.begin;
        hovered_frame_x1 = (double)frame.end;

        if (*_highlighted_frame_index + 1 < database.get_frames().size())
        {
            // Extend the highlighted area to the end of the next frame
            const auto& next_frame = database.get_frames()[*_highlighted_frame_index + 1];
            hovered_frame_x1 = (double)next_frame.begin;
        }
        else if (database.get_samples().sample_count > 0)
        {
            // This is the last frame: extend the highlighted area to the end of the last registered
            // profiling sample.
            hovered_frame_x1 = std::max<double>(
                hovered_frame_x1.value(),
                database.get_samples().threads.front().max_timestamp.value_or(0));
        }

        hovered_frame_x0.value() =
            _shared_view.unit_to_px({hovered_frame_x0.value(), 0.0}, widget_view_area).x;
        hovered_frame_x1.value() =
            _shared_view.unit_to_px({hovered_frame_x1.value(), 0.0}, widget_view_area).x;

        // Make the highlight more visible at low zoom values
        hovered_frame_x0.value() -= 1.0;
        hovered_frame_x1.value() += 1.0;
    }

    // Setup widget draw params
    WidgetDrawContext ctx;
    ctx.shared_view = &_shared_view;
    ctx.shared_ruler = &_ruler;
    ctx.hovered_pixel_x = hovered_pixel_x;
    ctx.hovered_frame_x0 = hovered_frame_x0;
    ctx.hovered_frame_x1 = hovered_frame_x1;
    ctx.database = &database;
    ctx.action_bus = &action_bus;

    lm::dvec2 draw_position = area.p0 - lm::dvec2(ImGui::GetScrollX(), ImGui::GetScrollY());

    view::ViewEvents widget_events;
    std::optional<size_t> closed_widget_id;

    for (size_t widget_id = 0; widget_id < _widgets.size(); widget_id++)
    {
        TimelineWidget* widget = _widgets[widget_id].get();

        const lm::dvec2 header_position = draw_position;
        draw_position.y += ImGui::GetTextLineHeightWithSpacing();

        const double widget_height = std::max(WIDGET_MIN_HEIGHT, widget->get_preferred_height());
        const Rect widget_area = Rect(draw_position, {area.p1.x, draw_position.y + widget_height});
        draw_position.y += widget_area.size().y + WIDGET_SPACING;

        ImGui::PushID(widget_id);

        auto result = _draw_widget(widget, widget_area, header_position, ctx);

        if (result.close) closed_widget_id = widget_id;
        if (!result.events.empty()) widget_events = result.events;

        ImGui::PopID();
    }

    if (closed_widget_id.has_value())
    {
        _widgets.erase(_widgets.begin() + closed_widget_id.value());
    }

    if (!widget_events.empty()) _shared_view.process_events(widget_events);

    return (draw_position.y - area.p0.y) - WIDGET_SPACING;
}

void Timeline::_draw_footer(const helpers::Rect& area, const data::Database& database)
{
    const Rect button_area =
        area.subrect(Direction::Left, WIDGET_MAX_COL_WIDTH).trim(Direction::Up, 8.0);

    // We need to use another child window to have mouse input, it would be blocked by the widgets
    // area's scrollable child window otherwise.
    ImGui::SetCursorScreenPos(button_area.p0);
    if (ImGui::BeginChild("##Footer", button_area.size()))
    {
        _widget_addition_popup(ImGui::Button("Add widget..."), database);

        ImGui::SameLine();
        help_marker(
            "Every widget added in the timeline has different interactions.\n"
            "Try double clicking on the markers, or right clicking anywhere.");
    }
    ImGui::EndChild();

    const auto indicator_position = area.offset(WIDGET_MAX_COL_WIDTH, WIDGET_SPACING).p0;
    const auto indicator_width = area.size().x - WIDGET_MAX_COL_WIDTH;
    _scale_indicator(indicator_position, indicator_width, database);
}

void Timeline::_add_widget(std::unique_ptr<TimelineWidget> widget)
{
    auto visible_area = _shared_view.get_visible_area();
    widget->set_time_range(visible_area.p0.x, visible_area.p1.x);

    _widgets.push_back(std::move(widget));
}

void Timeline::_widget_addition_popup(bool open, const data::Database& database)
{
    if (open)
    {
        ImGui::OpenPopup("##Add timeline row");
    }

    bool open_metric_selection_modal = false;
    if (ImGui::BeginPopup("##Add timeline row"))
    {
        if (ImGui::MenuItem("Add sample graph"))
        {
            _add_widget(std::make_unique<SampleGraph>());
        }

        if (ImGui::MenuItem("Add snapshot timeline"))
        {
            _add_widget(std::make_unique<SnapshotGraph>());
        }

        open_metric_selection_modal = ImGui::MenuItem("Add metric graph...");

        ImGui::EndPopup();
    }

    const auto& known_metrics = database.get_metrics().get_known_metrics();
    std::optional<data::Metric> selected_metric = std::nullopt;

    bool selected = filtered_metric_selector(
        "Select a metric to examine", open_metric_selection_modal, known_metrics,
        database.get_threads(), selected_metric);

    if (selected && selected_metric)
    {
        _add_widget(std::make_unique<MetricGraph>(selected_metric));
    }
}

void Timeline::_scale_indicator(
    const lm::dvec2& position,
    double width,
    const data::Database& database)
{
    auto* draw_list = ImGui::GetWindowDrawList();
    auto& buffer = static_fmt_memory_buffer();

    const auto font_size = ImGui::GetFontSize();

    const auto main_line_position = position + lm::dvec2(0.0, font_size + 4.0);
    const auto text_center = main_line_position + lm::dvec2(width / 2.0, (font_size + 6.0) / 2.0);

    const auto time_range = _shared_view.get_visible_area().size().x;
    const auto base_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    // Base line
    draw_list->AddLine(main_line_position, main_line_position + lm::dvec2(width, 0.0), base_color);
    draw_text_centered(draw_list, text_center, base_color, "{}", (Duration)time_range);

    if (database.get_frames().empty()) return;

    // Estimate the amount of pixels between each frame
    // (by estimating that frames last 16 ms)
    const double us_per_pixel = _shared_view.get_visible_area().size().x / width;
    const double pixels_per_frame = 1.0 / us_per_pixel * 16667.0;

    static constexpr double PPF_CUTOFF_THRESHOLD = 8.0;
    static constexpr double PPF_FADE_THRESHOLD = 16.0;

    if (pixels_per_frame > PPF_CUTOFF_THRESHOLD)
    {
        // Frame graduations (if there are enough pixels between frames)
        auto range = database.find_frames(
            _shared_view.get_visible_area().p0.x, _shared_view.get_visible_area().p1.x);

        auto fade_color = [](uint32_t color, double value, double fade_begin, double fade_end)
        {
            double min = std::min(fade_begin, fade_end);
            double max = std::max(fade_begin, fade_end);

            value = clamp((value - min) / (max - min), 0.0, 1.0);
            return color::mix(color::with_alpha(color, 0), color, value);
        };

        // Fade the line color when nearing the cutoff threshold
        const auto line_color =
            fade_color(base_color, pixels_per_frame, PPF_FADE_THRESHOLD, PPF_CUTOFF_THRESHOLD);

        // Exception: we want to add the previous frame to the result if it exists,
        // in order to show it to the user event if the view is scrolled too much
        // to the right. This is to avoid situations where no frames are displayed
        // when zoomed in too much.
        if (database.get_frames()[range.first_index].begin > _shared_view.get_visible_area().p0.x
            && range.first_index > 0)
        {
            range.first_index--;
            range.count++;
        }
        else if (range.count == 0)
        {
            auto first_before = database.find_last_frame(_shared_view.get_visible_area().p0.x);
            if (first_before)
            {
                range.first_index = *first_before;
                range.count = 1;
            }
        }

        static const double TEXT_SPACING = 4.0;
        const Rect ruler_rect(position, position + lm::dvec2(width, 1.0));

        double next_text_x = ruler_rect.p1.x;

        auto draw_text_if_possible =
            [&](const char* text, lm::dvec2 position, double max_width, uint32_t color)
        {
            double text_width = ImGui::CalcTextSize(text).x;
            if (text_width > max_width)
            {
                return 0.0;
            }

            auto faded_color = fade_color(color, max_width - text_width, 0.0, 10.0);
            draw_list->AddText(position, faded_color, text);

            return text_width;
        };

        draw_list->PushClipRect(position, position + lm::dvec2(width, font_size * 2.0));
        for (int range_index = range.count - 1; range_index >= 0; range_index--)
        {
            size_t frame_index = range.first_index + range_index;

            const auto& frame = database.get_frames()[frame_index];

            double x = _shared_view.unit_to_px(lm::dvec2(frame.begin, 0.), ruler_rect).x;
            draw_list->AddLine(
                lm::dvec2(x, position.y), lm::dvec2(x, position.y + font_size + 4.0), line_color);

            // Exception: if this is the leftmost frame, we want to clamp it to
            // the screen so that at least one frame is displayed no matter the scrolling
            // and zoom level: but only if it doesn't overlap with next frame.
            if (range_index == 0)
            {
                x = std::max(x, position.x);
            }

            double text_available_width = next_text_x - x;
            text_available_width -= TEXT_SPACING * 2.0;

            lm::dvec2 text_position = {x + TEXT_SPACING, position.y};

            format_buffer(buffer, "#{}", frame_index);
            double text_width = draw_text_if_possible(
                buffer.data(), text_position, text_available_width, line_color);

            text_available_width -= text_width + TEXT_SPACING;
            text_position.x += text_width + TEXT_SPACING;

            auto frame_end = frame.end;
            if (frame_index < database.get_frames().size() - 1)
            {
                // We want to use the next frame's begin timestamp when possible to match the
                // visuals of the timeline.
                frame_end = database.get_frames()[frame_index + 1].begin;
            }
            else if (database.get_samples().sample_count > 0)
            {
                // Use the last sample to compute the duration of the last frame if possible.
                frame_end = std::max(
                    frame_end, database.get_samples().threads.front().max_timestamp.value_or(0));
            }

            format_buffer(buffer, "- {}", (Duration)(frame_end - frame.begin));
            draw_text_if_possible(buffer.data(), text_position, text_available_width, line_color);

            next_text_x = x;
        }
        draw_list->PopClipRect();
    }
}

} // namespace ui::widget
