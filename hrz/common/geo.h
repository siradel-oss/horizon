#pragma once

#include "hrz/common/maths.h"
#include "hrz/common/shader_defines.h"
#include "hrz/common/tile_coords.h"

#include <lin_maths.h>

#include <algorithm>
#include <optional>

namespace hrz
{

static constexpr double EARTH_RADIUS = HRZ_S_EARTH_RADIUS;
static constexpr double EARTH_CIRCUMFERENCE = EARTH_RADIUS * 2 * lm::PI;
static constexpr double STRAT_RADIUS = HRZ_S_STRAT_RADIUS;
static constexpr double WGS84_AXES_LENGTH_RATIO = HRZ_S_WGS84_AXES_LENGTH_RATIO;
static constexpr double WGS84_AXES_LENGTH_RATIO_2 =
    WGS84_AXES_LENGTH_RATIO * WGS84_AXES_LENGTH_RATIO;
static constexpr double WGS84_ECCENTRICITY = 8.1819190842622e-2;
static constexpr double WGS84_ECCENTRICITY_2 = WGS84_ECCENTRICITY * WGS84_ECCENTRICITY;
static constexpr double MERCATOR_MAX_LAT = 1.48442222974533236696; // = 2 * atan(e^pi) - pi/2
static constexpr double MERCATOR_MAX_LAT_DEG = MERCATOR_MAX_LAT * 57.2957795130823208768;
static constexpr double MERCATOR_MAX_LAT_METERS = EARTH_RADIUS * lm::PI;
static constexpr double HALF_MERCATOR_RANGE = MERCATOR_MAX_LAT_METERS;
static constexpr double MERCATOR_RANGE = 2.0 * HALF_MERCATOR_RANGE;
static constexpr int MERCATOR_TILE_SIZE = HRZ_S_MERCATOR_TILE_SIZE;
static constexpr int UNTILED_TILE_SIZE = HRZ_S_MERCATOR_TILE_SIZE;
static constexpr int CLIPMAP_SIZE = HRZ_S_CLIPMAP_SIZE;
static constexpr int CLIPMAP_LOD_COUNT = HRZ_S_CLIPMAP_LOD_COUNT;
static constexpr int ATLAS_TILE_SIZE = HRZ_S_ATLAS_TILE_SIZE;
static constexpr int ATLAS_TILE_BORDER_SIZE = HRZ_S_ATLAS_TILE_BORDER_SIZE;
static constexpr int MAX_IMAGERY_GROUP_COUNT = HRZ_S_MAX_IMAGERY_GROUP_COUNT;

// In radians.
struct GeoPosition2
{
    double lat;
    double lon;

    constexpr GeoPosition2() : lat(0), lon(0) {}

    constexpr GeoPosition2(double lat, double lon) : lat(lat), lon(lon) {}

    explicit constexpr GeoPosition2(const lm::dvec2& v) : lat(v.x), lon(v.y) {}

    explicit constexpr operator lm::dvec2() const { return lm::dvec2{lat, lon}; }

    constexpr bool operator ==(const GeoPosition2& other) const = default;
};

// In radians.
struct GeoPosition3
{
    double lat;
    double lon;
    double alt;

    constexpr GeoPosition3() : lat(0), lon(0), alt(0) {}

    constexpr GeoPosition3(double lat, double lon, double alt) : lat(lat), lon(lon), alt(alt) {}

    explicit constexpr GeoPosition3(const lm::dvec3& v) : lat(v.x), lon(v.y), alt(v.z) {}

    explicit constexpr operator lm::dvec3() const { return lm::dvec3{lat, lon, alt}; }

    constexpr GeoPosition2 latlon() const { return GeoPosition2(lat, lon); }

    constexpr bool operator ==(const GeoPosition3& other) const = default;
};

/**
 * Geographic bounds, in radians.
 *
 * Can define an area straddling the antimeridian if East > West.
 * Empty if South > North.
 * Zero-area bounds are not empty, as they can contain points.
 */
struct GeoBounds
{
    double west, east;
    double south, north;

    constexpr GeoBounds() : west(0.0), east(0.0), south(1.0), north(-1.0) {};

