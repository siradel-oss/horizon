#pragma once

#include "hrz_core_render.h"

#include <hrz_common_geo.h>

#include <gsl/gsl-lite.hpp>
#include <lin_maths.h>

#include <initializer_list>

namespace hrz
{
struct DebugDrawSystem;

namespace debug_draw
{
#define HRZ_DEBUG_DRAW_GROUPS     \
    HRZ_DEBUG_DRAW_GROUP(Default) \
    HRZ_DEBUG_DRAW_GROUP(Camera)  \
    HRZ_DEBUG_DRAW_GROUP(Vector)

enum Group
{

#define HRZ_DEBUG_DRAW_GROUP(NAME) Group_##NAME,
    HRZ_DEBUG_DRAW_GROUPS
#undef HRZ_DEBUG_DRAW_GROUP
        Group_Count
};

enum class TextAlign
{
    Left,
    Center,
    Right
};

enum class Space
{
    Clip = 0,
    Screen, // In pixels, origin at the top-left corner of the window
    Camera,
    Ecef,
    LatLonAltRad,
    LonLatAltRad,
    WebMercator,
};

DebugDrawSystem* create_system();

void destroy_system(DebugDrawSystem*, Render*);

void initialize_rendering(DebugDrawSystem*, Render*);

RenderRequest work_gpu(DebugDrawSystem*, Render*);

void draw(DebugDrawSystem*, Render*);
void draw_display(DebugDrawSystem*, my::RenderContext*);

void polyline(
    gsl::span<const double> coords,
    const lm::vec4& color = {1, 1, 1, 1},
    Space space = Space::LatLonAltRad,
    Group group = Group_Default);

void points(
    gsl::span<const double> coords,
    const lm::vec4& color = {1, 1, 1, 1},
    Space space = Space::LatLonAltRad,
    Group group = Group_Default);

void triangles(
    gsl::span<const double> coords,
    const lm::vec4& color = {1, 1, 1, 1},
    Space space = Space::LatLonAltRad,
    Group group = Group_Default);

// The `text` string is copied, its data doesn't need to remain valid after the call.
void text(
    std::string_view text,
    const lm::dvec3& position,
    const lm::vec4& color = {1, 1, 1, 1},
    TextAlign align = TextAlign::Center,
    Space space = Space::LonLatAltRad,
    Group group = Group_Default);

void texture(my::ResourceHandle texture_handle);

} // namespace debug_draw

struct DebugDraw
{
    void polyline(std::initializer_list<lm::dvec3> positions) const;
    void polyline(gsl::span<const lm::dvec3> positions) const;

    void polyline_geo(std::initializer_list<GeoPosition3> positions) const;
    void polyline_geo(gsl::span<const GeoPosition3> positions) const;

    void points(std::initializer_list<lm::dvec3> positions) const;
    void points(gsl::span<const lm::dvec3> positions) const;

    void points_geo(std::initializer_list<GeoPosition3> positions) const;
    void points_geo(gsl::span<const GeoPosition3> positions) const;

    void triangles(std::initializer_list<lm::dvec3> positions) const;
    void triangles(gsl::span<const lm::dvec3> positions) const;

    void triangles_geo(std::initializer_list<GeoPosition3> positions) const;
    void triangles_geo(gsl::span<const GeoPosition3> positions) const;

    void wgs84_box(const hrz::GeoVolumeBounds& bounds) const;

    void text(std::string_view text, const lm::dvec3& position) const;
    void point_text(std::string_view text, const lm::dvec3& position) const;
    void screen_text(std::string_view text, const lm::dvec2& position) const;
    void clip_text(std::string_view text, const lm::dvec2& position) const;

    void text_geo(std::string_view text, const GeoPosition3& position) const;
    void point_text_geo(std::string_view text, const GeoPosition3& position) const;

    lm::vec4 color;
    debug_draw::Group group;
    debug_draw::Space space;
    debug_draw::TextAlign text_align;

    DebugDraw(
        const lm::vec4& color = {1, 1, 1, 1},
        debug_draw::Group group = debug_draw::Group_Default,
        debug_draw::Space space = debug_draw::Space::Ecef,
        debug_draw::TextAlign text_align = debug_draw::TextAlign::Center) :
        color(color), group(group), space(space), text_align(text_align)
    {
    }

    DebugDraw with_color(const lm::vec4& color) const
    {
        auto dd = *this;
        dd.color = color;
        return dd;
    }

    DebugDraw with_space(debug_draw::Space space) const
    {
        auto dd = *this;
        dd.space = space;
        return dd;
    }

    DebugDraw with_align(debug_draw::TextAlign text_align) const
    {
        auto dd = *this;
        dd.text_align = text_align;
        return dd;
    }
};

} // namespace hrz
