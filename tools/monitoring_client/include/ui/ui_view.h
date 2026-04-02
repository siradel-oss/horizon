#pragma once

#include "ui/ui_helpers.h"

#include <imgui.h>

#include <optional>

namespace ui::view
{

struct ViewEvents
{
    bool empty() const { return !scroll && !zoom && !horizontal_focus && !vertical_focus; }

    struct Scroll
    {
        lm::dvec2 value;
    };

    std::optional<Scroll> scroll;

    struct Zoom
    {
        lm::dvec2 value;
        lm::dvec2 focus;
    };

    std::optional<Zoom> zoom;

    struct Focus
    {
        double min;
        double max;
    };

    std::optional<Focus> horizontal_focus;
    std::optional<Focus> vertical_focus;
};

class View
{
public:
    View();
    View(const helpers::Rect& visible_area, bool flip_x = false, bool flip_y = false);

    lm::dvec2 px_to_unit(const lm::dvec2& position, const helpers::Rect& px_area) const;
    lm::dvec2 unit_to_px(const lm::dvec2& position, const helpers::Rect& px_area) const;
    helpers::Rect px_to_unit(const helpers::Rect& rect, const helpers::Rect& px_area) const;
    helpers::Rect unit_to_px(const helpers::Rect& rect, const helpers::Rect& px_area) const;

    lm::dvec2 unit_per_px(lm::dvec2 px_size) const
    {
        return lm::dvec2{1, 1} / px_size * _visible_area.size();
    }

    void scroll_by(const lm::dvec2& value);
    void zoom_by(const lm::dvec2& value);
    void zoom_by(const lm::dvec2& value, const lm::dvec2& unit_target);

    void set_visible_range_x(double min, double max, bool smooth = false);
    void set_visible_range_y(double min, double max, bool smooth = false);

    void set_bounds(const std::optional<helpers::Rect>& bounds) { _bounds = bounds; }

    void set_zoom_bounds(const std::optional<helpers::Rect>& bounds) { _zoom_bounds = bounds; }

    constexpr bool get_flip_x() const { return _flip_x; }

    constexpr bool get_flip_y() const { return _flip_y; }

    helpers::Rect get_visible_area() const { return _visible_area; };

    const std::optional<helpers::Rect>& get_bounds() const { return _bounds; };

    const std::optional<helpers::Rect>& get_zoom_bounds() const { return _zoom_bounds; }

    lm::dvec2 get_current_zoom() const { return _view_size / _visible_area.size(); }

    ViewEvents catch_events(
        const helpers::Rect& area,
        bool prevent_scrolling = false,
        bool prevent_zooming = false) const;

    void process_events(const ViewEvents& events);
    void process_smoothing();

private:
    bool _flip_x = false;
    bool _flip_y = false;

    helpers::Rect _visible_area;
    helpers::Rect _target_visible_area;
    lm::dvec2 _view_size;

    std::optional<helpers::Rect> _bounds;
    std::optional<helpers::Rect> _zoom_bounds;
};

struct Layout
{
    helpers::Rect horizontal_ruler;
    helpers::Rect vertical_ruler;
    helpers::Rect main;

    static constexpr double RULER_WIDTH = 40.0;
    static constexpr double RULER_HEIGHT = 24.0;
};

// A RulerFormatter is responsible for formatting the values of the ruler graduations.
using RulerFormatter = std::function<void(fmt::memory_buffer&, helpers::MetricValue value)>;

struct Ruler
{
    data::MetricUnit unit;
    std::optional<RulerFormatter> special_formatter;

    static int get_subgraduation_count();
    static lm::dvec2 get_spacing();

    void draw_horizontal(const Layout&, const View&, bool with_ruler = true, bool with_grads = true)
        const;
    void draw_vertical(const Layout&, const View&, bool with_ruler = true, bool with_grads = true)
        const;

    double get_preferred_width_for_vertical(const View&, double ruler_height) const;

private:
    void _format_metric_value(fmt::memory_buffer& buffer, double value) const;
};

double compute_graduation_step_horizontal(double ruler_width, const View&);
double compute_graduation_step_vertical(double ruler_height, const View&);

// Returns a layout where the rulers are positionned differently depending on
// if the view is flipped or not.
Layout make_layout(
    const helpers::Rect& area,
    const View* view = nullptr,
    bool with_horizontal_ruler = false,
    bool with_vertical_ruler = false);

// Returns a layout with the vertical ruler on the left and the horizontal ruler
// on the top, with the specified pixel sizes.
Layout make_custom_layout(const helpers::Rect& area, double ruler_width, double ruler_height);

inline uint32_t get_default_background_color()
{
    return ImGui::GetColorU32(ImGuiCol_FrameBg);
}

void draw_background(const Layout& layout, std::optional<uint32_t> color = std::nullopt);

} // namespace ui::view