    constexpr GeoBounds(double west, double east, double south, double north) :
        west(west), east(east), south(south), north(north)
    {
    }

    constexpr GeoBounds(const GeoPosition2& min, const GeoPosition2& max) :
        west(min.lon), east(max.lon), south(min.lat), north(max.lat)
    {
    }

    constexpr bool operator ==(const GeoBounds& other) const = default;

    static constexpr GeoBounds empty() { return {0.0, 0.0, 1.0, -1.0}; }

    static constexpr GeoBounds full() { return {-lm::PI, lm::PI, -lm::PI * 0.5, lm::PI * 0.5}; }

    bool is_empty() const { return north < south; }
};

/**
 * Geographic bounds, in radians.
 * Heights in metres above the ellipsoid.
 *
 * Can define an area straddling the antimeridian if East > West.
 * Empty if South > North or if min height > max height.
 * Zero-area bounds are not empty, as they can contain points.
 */
struct GeoVolumeBounds
{
    double west, east;
    double south, north;
    double min_height, max_height;

    constexpr GeoVolumeBounds() :
        west(0.0), east(0.0), south(1.0), north(-1.0), min_height(1.0), max_height(-1.0) {};

    constexpr GeoVolumeBounds(
        double west,
        double east,
        double south,
        double north,
        double min_height,
        double max_height) :
        west(west),
        east(east),
        south(south),
        north(north),
        min_height(min_height),
        max_height(max_height)
    {
    }

    constexpr GeoVolumeBounds(const GeoPosition3& min, const GeoPosition3& max) :
        west(min.lon),
        east(max.lon),
        south(min.lat),
        north(max.lat),
        min_height(min.alt),
        max_height(max.alt)
    {
    }

    explicit constexpr GeoVolumeBounds(
        const GeoBounds& bounds,
        double min_height,
        double max_height) :
        west(bounds.west),
        east(bounds.east),
        south(bounds.south),
        north(bounds.north),
        min_height(min_height),
        max_height(max_height)
    {
    }

    constexpr GeoPosition3 center() const
    {
        return GeoPosition3(
            (south + north) / 2.0, (east + west) / 2.0, (min_height + max_height) / 2.0);
    }

    // Bit 0: latitude
    // Bit 1: longitude
    // Bit 2: altitude
    constexpr GeoPosition3 corner(uint32_t mask) const
    {
        assert(mask < 8);
        return GeoPosition3(
            mask & 1 ? north : south, mask & 2 ? east : west, mask & 4 ? max_height : min_height);
    }

    constexpr bool operator ==(const GeoVolumeBounds& other) const = default;

    static constexpr GeoVolumeBounds empty() { return {0.0, 0.0, 1.0, -1.0, 1.0, -1.0}; }

    static constexpr GeoVolumeBounds full()
    {
        return {
            -lm::PI,
            lm::PI,
            -lm::PI * 0.5,
            lm::PI * 0.5,
            std::numeric_limits<double>::lowest(),
            std::numeric_limits<double>::max()
        };
    }

    bool is_empty() const { return max_height < min_height || north < south; }

