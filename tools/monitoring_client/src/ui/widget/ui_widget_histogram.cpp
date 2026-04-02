#include "ui/widget/ui_widget_histogram.h"

#include "ui/ui_colors.h"

#include <cinttypes>

namespace ui::widget
{

using namespace helpers;

void HistogramRuler::draw_horizontal(const view::Layout& layout)
{
    const auto& area = layout.horizontal_ruler;

    double grad_spacing = area.size().x;
    int grad_step = 1;
    if (graduations.size() > 1)
    {
        grad_spacing /= (graduations.size() - 1);
    }

    static constexpr double GRAD_MIN_SPACING = 100.0;
    if (grad_spacing < GRAD_MIN_SPACING)
    {
        grad_step = std::ceil(GRAD_MIN_SPACING / grad_spacing);
    }

    auto* draw_list = ImGui::GetWindowDrawList();

    for (size_t i = 1; i < graduations.size(); ++i)
    {
        //      ^^^
        // Prefer starting drawing from the second graduation
        // as the first one usually is -inf
        //                  vvv
        if (i % grad_step != 1)
        {
            continue;
        }

        const double grad_x = area.p0.x + i * grad_spacing;
        const lm::dvec2 text_center = {grad_x, area.p0.y + area.size().y / 2.0};
        const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);

        helpers::draw_text_centered(draw_list, text_center, text_color, "{:.2Lf}", graduations[i]);
    }
}

Histogram::Histogram() : _view({{0, 0}, {1, 1}}, false, true) {}

void Histogram::set_metric(const std::optional<data::Metric>& metric)
{
    _selected_metric = metric;
    _refresh_horizontal_graduations = true;
}

void Histogram::draw(
    const Rect& area,
    const data::Database& database,
    context::ActionBus& action_bus)
{
    const auto& histograms_system = database.get_histograms();
    const auto& all_histograms = histograms_system.get_known_metrics();

    bool open_metrics_selector = ImGui::Button("Select histogram");
    bool metric_changed = filtered_metric_selector(
        "Select a histogram", open_metrics_selector, all_histograms, database.get_threads(),
        _selected_metric);
    if (metric_changed) _refresh_horizontal_graduations = true;

    ImGui::SameLine();
    if (!_selected_metric)
    {
        ImGui::TextDisabled("No histogram has been selected.");
        return;
    }
    else if (!histograms_system.get_updates_map().contains(*_selected_metric))
    {
        ImGui::TextDisabled("Unknown histogram, plase select another one.");
        return;
    }
    else
    {
        ImGui::Text("%s", _selected_metric->display_string.c_str());
    }

    const Rect main_area(ImGui::GetCursorScreenPos(), area.p1);
    auto layout = view::make_layout(main_area, &_view, true, true);
    view::draw_background(layout);

    _horizontal_ruler.draw_horizontal(layout);
    _vertical_ruler.draw_vertical(layout, _view);

    const auto& histogram = histograms_system.get_updates_map().at(*_selected_metric).back().value;
    const auto& buckets = histogram.get_buckets();
    const auto bucket_count = buckets.size();

    if (_refresh_horizontal_graduations)
    {
        std::vector<double> graduations(
            bucket_count + 1, -1.0 * std::numeric_limits<double>::infinity());
        for (size_t i = 0; i < buckets.size(); ++i)
        {
            graduations[i + 1] = buckets[i].max_value;
        }
        _horizontal_ruler.graduations = graduations;
        _refresh_horizontal_graduations = false;
    }

    const int64_t max_count = buckets[histogram.get_max_index()].count;
    auto bounds = Rect({0, 0}, {1, (double)max_count});
    _view.set_bounds(bounds);
    _view.set_visible_range_y(bounds.p0.y, bounds.p1.y);

    // Plotting
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < bucket_count; ++i)
    {
        ImGui::PushID(i);

        Rect unit_rect(
            {(double)i / (double)(bucket_count), 0.0},
            {(double)(i + 1) / (double)(bucket_count), (double)buckets[i].count});

        Rect px_rect = _view.unit_to_px(unit_rect, layout.main);

        // Add some spacing if bar is large enough
        if (px_rect.size().x > 3.0)
        {
            px_rect.p0.x += 1.0;
            px_rect.p1.x -= 1.0;
        }
        draw_list->AddRectFilled(px_rect.p0, px_rect.p1, color_set::MAGENTA.base);

        Rect interaction_rect = px_rect;
        interaction_rect.p0.y = layout.main.p0.y;

        if (interaction_rect.size().y > 0.0 && interaction_rect.size().x > 0.0)
        {
            ImGui::SetCursorScreenPos(interaction_rect.p0);
            ImGui::InvisibleButton("##Histogram bar", interaction_rect.size());

            if (ImGui::IsItemHovered())
            {
                double upper_bound = buckets[i].max_value;
                double lower_bound =
                    (i > 0) ? buckets[i - 1].max_value : -std::numeric_limits<double>::infinity();
                ImGui::SetTooltip(
                    "%" PRIu64 " \n%.0f < x < %.0f", buckets[i].count, lower_bound, upper_bound);

                draw_list->AddRectFilled(
                    interaction_rect.p0, interaction_rect.p1, color::get_hover_shadow());
            }
        }

        ImGui::PopID();
    }
}

} // namespace ui::widget
