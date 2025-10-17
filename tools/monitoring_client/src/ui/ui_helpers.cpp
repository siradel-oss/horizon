#include "ui/ui_helpers.h"

#include "data.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace
{
using namespace ui::helpers;

constexpr size_t BUFFER_SIZE = 512;
char s_buffer[BUFFER_SIZE];

fmt::memory_buffer s_fmt_buffer;

struct BaseSelectorResult
{
    bool change;
    std::optional<size_t> selected_id;
};

BaseSelectorResult _base_filtered_selector(
    const char* modal_title,
    bool open_now,
    std::span<const data::Metric> metrics,
    std::span<const data::Thread> threads,
    const char* null_option = nullptr,
    std::vector<bool>* selected = nullptr,
    bool force_same_unit = false)
{
    if (open_now)
    {
        ImGui::OpenPopup(modal_title);
    }

    BaseSelectorResult result = {};

    bool open_dummy = true;
    ImGui::SetNextWindowSize(lm::vec2(600.0f, 450.0f), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal(modal_title, &open_dummy))
    {
        static ImGuiTextFilter filter;
        bool autoselect_filtered = false;

        if (open_now) ImGui::SetKeyboardFocusHere();
        filter.Draw("Filter", available_rect().size().x - 70.0f);

        ImGui::SameLine();
        help_marker("Exclude entries containing \"foo\" by filtering with \"-foo\".");

        // Menu bar-style horizontal layout for multiselect buttons
        const bool multiselect = selected != nullptr;
        if (multiselect)
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            window->DC.LayoutType = ImGuiLayoutType_Horizontal;

            if (ImGui::Button("Select all"))
            {
                result.change = true;
                autoselect_filtered = true;
            }
            help_marker(
                "Select all of the visible filtered elements at once.\n"
                "Useful: type \"pass\" and use the button to select (most likely!) all GPU pass "
                "metrics.");
            ImGui::Separator();

            if (ImGui::Button("Select none"))
            {
                result.change = true;
                for (size_t i = 0; i < selected->size(); ++i)
                    (*selected)[i] = false;
            }

            window->DC.LayoutType = ImGuiLayoutType_Vertical;
            ImGui::NewLine();
        }

        const double footer_height =
            ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

        std::optional<data::MetricUnit> selected_unit = std::nullopt;
        if (force_same_unit && selected)
        {
            for (size_t i = 0; i < selected->size(); ++i)
            {
                if (selected->at(i) && i < metrics.size())
                {
                    selected_unit = metrics[i].unit;
                }
            }
        }

        if (null_option)
        {
            if (ImGui::Selectable(null_option, false))
            {
                result.selected_id = std::nullopt;
                result.change = true;
                ImGui::CloseCurrentPopup();
            }

            ImGui::Separator();
        }

        size_t num_selected = 0;
        size_t num_passed = 0;

        ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Hideable
            | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable
            | ImGuiTableFlags_NoBordersInBodyUntilResize | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable(
                "Metrics table", 3, flags,
                available_rect().trim(Direction::Down, footer_height).size()))
        {
            ImGui::TableSetupColumn("Name", 0, 160.0f);
            ImGui::TableSetupColumn("Labels", 0, 200.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < metrics.size(); ++i)
            {
                const auto& metric = metrics[i];

                bool is_selected = (multiselect && selected->at(i));
                bool enabled = !selected_unit || metric.unit == *selected_unit || is_selected;

                if (is_selected) num_selected++;

                if (!filter.PassFilter(metric.display_string.c_str())) continue;

                num_passed++;

                if (enabled && multiselect && autoselect_filtered)
                {
                    (*selected)[i] = true;
                    if (!selected_unit.has_value())
                    {
                        selected_unit = metric.unit;
                    }
                }

                ImGui::BeginDisabled(!enabled);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                ImGui::PushID(i);

                if (ImGui::Selectable(
                        metric.name.c_str(), is_selected,
                        ImGuiSelectableFlags_SpanAllColumns
                            | ImGuiSelectableFlags_AllowItemOverlap))
                {
                    result.selected_id = i;
                    result.change = true;

                    if (multiselect)
                    {
                        (*selected)[i] = !is_selected;
                        is_selected = !is_selected;
                    }
                    else
                    {
                        ImGui::CloseCurrentPopup();
                    }
                }

                bool show_tooltip = ImGui::IsItemHovered();

                ImGui::TableNextColumn();
                if (!metric.labels.empty())
                {
                    for (const auto& pair : metric.labels)
                    {
                        ImGui::Text("%s: %s", pair.first.c_str(), pair.second.c_str());
                        ImGui::SameLine();
                    }
                    ImGui::NewLine();
                }
                else
                {
                    ImGui::TextDisabled("None");
                }

                ImGui::EndDisabled();

                if (show_tooltip)
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(300.0f);

                    ImGui::Text("%s", metric.name.c_str());

                    ImGui::Separator();

                    if (!metric.labels.empty())
                    {
                        for (const auto& pair : metric.labels)
                        {
                            ImGui::Text("%s: %s", pair.first.c_str(), pair.second.c_str());
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("No labels");
                    }

                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }

                ImGui::PopID();
            }

            if (num_passed == 0)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("Nothing to show here!");
            }

            ImGui::EndTable();
        }

        // Footer
        ImGui::Separator();
        if (ImGui::Button(multiselect ? "Confirm selection" : "Close")) ImGui::CloseCurrentPopup();

        if (multiselect)
        {
            ImGui::SameLine();
            ImGui::Text("%zu elements selected", num_selected);

            if (selected_unit)
            {
                ImGui::SameLine(250.0f);
                ImGui::Text("(Unit: %s)", data::metric_unit_label(*selected_unit));
                ImGui::SameLine();
                help_marker("Only metrics with the same unit can be part of the same selection.");
            }
        }

        ImGui::EndPopup();
    }

    return result;
};

} // namespace

