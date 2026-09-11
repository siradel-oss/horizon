// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#include "ui/ui_view.h"

#include "hrz/fnd/flat_hash_map.h"
#include "ui/ui_colors.h"
#include "ui/ui_helpers.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <lin_maths.h>

#include <cmath>
#include <utility>

namespace
{

void _default_ruler_formatter(fmt::memory_buffer& buffer, ui::helpers::MetricValue value)
{
    ui::helpers::format_buffer(buffer, "{}", value);
}

// Returns x such that a graduation should be placed every x units of the view
// min_spacing: the minimum spacing in pixels between graduations
double _compute_graduation_step(double max_spacing, double unit_per_px)
{
    double unit_spacing = max_spacing * unit_per_px;
    double power = std::floor(std::log10(unit_spacing));
    double power_of_ten = std::pow(10.0, power);
    double multiplier = unit_spacing / power_of_ten;

    multiplier = (multiplier > 5.0) ? 5.0 : (multiplier > 2.0) ? 2.0 : 1.0;

    return multiplier * power_of_ten;
}

std::pair<uint32_t, uint32_t> _get_graduations_colors()
{
    const int32_t disabled_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const int32_t background_color = ui::view::get_default_background_color();

    const int32_t grad_color = ui::color::mix(disabled_color, background_color, 0.8);
    const int32_t subgrad_color = ui::color::mix(disabled_color, background_color, 0.93);

    return {grad_color, subgrad_color};
}

} // namespace