    GeoBounds to_flat() const { return GeoBounds(west, east, south, north); }
};

double earth_radius_at_latitude(double lat);

GeoPosition2 ecef_to_geo2(const lm::dvec3& ecef);
GeoPosition3 ecef_to_geo3(const lm::dvec3& ecef);

lm::dvec3 geo_to_ecef(double lat, double lon, double altitude = 0);

inline lm::dvec3 geo_to_ecef(const GeoPosition2& geo, double altitude = 0)
{
    return geo_to_ecef(geo.lat, geo.lon, altitude);
}

inline lm::dvec3 geo_to_ecef(const GeoPosition3& geo)
{
    return geo_to_ecef(geo.lat, geo.lon, geo.alt);
}

lm::dmat4 enu_to_ecef_rotation_matrix_for_geo(double lat, double lon);

inline lm::dmat4 enu_to_ecef_rotation_matrix_for_geo(const GeoPosition2& geo)
{
    return enu_to_ecef_rotation_matrix_for_geo(geo.lat, geo.lon);
}

lm::dmat4 ecef_to_enu_rotation_matrix_for_geo(double lat, double lon);

inline lm::dmat4 ecef_to_enu_rotation_matrix_for_geo(const GeoPosition2& geo)
{
    return ecef_to_enu_rotation_matrix_for_geo(geo.lat, geo.lon);
}

lm::dquat enu_to_ecef_quat_for_geo(double lat, double lon);

inline lm::dquat enu_to_ecef_quat_for_geo(const GeoPosition2& geo)
{
    return enu_to_ecef_quat_for_geo(geo.lat, geo.lon);
}

inline lm::dquat ecef_to_enu_quat_for_geo(double lat, double lon)
{
    return lm::conjugate(enu_to_ecef_quat_for_geo(lat, lon));
}

inline lm::dquat ecef_to_enu_quat_for_geo(const GeoPosition2& geo)
{
    return lm::conjugate(enu_to_ecef_quat_for_geo(geo.lat, geo.lon));
}

inline lm::ddual_quat enu_to_ecef_dual_quat_for_geo(double lat, double lon, double altitude = 0)
{
    lm::dvec3 pos = geo_to_ecef(lat, lon, altitude);
    return lm::translation_dquat(pos) * lm::ddual_quat(enu_to_ecef_quat_for_geo(lat, lon));
}

inline lm::ddual_quat enu_to_ecef_dual_quat_for_geo(const GeoPosition3& geo)
{
    return enu_to_ecef_dual_quat_for_geo(geo.lat, geo.lon, geo.alt);
}

inline lm::ddual_quat ecef_to_enu_dual_quat_for_geo(double lat, double lon, double altitude = 0)
{
    lm::dvec3 pos = geo_to_ecef(lat, lon, altitude);
    return lm::ddual_quat(ecef_to_enu_quat_for_geo(lat, lon)) * lm::translation_dquat(-pos);
}

inline lm::ddual_quat ecef_to_enu_dual_quat_for_geo(const GeoPosition3& geo)
{
    return ecef_to_enu_dual_quat_for_geo(geo.lat, geo.lon, geo.alt);
}

inline lm::dmat4 ecef_to_enu_transform_for_geo(double lat, double lon, double altitude = 0)
{
    lm::dvec3 pos = geo_to_ecef(lat, lon, altitude);
    return ecef_to_enu_rotation_matrix_for_geo(lat, lon) * lm::translation(-pos);
}

inline lm::dmat4 ecef_to_enu_transform_for_geo(const GeoPosition3& geo)
{
    return ecef_to_enu_transform_for_geo(geo.lat, geo.lon, geo.alt);
}

inline lm::dmat4 ecef_to_enu_transform_for_geo(const GeoPosition2& geo)
{
    return ecef_to_enu_transform_for_geo(geo.lat, geo.lon);
}

inline lm::dmat4 enu_to_ecef_transform_for_geo(double lat, double lon, double altitude = 0)
{
    lm::dvec3 pos = geo_to_ecef(lat, lon, altitude);
    return lm::translation(pos) * enu_to_ecef_rotation_matrix_for_geo(lat, lon);
}

inline lm::dmat4 enu_to_ecef_transform_for_geo(const GeoPosition3& geo)
{
    return enu_to_ecef_transform_for_geo(geo.lat, geo.lon, geo.alt);
}

inline lm::dmat4 enu_to_ecef_transform_for_geo(const GeoPosition2& geo)
{
    return enu_to_ecef_transform_for_geo(geo.lat, geo.lon);
}

lm::dvec3 geo_to_normal(double lat, double lon);

inline lm::dvec3 geo_to_normal(const GeoPosition2& geo)
{
    return geo_to_normal(geo.lat, geo.lon);
}

double geodesic_distance(double lat_a, double lon_a, double lat_b, double lon_b);

inline double geodesic_distance(const GeoPosition2& geo_a, const GeoPosition2& geo_b)
{
    return geodesic_distance(geo_a.lat, geo_a.lon, geo_b.lat, geo_b.lon);
}

GeoPosition2 geodesic_midpoint(double lat_a, double lon_a, double lat_b, double lon_b);

inline GeoPosition2 geodesic_midpoint(const GeoPosition2& a, const GeoPosition2& b)
{
    return geodesic_midpoint(a.lat, a.lon, b.lat, b.lon);
}

GeoPosition2 geodesic_interpolation(
    double lat_a,
    double lon_a,
    double lat_b,
    double lon_b,
    double t);

inline GeoPosition2 geodesic_interpolation(const GeoPosition2& a, const GeoPosition2& b, double t)
{
    return geodesic_interpolation(a.lat, a.lon, b.lat, b.lon, t);
}

GeoPosition2 rhumb_line_midpoint(
    const GeoPosition2& a,
    const GeoPosition2& b,
    bool allow_antimeridian_crossing);

double rhumb_line_distance(
    double lat_a,
    double lon_a,
    double lat_b,
    double lon_b,
    bool allow_antimeridian_crossing);

inline double rhumb_line_distance(
    const GeoPosition2& geo_a,
    const GeoPosition2& geo_b,
    bool allow_antimeridian_crossing)
{
    return rhumb_line_distance(
        geo_a.lat, geo_a.lon, geo_b.lat, geo_b.lon, allow_antimeridian_crossing);
}

// 0 points to the North,
// +pi/2 points to the East,
// pi points to the South,
// -pi/2 points to the West.
double bearing_between_points(const GeoPosition2& from, const GeoPosition2& to);

// 0 points to the North,
// +pi/2 points to the East,
// pi points to the South,
// -pi/2 points to the West.
double rhumb_line_bearing_between_web_mercator_points(
    const lm::dvec2& wmerc_from,
    const lm::dvec2& wmerc_to,
    bool allow_antimeridian_crossing);

// 0 points to the North,
// +pi/2 points to the East,
// pi points to the South,
// -pi/2 points to the West.
double rhumb_line_bearing_between_points(
    const GeoPosition2& from,
    const GeoPosition2& to,
    bool allow_antimeridian_crossing);

double normalize_longitude(double l);

inline void normalize_longitude(GeoPosition2& geo)
{
    geo.lon = normalize_longitude(geo.lon);
}

inline double clamp_latitude(double lat)
{
    return std::min(std::max(lat, -lm::PI / 2.0), lm::PI / 2.0);
}

void normalize_longitude(GeoPosition2& a_sph, GeoPosition2& b_sph, GeoPosition2& c_sph);

// This assumes 32 levels of detail.
// Latitudes above/below (-)85.05° will be clamped.
// Pixels are counted from the North.
lm::dvec2 geo_to_web_mercator_pixels(GeoPosition2 geo);

// This computes the distance between two web mercator coordinates in pixels
// taking into account the wrapping on the x axis.
double distance_web_mercator_pixels(lm::dvec2 p0, lm::dvec2 p1);

lm::dvec2 geo_to_web_mercator(GeoPosition2 geo);

inline lm::dvec3 geo_to_web_mercator(GeoPosition3 geo)
{
    return lm::dvec3(geo_to_web_mercator(geo.latlon()), geo.alt);
}

lm::dbbox2 geo_to_web_mercator(const GeoBounds& geo);

GeoPosition2 web_mercator_to_geo2(lm::dvec2 web_mercator);

inline GeoPosition3 web_mercator_to_geo3(lm::dvec3 web_mercator)
{
    auto geo2 = web_mercator_to_geo2(web_mercator.xy);
    return {geo2.lat, geo2.lon, web_mercator.z};
}

GeoBounds web_mercator_bounds_to_geo(const lm::dbbox2& web_mercator_bounds);

std::optional<TileCoords> geo_to_mercator_tile(
    const GeoPosition2& geo,
    uint8_t lod,
    bool tms_coords = false);

inline lm::dvec3 ecef_to_web_mercator_alt(lm::dvec3 pos)
{
    GeoPosition3 geo = ecef_to_geo3(pos);
    lm::dvec2 wm = geo_to_web_mercator(GeoPosition2{geo.lat, geo.lon});
    return lm::dvec3(wm, geo.alt);
}

inline lm::dvec3 web_mercator_to_ecef(lm::dvec2 web_mercator, double altitude = 0)
{
    GeoPosition2 geo = web_mercator_to_geo2(web_mercator);
    return geo_to_ecef(geo, altitude);
}

inline double mercator_tile_size_meters(TileCoords tile)
{
    return ((2 * MERCATOR_MAX_LAT_METERS) / (1 << tile.lod));
}

lm::dbbox2 mercator_tile_bbox_meters(TileCoords tile, bool tms_coords = false);

GeoBounds geodetic_tile_bounds(TileCoords tile, bool tms_coords = false);

lm::dvec2 mercator_tile_center_meters(TileCoords tile);

bool planet_intersection(const Ray& ray, lm::dvec3* hit, double altitude = 0);

// In the spherical Earth model.
double distance_to_horizon_squared(double altitude);

double web_mercator_distance(const lm::dvec2& a, const lm::dvec2& b);

// All latitudes are expected to be between -pi/2 and pi/2, and north > south.
inline bool is_in_spherical_segment(double north, double south, double lat, double tolerance = 0.0)
{
    return lat <= north + tolerance && lat >= south - tolerance;
}

// All longitudes are expected to have been normalized with
// `normalize_longitude`, and thus to be between -pi and pi. When the arc
// crosses the antimeridian, west > east, and this is valid!
inline bool is_in_spherical_wedge(double west, double east, double lon, double tolerance = 0.0)
{
    if (west <= east)
    {
        return lon >= west - tolerance && lon <= east + tolerance;
    }
    else
    {
        return lon <= east + tolerance || lon >= west - tolerance;
    }
}

inline bool contains(const GeoBounds& bounds, GeoPosition2 geo)
{
    if (bounds.is_empty()) return false;

    return is_in_spherical_segment(bounds.north, bounds.south, geo.lat)
        && is_in_spherical_wedge(bounds.west, bounds.east, geo.lon);
}

// Parameters should be normalised.
bool intersect(const GeoBounds& left, const GeoBounds& right);

inline GeoPosition2 normalize(const GeoPosition2& position)
{
    return {clamp_latitude(position.lat), normalize_longitude(position.lon)};
}

inline GeoPosition3 normalize(const GeoPosition3& position)
{
    return {clamp_latitude(position.lat), normalize_longitude(position.lon), position.alt};
}

GeoBounds normalize(const GeoBounds& bounds);

inline GeoVolumeBounds normalize(const GeoVolumeBounds& bounds)
{
    auto normalized = normalize(GeoBounds{bounds.west, bounds.east, bounds.south, bounds.north});
    return {normalized.west,  normalized.east,   normalized.south,
            normalized.north, bounds.min_height, bounds.max_height};
}

GeoBounds intersection(const GeoBounds& left, const GeoBounds& right);

GeoBounds merge(const GeoBounds& left, const GeoBounds& right);

GeoBounds mercator_tile_bounds(TileCoords tile, bool tms_coords = false);

double distance_to_wgs84_bbox(
    const lm::dvec3& ecef_pos,
    const GeoBounds& wgs84_bounds,
    double min_ele,
    double max_ele);

inline double distance_to_wgs84_bbox(const lm::dvec3& ecef_pos, const GeoVolumeBounds& wgs84_bounds)
{
    return distance_to_wgs84_bbox(
        ecef_pos, {wgs84_bounds.west, wgs84_bounds.east, wgs84_bounds.south, wgs84_bounds.north},
        wgs84_bounds.min_height, wgs84_bounds.max_height);
}

class ScreenToEllipsoidTransform
{
    lm::dmat4 _screen_to_ecef_direction;
    lm::dvec3 _origin;

public:
    ScreenToEllipsoidTransform(
        const lm::dvec3& position,
        const lm::dmat4& inv_pv_cc,
        const lm::vec2& subview_size_px);

    std::optional<lm::dvec3> planet_intersection(lm::vec2 view_pos, double altitude = 0) const;
};

} // namespace hrz
