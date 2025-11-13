#pragma once

#include "hrz/common/geo.h"

#include <earcut.hpp>
#include <lin_maths.h>

namespace mapbox::util
{
template<>
struct nth<0, lm::dvec2>
{
    inline static double get(const lm::dvec2& v) { return v.x; }
};

template<>
struct nth<1, lm::dvec2>
{
    inline static double get(const lm::dvec2& v) { return v.y; }
};

template<>
struct nth<0, lm::dvec3>
{
    inline static double get(const lm::dvec3& v) { return v.x; }
};

template<>
struct nth<1, lm::dvec3>
{
    inline static double get(const lm::dvec3& v) { return v.y; }
};

template<>
struct nth<0, hrz::GeoPosition2>
{
    inline static double get(const hrz::GeoPosition2& v) { return v.lat; }
};

template<>
struct nth<1, hrz::GeoPosition2>
{
    inline static double get(const hrz::GeoPosition2& v) { return v.lon; }
};
} // namespace mapbox::util
