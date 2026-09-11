// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "data.h"
#include "hrz/fnd/format.h"

#include <lin_maths.h>

#include <optional>
#include <span>
#include <string_view>

#define IM_VEC2_CLASS_EXTRA      \
    ImVec2(const lm::vec2& vec)  \
    {                            \
        x = vec.x;               \
        y = vec.y;               \
    }                            \
    ImVec2(const lm::dvec2& vec) \
    {                            \
        x = vec.x;               \
        y = vec.y;               \
    }                            \
    operator lm::dvec2() const   \
    {                            \
        return lm::dvec2(x, y);  \
    }

#define IM_VEC4_CLASS_EXTRA           \
    ImVec4(const lm::vec4& vec)       \
    {                                 \
        x = vec.x;                    \
        y = vec.y;                    \
        z = vec.z;                    \
        w = vec.w;                    \
    }                                 \
    ImVec4(const lm::dvec4& vec)      \
    {                                 \
        x = vec.x;                    \
        y = vec.y;                    \
        z = vec.z;                    \
        w = vec.w;                    \
    }                                 \
    operator lm::dvec4() const        \
    {                                 \
        return lm::dvec4(x, y, z, w); \
    }

#include <imgui.h>
#include <imgui_internal.h>

#include <string>
#include <vector>

namespace data
{

struct Metric;
enum class MetricUnit;

} // namespace data

namespace ui::helpers
{

template<typename T>
inline T clamp(T value, T min, T max)
{
    return (value < min) ? min : ((value > max) ? max : value);
}

template<typename T>
inline T map(T value, T src_min, T src_max, T dst_min, T dst_max)
{
    value = clamp(value, src_min, src_max);
    return dst_min + (value - src_min) / (src_max - src_min) * (dst_max - dst_min);
}

template<typename T>
inline T lerp(T a, T b, float t)
{
    t = clamp(t, 0.0F, 1.0F);
    return (1.0 - t) * a + t * b;
}

enum class Direction
{
    Left,
    Right,
    Up,
    Down
};

struct Rect
{
    lm::dvec2 p0; // top left
    lm::dvec2 p1; // bottom right

    Rect() : p0(), p1() {}

    Rect(const lm::dvec2& p0, const lm::dvec2& p1) : p0(p0), p1(p1) {}

    lm::dvec2 center() const { return (p0 + p1) / 2.0; }

    lm::dvec2 size() const { return p1 - p0; }

    double area() const { return size().x * size().y; }

    // Returns a rect where p0 and p1's components are switched if they don't
    // respect the order (p0's components should be inferior to p1's)
    Rect fix() const;

    bool contains(const lm::dvec2& pos) const
    {
        return pos.x >= p0.x && pos.y >= p0.y && pos.x <= p1.x && pos.y <= p1.y;
    }

    // Splits the rect in two, by cutting it at the specified distance from its edge
    // at the specified direction.
    //
    // Returns the resulting sub-rect at the specified direction. Its value is also
    // written to out_at_direction, and the value of the other resulting rect is written
    // to out_other (the pointers can be nullptr).
    Rect split(
        Direction direction,
        double distance,
        Rect* out_at_direction = nullptr,
        Rect* out_other = nullptr,
        double spacing = 0.0) const;

    void split_4(
        const lm::dvec2& absolute_position,
        Rect* topleft,
        Rect* topright,
        Rect* bottomleft,
        Rect* bottomright,
        double spacing = 0.0F) const;

    Rect project_into(const Rect& target_rect, const Rect& origin_rect) const;

    // Translation
    Rect operator +(const lm::dvec2& a) const;
    Rect operator -(const lm::dvec2& a) const;

    Rect& operator +=(const lm::dvec2& a);
    Rect& operator -=(const lm::dvec2& a);

    Rect offset(double x, double y) const { return *this + lm::dvec2(x, y); };

    Rect trim(double amount) const;
    Rect trim(lm::dvec2 amount) const;
    Rect trim(Direction dir, double amount) const;

    Rect expand(double amount) const { return trim(-amount); }

    Rect expand(Direction dir, double amount) const { return trim(dir, -amount); }

    Rect subrect(Direction dir, double size) const;
};

// Returns the (remaining) available area for content drawing in the current window.
Rect available_rect();

// Returns the thread name as defined in the given span if it is there, or a generic "Thread #X"
// if it is not.
// Do not hold on to the result.
const char* get_thread_name(uint32_t thread_id, std::span<const data::Thread> threads);

struct Duration
{
    int64_t us;

    Duration() : us(0) {}

    explicit Duration(int64_t us) : us(us) {}

    explicit Duration(double us) : us(us) {}
};

struct MemorySize
{
    size_t bytes;

    MemorySize() : bytes(0) {}

    explicit MemorySize(size_t bytes) : bytes(bytes) {}

    explicit MemorySize(double bytes) : bytes(bytes) {}
};

struct MetricValue
{
    double value;
    data::MetricUnit unit;