namespace ui::view
{

using namespace helpers;

Layout make_layout(
    const helpers::Rect& area,
    const View* view,
    bool with_horizontal_ruler,
    bool with_vertical_ruler)
{
    Layout layout;
    layout.main = area;

    if (with_vertical_ruler)
    {
        auto dir = (view && view->get_flip_x()) ? Direction::Right : Direction::Left;
        layout.main.split(dir, Layout::RULER_WIDTH, &layout.vertical_ruler, &layout.main);
    }

    if (with_horizontal_ruler)
    {
        auto dir = (view && view->get_flip_y()) ? Direction::Down : Direction::Up;
        layout.main.split(dir, Layout::RULER_HEIGHT, &layout.horizontal_ruler, &layout.main);
    }

    if (with_horizontal_ruler && with_vertical_ruler)
    {
        auto dir = (view && view->get_flip_y()) ? Direction::Down : Direction::Up;
        layout.vertical_ruler = layout.vertical_ruler.trim(dir, Layout::RULER_HEIGHT);
    }

    return layout;
}

Layout make_custom_layout(const helpers::Rect& area, double ruler_width, double ruler_height)
{
    Layout layout;

    ruler_width = std::min(ruler_width, area.size().x);
    ruler_height = std::min(ruler_height, area.size().y);

    auto split_point = area.p0 + lm::dvec2{ruler_width, ruler_height};
    area.split_4(
        split_point, nullptr, &layout.horizontal_ruler, &layout.vertical_ruler, &layout.main);

    return layout;
}

void draw_background(const Layout& layout, std::optional<uint32_t> color)
{
    if (!color) color = get_default_background_color();

    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(layout.main.p0, layout.main.p1, *color);
}

View::View() :
    _visible_area({0.0, 0.0}, {1.0, 1.0}),
    _target_visible_area(_visible_area),
    _view_size(_visible_area.size())
{
}

View::View(const helpers::Rect& visible_area, bool flip_x, bool flip_y) :
    _flip_x(flip_x),
    _flip_y(flip_y),
    _visible_area(visible_area),
    _target_visible_area(visible_area),
    _view_size(visible_area.size())
{
}

lm::dvec2 View::px_to_unit(const lm::dvec2& position, const helpers::Rect& px_area) const
{
    auto relative = (position - px_area.p0) / px_area.size() * _visible_area.size();

    relative.x *= (_flip_x) ? -1 : 1;
    relative.y *= (_flip_y) ? -1 : 1;

    lm::dvec2 origin = _visible_area.p0;
    if (_flip_x) origin.x += _visible_area.size().x;
    if (_flip_y) origin.y += _visible_area.size().y;

    return origin + relative;
}

lm::dvec2 View::unit_to_px(const lm::dvec2& position, const helpers::Rect& px_area) const
{
    auto relative = (position - _visible_area.p0) / _visible_area.size() * px_area.size();

    relative.x *= (_flip_x) ? -1 : 1;
    relative.y *= (_flip_y) ? -1 : 1;

    lm::dvec2 origin = px_area.p0;
    if (_flip_x) origin.x += px_area.size().x;
    if (_flip_y) origin.y += px_area.size().y;

    return origin + relative;
}

helpers::Rect View::px_to_unit(const helpers::Rect& rect, const helpers::Rect& px_area) const
{
    auto result = Rect(px_to_unit(rect.p0, px_area), px_to_unit(rect.p1, px_area));
    return (_flip_x || _flip_y) ? result.fix() : result;
}

helpers::Rect View::unit_to_px(const helpers::Rect& rect, const helpers::Rect& px_area) const
{
    auto result = Rect(unit_to_px(rect.p0, px_area), unit_to_px(rect.p1, px_area));
    return (_flip_x || _flip_y) ? result.fix() : result;
}

void View::scroll_by(const lm::dvec2& value)
{
    lm::dvec2 clamped_value = value;

    clamped_value.x *= (_flip_x) ? -1.0 : 1.0;
    clamped_value.y *= (_flip_y) ? -1.0 : 1.0;

    if (_bounds)
    {
        clamped_value.x = helpers::clamp(
            clamped_value.x, _bounds->p0.x - _target_visible_area.p0.x,
            _bounds->p1.x - _target_visible_area.p1.x);
        clamped_value.y = helpers::clamp(
            clamped_value.y, _bounds->p0.y - _target_visible_area.p0.y,
            _bounds->p1.y - _target_visible_area.p1.y);
    }

    _visible_area += clamped_value;
    _target_visible_area += clamped_value;
}

void View::zoom_by(const lm::dvec2& value)
{
    const lm::dvec2 center = (_visible_area.p0 + _visible_area.p1) / 2.0F;
    zoom_by(value, center);
}

void View::zoom_by(const lm::dvec2& value, const lm::dvec2& anchor)
{
    const lm::dvec2 zoom = _view_size / _target_visible_area.size();
    lm::dvec2 target_zoom = zoom + value;

    if (_zoom_bounds)
    {
        target_zoom.x = helpers::clamp(target_zoom.x, _zoom_bounds->p0.x, _zoom_bounds->p1.x);
        target_zoom.y = helpers::clamp(target_zoom.y, _zoom_bounds->p0.y, _zoom_bounds->p1.y);
    }

    _target_visible_area = {
        anchor - (anchor - _target_visible_area.p0) * zoom / target_zoom,
        anchor - (anchor - _target_visible_area.p1) * zoom / target_zoom
    };

    const auto target_before_clamp = _target_visible_area;

    if (_bounds)
    {
        // After zooming out, the visible area might have expanded beyond the imposed
        // bounds. We need to clamp it back:
        // Clamp the upper left corner
        _target_visible_area.p0.x =
            helpers::clamp(_target_visible_area.p0.x, _bounds->p0.x, _bounds->p1.x);
        _target_visible_area.p0.y =
            helpers::clamp(_target_visible_area.p0.y, _bounds->p0.y, _bounds->p1.y);

        // Apply the resulting offset to the lower right corner, to try conserving
        // the targeted zoom value
        const auto p0_offset = _target_visible_area.p0 - target_before_clamp.p0;
        _target_visible_area.p1 += p0_offset;

        // Clamp the lower right corner
        _target_visible_area.p1.x =
            helpers::clamp(_target_visible_area.p1.x, _bounds->p0.x, _bounds->p1.x);
        _target_visible_area.p1.y =
            helpers::clamp(_target_visible_area.p1.y, _bounds->p0.y, _bounds->p1.y);
    }
}

void View::set_visible_range_x(double min, double max, bool smooth)
{
    if (_bounds)
    {
        _target_visible_area.p0.x = std::max(_bounds->p0.x, min);
        _target_visible_area.p1.x = std::min(max, _bounds->p1.x);
    }
    else
    {
        _target_visible_area.p0.x = min;
        _target_visible_area.p1.x = max;
    }

    if (!smooth)
    {
        _visible_area.p0.x = _target_visible_area.p0.x;
        _visible_area.p1.x = _target_visible_area.p1.x;
    }
}

void View::set_visible_range_y(double min, double max, bool smooth)
{
    if (_bounds)
    {
        _target_visible_area.p0.y = std::max(_bounds->p0.y, min);
        _target_visible_area.p1.y = std::min(max, _bounds->p1.y);
    }
    else
    {
        _target_visible_area.p0.y = min;
        _target_visible_area.p1.y = max;
    }

    if (!smooth)
    {
        _visible_area.p0.y = _target_visible_area.p0.y;
        _visible_area.p1.y = _target_visible_area.p1.y;
    }
}

ViewEvents View::catch_events(const Rect& area, bool prevent_scrolling, bool prevent_zooming) const
{
    ViewEvents events;

    auto& io = ImGui::GetIO();

    if (area.size().x <= 1.0F || area.size().y <= 1.0F)
    {
        return events;
    }

    // Push a "unique id" based on the position of the given area's top left pixel,
    // necessary to make the different bodies independent from one another
    ImGuiID unique_id = area.p0.x + area.p0.y * ImGui::GetWindowWidth();
    ImGui::PushID(unique_id);

    ImGui::SetCursorScreenPos(area.p0);
    ImGui::InvisibleButton("##View event catcher", area.size());

    const lm::dvec2 upp = unit_per_px(area.size());

    if (!prevent_scrolling && ImGui::IsItemActive())
    {
        lm::dvec2 delta = io.MouseDelta;
        events.scroll = ViewEvents::Scroll{-delta * upp};
    }

    if (!prevent_zooming && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
    {
        ImGui::SetItemUsingMouseWheel();

        const auto mouse_unit_pos = px_to_unit(io.MousePos, area);

        if (std::abs(io.MouseWheel) > 0.1F)
        {
            auto input_zoom = lm::dvec2(io.MouseWheel, io.MouseWheel) * 5.0;
            events.zoom = ViewEvents::Zoom{input_zoom, mouse_unit_pos};
        }
    }

    ImGui::PopID();
    return events;
}

void View::process_events(const ViewEvents& events)
{
    if (events.scroll)
    {
        scroll_by(events.scroll->value);
    }

    if (events.zoom)
    {
        auto multiplier = get_current_zoom() / 20.0F;
        zoom_by(events.zoom->value * multiplier, events.zoom->focus);
    }

    if (events.horizontal_focus)
    {
        set_visible_range_x(events.horizontal_focus->min, events.horizontal_focus->max, true);
    }

    if (events.vertical_focus)
    {
        set_visible_range_y(events.vertical_focus->min, events.vertical_focus->max, true);
    }
}

void View::process_smoothing()
{
    static constexpr double INTERPOLATION_STRENGTH = .2F;

    _visible_area.p0 += (_target_visible_area.p0 - _visible_area.p0) * INTERPOLATION_STRENGTH;
    _visible_area.p1 += (_target_visible_area.p1 - _visible_area.p1) * INTERPOLATION_STRENGTH;
}

int Ruler::get_subgraduation_count()
{
    return 4;
}

lm::dvec2 Ruler::get_spacing()
{
    return ImGui::GetStyle().ItemInnerSpacing;
}

void Ruler::draw_horizontal(
    const Layout& layout,
    const View& view,
    bool with_ruler,
    bool with_grads) const
{
    const auto& view_area = layout.main;
    const auto& ruler_area = layout.horizontal_ruler;

    const auto unit_per_px = view.unit_per_px(layout.main.size()).x;

    const auto grad_step = compute_graduation_step_horizontal(view_area.size().x, view);

    const int grad_begin = std::floor(view.get_visible_area().p0.x / grad_step);
    const int grad_end = std::ceil(view.get_visible_area().p1.x / grad_step) + 1;

    const auto colors = _get_graduations_colors();
    const auto grad_color = colors.first;
    const auto subgrad_color = colors.second;

    auto* draw_list = ImGui::GetWindowDrawList();

    for (int x = grad_begin; x < grad_end; ++x)
    {
        float x_px = view.unit_to_px(lm::dvec2((double)x * grad_step, 0), view_area).x;
        lm::dvec2 grad_p0 = {x_px, view_area.p0.y};
        lm::dvec2 grad_p1 = {x_px, view_area.p1.y};

        // Grid
        if (with_grads && grad_p1.y - grad_p0.y > 0.1)
        {
            draw_list->PushClipRect(view_area.p0, view_area.p1, true);
            draw_list->AddLine(grad_p0, grad_p1, grad_color);

            // Subgraduations
            const int subgrad_count = get_subgraduation_count();
            const auto subgrad_step = grad_step / (subgrad_count + 1);

            for (int i = 1; i <= subgrad_count; ++i)
            {
                const auto offset = i * lm::vec2(subgrad_step, 0.0) / unit_per_px;

                lm::dvec2 subgrad_p0 = grad_p0 + offset;
                lm::dvec2 subgrad_p1 = grad_p1 + offset;

                draw_list->AddLine(subgrad_p0, subgrad_p1, subgrad_color);
            }

            draw_list->PopClipRect();
        }

        if (with_ruler && x_px >= ruler_area.p0.x - 1.0 && x_px <= ruler_area.p1.x + 1.0)
        {
            const lm::dvec2 text_center = {x_px, ruler_area.p0.y + ruler_area.size().y / 2.0};
            const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);

            auto& buffer = static_fmt_memory_buffer();
            _format_metric_value(buffer, x * grad_step);

            helpers::draw_text_centered(
                draw_list, text_center, text_color, fmt::runtime(buffer.data()));
        }
    }
}

void Ruler::draw_vertical(const Layout& layout, const View& view, bool with_ruler, bool with_grads)
    const
{
    const auto& view_area = layout.main;
    const auto& ruler_area = layout.vertical_ruler;

    const auto unit_per_px = view.unit_per_px(layout.main.size()).y;

    const auto grad_step = compute_graduation_step_vertical(view_area.size().y, view);

    const int grad_begin = std::floor(view.get_visible_area().p0.y / grad_step);
    const int grad_end = std::ceil(view.get_visible_area().p1.y / grad_step) + 1;

    const auto colors = _get_graduations_colors();
    const auto grad_color = colors.first;
    const auto subgrad_color = colors.second;

    auto* draw_list = ImGui::GetWindowDrawList();

    for (int y = grad_begin; y < grad_end; ++y)
    {
        float y_px = view.unit_to_px(lm::dvec2(0.0, (double)y * grad_step), view_area).y;
        lm::dvec2 grad_p0 = {view_area.p0.x, y_px};
        lm::dvec2 grad_p1 = {view_area.p1.x, y_px};

        // Grid
        if (with_grads && grad_p1.x - grad_p0.x > 0.1)
        {
            draw_list->PushClipRect(view_area.p0, view_area.p1, true);
            draw_list->AddLine(grad_p0, grad_p1, grad_color);

            // Subgraduations
            static const int subgrad_count = 4;
            const auto subgrad_step = grad_step / (subgrad_count + 1);

            for (int i = 1; i <= subgrad_count; ++i)
            {
                const auto offset = i * lm::vec2(0.0, subgrad_step) / unit_per_px;

                lm::dvec2 subgrad_p0 = grad_p0 + offset;
                lm::dvec2 subgrad_p1 = grad_p1 + offset;

                draw_list->AddLine(subgrad_p0, subgrad_p1, subgrad_color);
            }

            draw_list->PopClipRect();
        }

        if (with_ruler && y_px >= ruler_area.p0.y - 1.0 && y_px <= ruler_area.p1.y + 1.0)
        {
            const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
            const float text_height = ImGui::GetFontSize();
            const float spacing = get_spacing().x;
            const lm::dvec2 text_edge = {
                ruler_area.p0.x + ruler_area.size().x - spacing, y_px - text_height / 2.0
            };

            auto& buffer = static_fmt_memory_buffer();
            _format_metric_value(buffer, y * grad_step);

            helpers::draw_text_right_aligned(
                draw_list, text_edge, text_color, fmt::runtime(buffer.data()));
        }
    }
}

double Ruler::get_preferred_width_for_vertical(const View& view, double ruler_height) const
{
    const auto grad_step = compute_graduation_step_vertical(ruler_height, view);
    const auto grad_last = std::ceil(view.get_visible_area().p1.y / grad_step);

    auto& buffer = static_fmt_memory_buffer();
    _format_metric_value(buffer, grad_last * grad_step);

    return ImGui::CalcTextSize(buffer.data()).x + get_spacing().x;
}

void Ruler::_format_metric_value(fmt::memory_buffer& buffer, double value) const
{
    MetricValue metric_value = {value, unit};
    if (special_formatter.has_value())
    {
        special_formatter.value()(buffer, metric_value);
    }
    else
    {
        _default_ruler_formatter(buffer, metric_value);
    }
}

double compute_graduation_step_horizontal(double ruler_width, const View& view)
{
    static constexpr double GRAD_MAX_SPACING = 160.0;
    return _compute_graduation_step(GRAD_MAX_SPACING, view.unit_per_px({ruler_width, 0.0}).x);
}

double compute_graduation_step_vertical(double ruler_height, const View& view)
{
    const double max_spacing = ImGui::GetFontSize() * 2.5;
    return _compute_graduation_step(max_spacing, view.unit_per_px({0.0, ruler_height}).y);
}

} // namespace ui::view