namespace ui::helpers
{
Rect Rect::fix() const
{
    Rect fixed = *this;

    if (fixed.p1.x < fixed.p0.x)
    {
        std::swap(fixed.p0.x, fixed.p1.x);
    }

    if (fixed.p1.y < fixed.p0.y)
    {
        std::swap(fixed.p0.y, fixed.p1.y);
    }

    return fixed;
}

Rect Rect::split(
    Direction direction,
    double distance,
    Rect* out_at_direction,
    Rect* out_other,
    double spacing) const
{
    Rect rect1 = *this;
    Rect rect2 = *this;

    switch (direction)
    {
        case Direction::Left:
            rect1.p1.x = rect1.p0.x + distance - spacing / 2.0;
            rect2.p0.x = rect1.p1.x + spacing / 2.0;
            break;

        case Direction::Right:
            rect1.p0.x = rect1.p1.x - distance + spacing / 2.0;
            rect2.p1.x = rect1.p0.x - spacing / 2.0;
            break;

        case Direction::Up:
            rect1.p1.y = rect1.p0.y + distance - spacing / 2.0;
            rect2.p0.y = rect1.p1.y + spacing / 2.0;
            break;

        case Direction::Down:
            rect1.p0.y = rect1.p1.y - distance + spacing / 2.0;
            rect2.p1.y = rect1.p0.y - spacing / 2.0;
            break;
    }

    if (out_at_direction)
    {
        *out_at_direction = rect1;
    }

    if (out_other)
    {
        *out_other = rect2;
    }

    return rect1;
}

void Rect::split_4(
    const lm::dvec2& position,
    Rect* topleft,
    Rect* topright,
    Rect* bottomleft,
    Rect* bottomright,
    double spacing) const
{
    if (topleft)
    {
        *topleft = Rect(p0, position - lm::vec2(spacing, spacing) / 2.0f);
    }

    if (topright)
    {
        *topright = Rect({position.x + spacing / 2.0f, p0.y}, {p1.x, position.y - spacing / 2.0f});
    }

    if (bottomleft)
    {
        *bottomleft =
            Rect({p0.x, position.y + spacing / 2.0f}, {position.x - spacing / 2.0f, p1.y});
    }

    if (bottomright)
    {
        *bottomright = Rect(position + lm::vec2(spacing, spacing) / 2.0f, p1);
    }
}

Rect Rect::project_into(const Rect& target_rect, const Rect& origin_rect) const
{
    Rect result;
    result.p0 = target_rect.p0 + (p0 - origin_rect.p0) / origin_rect.size() * target_rect.size();
    result.p1 = target_rect.p1 + (p1 - origin_rect.p1) / origin_rect.size() * target_rect.size();
    return result;
}

Rect Rect::operator+(const lm::dvec2& a) const
{
    return Rect(p0 + a, p1 + a);
}

Rect Rect::operator-(const lm::dvec2& a) const
{
    return Rect(p0 - a, p1 - a);
}

Rect& Rect::operator+=(const lm::dvec2& a)
{
    p0 += a;
    p1 += a;
    return *this;
}

Rect& Rect::operator-=(const lm::dvec2& a)
{
    p0 -= a;
    p1 -= a;
    return *this;
}

Rect Rect::trim(double amount) const
{
    Rect result(p0 + lm::vec2(amount, amount), p1 - lm::vec2(amount, amount));
    return result.fix();
}

Rect Rect::trim(lm::dvec2 amount) const
{
    Rect result(p0 + amount, p1 - amount);
    return result.fix();
}

Rect Rect::trim(Direction direction, double amount) const
{
    Rect result = *this;

    switch (direction)
    {
        case Direction::Left: result.p0.x += amount; break;
        case Direction::Right: result.p1.x -= amount; break;
        case Direction::Up: result.p0.y += amount; break;
        case Direction::Down: result.p1.y -= amount; break;
    }

    return result.fix();
}

Rect Rect::subrect(Direction direction, double size) const
{
    Rect result = *this;

    switch (direction)
    {
        case Direction::Left: result.p1.x = result.p0.x + size; break;
        case Direction::Right: result.p0.x = result.p1.x - size; break;
        case Direction::Up: result.p1.y = result.p0.y + size; break;
        case Direction::Down: result.p0.y = result.p1.y - size; break;
    }

    return result.fix();
}

Rect available_rect()
{
    const lm::dvec2 position = ImGui::GetCursorScreenPos();
    const lm::dvec2 size = ImGui::GetContentRegionAvail();

    return Rect(position, position + size);
}

const char* get_thread_name(uint32_t thread_id, std::span<const data::Thread> threads)
{
    if (thread_id < threads.size()) return threads[thread_id].name.c_str();

    if (thread_id == 0) return "Main thread";

    auto& buffer = static_fmt_memory_buffer();
    format_buffer(buffer, "Thread #{}", thread_id);
    return buffer.data();
}

fmt::memory_buffer& static_fmt_memory_buffer()
{
    return s_fmt_buffer;
}

void help_marker(const char* text)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

std::string short_text_fmt(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    int n = std::vsnprintf(s_buffer, BUFFER_SIZE, fmt, args);
    n = clamp(n, 0, (int)BUFFER_SIZE);

    va_end(args);

    return std::string(s_buffer);
}

lm::dvec2 short_text_size(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    int n = std::vsnprintf(s_buffer, BUFFER_SIZE, fmt, args);
    n = clamp(n, 0, (int)BUFFER_SIZE);

    va_end(args);

    return ImGui::CalcTextSize(s_buffer, s_buffer + n);
}

void short_text_centered(
    ImDrawList* draw_list,
    lm::dvec2 center,
    uint32_t color,
    const char* fmt,
    ...)
{
    va_list args;
    va_start(args, fmt);

    int n = std::vsnprintf(s_buffer, BUFFER_SIZE, fmt, args);
    n = clamp(n, 0, (int)BUFFER_SIZE);

    va_end(args);

    lm::dvec2 text_size = ImGui::CalcTextSize(s_buffer, s_buffer + n);
    draw_list->AddText(center - text_size / 2.0f, color, s_buffer, s_buffer + n);
}

void short_text_right_aligned(
    ImDrawList* draw_list,
    lm::dvec2 right_edge,
    uint32_t color,
    const char* fmt,
    ...)
{
    va_list args;
    va_start(args, fmt);

    int n = std::vsnprintf(s_buffer, BUFFER_SIZE, fmt, args);
    n = clamp(n, 0, (int)BUFFER_SIZE);

    va_end(args);

    lm::dvec2 text_size = ImGui::CalcTextSize(s_buffer, s_buffer + n);
    draw_list->AddText(right_edge - lm::dvec2{text_size.x, 0.0}, color, s_buffer, s_buffer + n);
}

bool filtered_metric_selector(
    const char* title,
    bool is_open,
    std::span<const data::Metric> metrics,
    std::span<const data::Thread> threads,
    std::optional<data::Metric>& selected,
    const char* null_option)
{
    auto result = _base_filtered_selector(title, is_open, metrics, threads, null_option);

    if (result.change)
    {
        selected =
            (result.selected_id) ? std::make_optional(metrics[*result.selected_id]) : std::nullopt;
        return true;
    }

    return false;
}

bool filtered_metric_multiselector(
    const char* title,
    bool is_open,
    std::span<const data::Metric> metrics,
    std::span<const data::Thread> threads,
    std::vector<bool>& selected,
    bool force_same_unit)
{
    auto result = _base_filtered_selector(
        title, is_open, metrics, threads, nullptr, &selected, force_same_unit);
    return result.change;
}

} // namespace ui::helpers