    MetricValue() : value(0), unit(data::MetricUnit::None) {}

    MetricValue(double value, data::MetricUnit unit) : value(value), unit(unit) {};
};

// Returns a fmt::memory_buffer that can be use to format strings at a lower cost.
// Use it for one-off formatting, as its content can be replaced by anything.
fmt::memory_buffer& static_fmt_memory_buffer();

// Clears the buffer and formats it as a null terminated string.
template<typename... Args>
void format_buffer(fmt::memory_buffer& buffer, fmt::format_string<Args...> fmt, Args&&... args)
{
    buffer.clear();
    fmt::format_to(std::back_inserter(buffer), fmt, std::forward<Args>(args)...);
    buffer.push_back(0);
}

template<typename... Args>
lm::dvec2 compute_text_size(fmt::format_string<Args...> fmt, Args&&... args)
{
    auto& buffer = static_fmt_memory_buffer();
    format_buffer(buffer, fmt, std::forward<Args>(args)...);

    return ImGui::CalcTextSize(buffer.data());
}

template<typename... Args>
void draw_text_centered(
    ImDrawList* draw_list,
    lm::dvec2 center,
    uint32_t col,
    fmt::format_string<Args...> fmt,
    Args&&... args)
{
    auto& buffer = static_fmt_memory_buffer();
    format_buffer(buffer, fmt, std::forward<Args>(args)...);

    lm::dvec2 text_size = ImGui::CalcTextSize(buffer.data());
    draw_list->AddText(center - text_size / 2.0F, col, buffer.data());
}

template<typename... Args>
void draw_text_right_aligned(
    ImDrawList* draw_list,
    lm::dvec2 right_edge,
    uint32_t col,
    fmt::format_string<Args...> fmt,
    Args&&... args)
{
    auto& buffer = static_fmt_memory_buffer();
    format_buffer(buffer, fmt, std::forward<Args>(args)...);

    lm::dvec2 text_size = ImGui::CalcTextSize(buffer.data());
    draw_list->AddText(right_edge - lm::dvec2{text_size.x, 0.0}, col, buffer.data());
}

void help_marker(const char* text);

bool filtered_metric_selector(
    const char* title,
    bool is_open,
    std::span<const data::Metric> metrics,
    std::span<const data::Thread> threads,
    std::optional<data::Metric>& selected,
    const char* null_option = nullptr);

bool filtered_metric_multiselector(
    const char* title,
    bool is_open,
    std::span<const data::Metric> metrics,
    std::span<const data::Thread> threads,
    std::vector<bool>& selected,
    bool force_same_unit = false);

} // namespace ui::helpers

template<>
struct fmt::formatter<ui::helpers::Duration>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const ui::helpers::Duration& timestamp, FormatContext& ctx) const
        -> decltype(ctx.out())
    {
        double us = (double)timestamp.us;
        if (us < 1000) return fmt::format_to(ctx.out(), "{:.0Lf} us", us);
        if (us < 100'000) return fmt::format_to(ctx.out(), "{:.2Lf} ms", us / 1000.0);
        if (us < 1'000'000) return fmt::format_to(ctx.out(), "{:.3Lf} s", us / 1'000'000.0);
        if (us < 10'000'000) return fmt::format_to(ctx.out(), "{:.2Lf} s", us / 1'000'000.0);
        return fmt::format_to(ctx.out(), "{:.1Lf} s", us / 1'000'000.0);
    }
};

template<>
struct fmt::formatter<ui::helpers::MemorySize>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const ui::helpers::MemorySize& size, FormatContext& ctx) const
        -> decltype(ctx.out())
    {
        int power_of_two = std::floor(std::log2(size.bytes));
        int unit = ui::helpers::clamp(power_of_two / 10, 0, 4);

        double value = (double)size.bytes;
        for (int i = 0; i < unit; ++i)
            value /= 1024.0;

        switch (unit)
        {
            case 0: return fmt::format_to(ctx.out(), "{:.0Lf} B", value);
            case 1: return fmt::format_to(ctx.out(), "{:.2Lf} KiB", value);
            case 2: return fmt::format_to(ctx.out(), "{:.2Lf} MiB", value);
            default:
            case 3: return fmt::format_to(ctx.out(), "{:.4Lf} GiB", value);
        }
    }
};

template<>
struct fmt::formatter<ui::helpers::MetricValue>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const ui::helpers::MetricValue& metric, FormatContext& ctx) const
        -> decltype(ctx.out())
    {
        switch (metric.unit)
        {
            case data::MetricUnit::Byte:
                return fmt::format_to(ctx.out(), "{}", (ui::helpers::MemorySize)metric.value);
            case data::MetricUnit::Microsecond:
                return fmt::format_to(ctx.out(), "{}", (ui::helpers::Duration)metric.value);
            default: return fmt::format_to(ctx.out(), "{:.2Lf}", metric.value);
        }
    }
};
