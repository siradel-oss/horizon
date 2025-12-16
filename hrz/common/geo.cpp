#include "hrz/common/geo.h"

#include <float.h>

double hrz::earth_radius_at_latitude(double lat)
{
    // https://en.wikipedia.org/wiki/Earth_radius#Geocentric_radius

    constexpr double a2 = EARTH_RADIUS * EARTH_RADIUS;
    constexpr double b2 =
        (EARTH_RADIUS * WGS84_AXES_LENGTH_RATIO) * (EARTH_RADIUS * WGS84_AXES_LENGTH_RATIO);

    const double cos_lat = std::cos(lat);
    const double sin_lat = std::sin(lat);

    const double cos2_lat = cos_lat * cos_lat;
    const double sin2_lat = sin_lat * sin_lat;

    const double numerator = a2 * a2 * cos2_lat + b2 * b2 * sin2_lat;
    const double denominator = a2 * cos2_lat + b2 * sin2_lat;

    return std::sqrt(numerator / denominator);
}

// See
// http://www.nalresearch.com/files/Standard%20Modems/A3LA-XG/A3LA-XG%20SW%20Version%201.0.0/GPS%20Technical%20Documents/GPS.G1-X-00006%20(Datum%20Transformations).pdf
hrz::GeoPosition2 hrz::ecef_to_geo2(const lm::dvec3& ecef)
{
    double lon = std::atan2(ecef.y, ecef.x);
    double p = lm::length(ecef.xy);

    static const double ep2 = (1 - hrz::WGS84_AXES_LENGTH_RATIO_2) / hrz::WGS84_AXES_LENGTH_RATIO_2;
    static const double e2 = 1 - hrz::WGS84_AXES_LENGTH_RATIO_2;

    double theta = std::atan2(ecef.z, p * hrz::WGS84_AXES_LENGTH_RATIO);

    double lat = std::atan2(
        ecef.z
            + ep2 * hrz::EARTH_RADIUS * hrz::WGS84_AXES_LENGTH_RATIO * std::pow(std::sin(theta), 3),
        p - e2 * hrz::EARTH_RADIUS * std::pow(std::cos(theta), 3));

    return {lat, lon};
}

// @Note There's a bit of duplicated code here. But it allows keeping things simple
//       and not forcing to compute the altitude when it's not needed.
hrz::GeoPosition3 hrz::ecef_to_geo3(const lm::dvec3& ecef)
{
    double lon = std::atan2(ecef.y, ecef.x);
    double p = lm::length(ecef.xy);

    static const double ep2 = (1 - hrz::WGS84_AXES_LENGTH_RATIO_2) / hrz::WGS84_AXES_LENGTH_RATIO_2;
    static const double e2 = 1 - hrz::WGS84_AXES_LENGTH_RATIO_2;

    double theta = std::atan2(ecef.z, p * hrz::WGS84_AXES_LENGTH_RATIO);

    double lat = std::atan2(
        ecef.z
            + ep2 * hrz::EARTH_RADIUS * hrz::WGS84_AXES_LENGTH_RATIO * std::pow(std::sin(theta), 3),
        p - e2 * hrz::EARTH_RADIUS * std::pow(std::cos(theta), 3));

    double n = hrz::EARTH_RADIUS / std::sqrt(1 - e2 * std::pow(std::sin(lat), 2));

    double alt;
    if (p > 1)
    {
        alt = p / std::cos(lat) - n;
    }
    else
    {
        // Close to the pole axis, just substract the Earth's radius to
        // the z value to get the altitude.
        alt = std::abs(ecef.z) - (hrz::EARTH_RADIUS * hrz::WGS84_AXES_LENGTH_RATIO);
    }

    return {lat, lon, alt};
}

// @Todo Sometimes we need to call many of these function in a row and
// thus many cos/sin calls are done multiple times. Maybe we could provide
// alternatives functions that use the cos/sin of the angles directly.
lm::dvec3 hrz::geo_to_ecef(double lat, double lon, double altitude)
{
    // See https://stackoverflow.com/a/25428344
    // See https://gist.github.com/klucar/1536194

    double cos_lon = std::cos(lon);
    double cos_lat = std::cos(lat);
    double sin_lon = std::sin(lon);
    double sin_lat = std::sin(lat);
    double n = hrz::EARTH_RADIUS / std::sqrt(1 - hrz::WGS84_ECCENTRICITY_2 * sin_lat * sin_lat);

    return lm::dvec3(
        (n + altitude) * cos_lat * cos_lon, (n + altitude) * cos_lat * sin_lon,
        ((1 - hrz::WGS84_ECCENTRICITY_2) * n + altitude) * sin_lat);
}

lm::dmat4 hrz::enu_to_ecef_rotation_matrix_for_geo(double lat, double lon)
{
    // See https://en.wikipedia.org/wiki/Geographic_coordinate_conversion#From_ENU_to_ECEF
    // It is the transpose of `ecef_to_enu_rotation_matrix_for_geo()`'s matrix.
    double sin_lat = std::sin(lat);
    double cos_lat = std::cos(lat);
    double sin_lon = std::sin(lon);
    double cos_lon = std::cos(lon);
    return lm::dmat4{
        {-sin_lon, cos_lon, 0, 0},
        {-sin_lat * cos_lon, -sin_lat * sin_lon, cos_lat, 0},
        {cos_lat * cos_lon, cos_lat * sin_lon, sin_lat, 0},
        {0, 0, 0, 1}};
}

lm::dmat4 hrz::ecef_to_enu_rotation_matrix_for_geo(double lat, double lon)
{
    // See https://en.wikipedia.org/wiki/Geographic_coordinate_conversion#From_ECEF_to_ENU
    // It is the transpose of `enu_to_ecef_rotation_matrix_for_geo()`'s matrix.
    double sin_lat = std::sin(lat);
    double cos_lat = std::cos(lat);
    double sin_lon = std::sin(lon);
    double cos_lon = std::cos(lon);
    return lm::dmat4{
        {-sin_lon, -sin_lat * cos_lon, cos_lat * cos_lon, 0},
        {cos_lon, -sin_lat * sin_lon, cos_lat * sin_lon, 0},
        {0, cos_lat, sin_lat, 0},
        {0, 0, 0, 1}};
}

lm::dquat hrz::enu_to_ecef_quat_for_geo(double lat, double lon)
{
    // This is the quaternion that rotates the ENU frame at (0, 0) to ECEF.
    // Unroll the matrix_to_quaternion function by hand for the following matrix
    // and you get this result. The magic is the default ENU to ECEF rotation matrix.
    // No magic.
    // [ 0 0 1 ]
    // [ 1 0 0 ]
    // [ 0 1 0 ]
    static const lm::dquat enu_to_ecef{0.5, 0.5, 0.5, 0.5};

    // Rotation for latitude (around ECEF y axis)
    lm::dquat rot_lat{0.0, -std::sin(lat / 2), 0.0, std::cos(lat / 2)};

    // Rotation for longitude (around ECEF z axis)
    lm::dquat rot_lon{0.0, 0.0, std::sin(lon / 2), std::cos(lon / 2)};

    return rot_lon * rot_lat * enu_to_ecef;
}

lm::dvec3 hrz::geo_to_normal(double lat, double lon)
{
    double cos_lat = std::cos(lat);
    return lm::normalize(
        lm::dvec3(cos_lat * std::cos(lon), cos_lat * std::sin(lon), std::sin(lat)));
}

double hrz::geodesic_distance(double lat_a, double lon_a, double lat_b, double lon_b)
{
    // This is the haversine formula

    // @Todo Use a version of this computation that takes
    // the Earth's eccentricity into account.

    double hav_lat = std::sin((lat_b - lat_a) * 0.5);
    double hav_lon = std::sin((lon_b - lon_a) * 0.5);
    double h = hav_lat * hav_lat + std::cos(lat_a) * std::cos(lat_b) * hav_lon * hav_lon;

    // "When using these formulae, one must ensure that h does not exceed 1 due
    // to a floating point error."
    // From https://en.wikipedia.org/wiki/Haversine_formula
    if (h > 1.0) h = 1.0;

    return 2.0 * hrz::EARTH_RADIUS * std::asin(std::sqrt(h));
}

hrz::GeoPosition2 hrz::geodesic_midpoint(double lat_a, double lon_a, double lat_b, double lon_b)
{
    double cos_lat_a = std::cos(lat_a);
    double cos_lat_b = std::cos(lat_b);
    double bx = cos_lat_b * std::cos(lon_b - lon_a);
    double by = cos_lat_b * std::sin(lon_b - lon_a);

    double lat = std::atan2(
        std::sin(lat_a) + std::sin(lat_b),
        std::sqrt((cos_lat_a + bx) * (cos_lat_a + bx) + by * by));

    double lon = lon_a + std::atan2(by, cos_lat_a + bx);

    return GeoPosition2(lat, lon);
}

hrz::GeoPosition2 hrz::geodesic_interpolation(
    double lat_a,
    double lon_a,
    double lat_b,
    double lon_b,
    double t)
{
    // From https://www.movable-type.co.uk/scripts/latlong.html

    double delta = geodesic_distance(lat_a, lon_a, lat_b, lon_b) / hrz::EARTH_RADIUS;

    double cos_lat_a = std::cos(lat_a);
    double sin_lat_a = std::sin(lat_a);
    double cos_lon_a = std::cos(lon_a);
    double sin_lon_a = std::sin(lon_a);
    double cos_lat_b = std::cos(lat_b);
    double sin_lat_b = std::sin(lat_b);
    double cos_lon_b = std::cos(lon_b);
    double sin_lon_b = std::sin(lon_b);
    double sin_delta = std::sin(delta);

    double a = std::sin((1.0 - t) * delta) / sin_delta;
    double b = std::sin(t * delta) / sin_delta;
    double x = a * cos_lat_a * cos_lon_a + b * cos_lat_b * cos_lon_b;
    double y = a * cos_lat_a * sin_lon_a + b * cos_lat_b * sin_lon_b;
    double z = a * sin_lat_a + b * sin_lat_b;

    double lat = std::atan2(z, std::sqrt(x * x + y * y));
    double lon = std::atan2(y, x);

    return GeoPosition2(lat, lon);
}

hrz::GeoPosition2 hrz::rhumb_line_midpoint(
    const GeoPosition2& a,
    const GeoPosition2& b,
    bool allow_antimeridian_crossing)
{
    auto wmerc_a = geo_to_web_mercator(a);
    auto wmerc_b = geo_to_web_mercator(b);

    if (!allow_antimeridian_crossing || std::abs(wmerc_b.x - wmerc_a.x) <= hrz::HALF_MERCATOR_RANGE)
    {
        return web_mercator_to_geo2(lm::mix(wmerc_a, wmerc_b, 0.5));
    }
    else
    {
        // The segment goes across the antimeridian.
        lm::dvec2 wmerc_midpoint;

        if (wmerc_a.x < wmerc_b.x)
        {
            auto a = wmerc_a;
            a.x += hrz::MERCATOR_RANGE;
            wmerc_midpoint = lm::mix(a, wmerc_b, 0.5);
        }
        else
        {
            auto b = wmerc_b;
            b.x += hrz::MERCATOR_RANGE;
            wmerc_midpoint = lm::mix(wmerc_a, b, 0.5);
        }

        if (wmerc_midpoint.x > hrz::HALF_MERCATOR_RANGE)
        {
            wmerc_midpoint.x -= hrz::MERCATOR_RANGE;
        }
        else if (wmerc_midpoint.x < -hrz::HALF_MERCATOR_RANGE)
        {
            wmerc_midpoint.x += hrz::MERCATOR_RANGE;
        }

        return web_mercator_to_geo2(wmerc_midpoint);
    }
}

double hrz::rhumb_line_distance(
    double lat_a,
    double lon_a,
    double lat_b,
    double lon_b,
    bool allow_antimeridian_crossing)
{
    // From https://www.movable-type.co.uk/scripts/latlong.html

    double delta_lat = std::abs(lat_a - lat_b);
    double delta_lon = std::abs(lon_a - lon_b);

    double delta_psi =
        std::log(std::tan(lm::PI / 4 + lat_b / 2) / std::tan(lm::PI / 4 + lat_a / 2));

    // E-W course becomes ill-conditioned with 0/0
    double q = std::abs(delta_psi) > 10e-12 ? delta_lat / delta_psi : std::cos(lat_a);

    // if delta_lon over 180° take shorter rhumb line across the anti-meridian:
    if (allow_antimeridian_crossing && std::abs(delta_lon) > lm::PI)
    {
        delta_lon = delta_lon > 0 ? -(2 * lm::PI - delta_lon) : (2 * lm::PI + delta_lon);
    }

    return std::sqrt(delta_lat * delta_lat + q * q * delta_lon * delta_lon) * hrz::EARTH_RADIUS;
}

double hrz::bearing_between_points(const GeoPosition2& from, const GeoPosition2& to)
{
    // From
    // https://www.igismap.com/formula-to-find-bearing-or-heading-angle-between-two-points-latitude-longitude/
    double delta_lon = to.lon - from.lon;
    double x = std::cos(to.lat) * std::sin(delta_lon);
    double y = std::cos(from.lat) * std::sin(to.lat)
        - std::sin(from.lat) * std::cos(to.lat) * std::cos(delta_lon);

    return std::atan2(x, y);
}

double hrz::rhumb_line_bearing_between_web_mercator_points(
    const lm::dvec2& wmerc_from,
    const lm::dvec2& wmerc_to,
    bool allow_antimeridian_crossing)
{
    lm::dvec2 direction;
    if (allow_antimeridian_crossing
        && std::abs(wmerc_to.x - wmerc_from.x) > hrz::HALF_MERCATOR_RANGE)
    {
        if (wmerc_to.x > wmerc_from.x)
        {
            auto to = wmerc_to;
            to.x -= hrz::MERCATOR_RANGE;
            direction = to - wmerc_from;
        }
        else
        {
            auto from = wmerc_from;
            from.x -= hrz::MERCATOR_RANGE;
            direction = from - wmerc_from;
        }
    }
    else
    {
        direction = wmerc_to - wmerc_from;
    }

    // `atan2` is usually called with y as first parameter and x as second parameter.
    // Here we have x then y. But it's not an error! This is to honour the contract regarding
    // the return value, where 0 points to the North and +pi/2 to the East.
    return std::atan2(direction.x, direction.y);
}

double hrz::rhumb_line_bearing_between_points(
    const GeoPosition2& from,
    const GeoPosition2& to,
    bool allow_antimeridian_crossing)
{
    return rhumb_line_bearing_between_web_mercator_points(
        hrz::geo_to_web_mercator(from), hrz::geo_to_web_mercator(to), allow_antimeridian_crossing);
}

double hrz::normalize_longitude(double l)
{
    if (hrz::flt_near(std::abs(l), lm::PI, std::numeric_limits<double>::epsilon()))
    {
        return l < 0 ? -lm::PI : lm::PI;
    }

    l = std::fmod(l, lm::PI * 2.0);
    if (l < -lm::PI) l += lm::PI * 2.0;
    if (l > lm::PI) l -= lm::PI * 2.0;
    return l;
}

void hrz::normalize_longitude(GeoPosition2& a_sph, GeoPosition2& b_sph, GeoPosition2& c_sph)
{
    // We want to make sure all points are in the same quadrant
    // so that the haversine geodesic midpoint is stable.

    // We set the poles longitude to one of the other point's.
    // This works because only one point at most can be a pole in
    // each patch.
    if (std::abs(std::cos(a_sph.lat)) < FLT_EPSILON)
    {
        a_sph.lon = b_sph.lon;
    }

    if (std::abs(std::cos(b_sph.lat)) < FLT_EPSILON)
    {
        b_sph.lon = c_sph.lon;
    }

    if (std::abs(std::cos(c_sph.lat)) < FLT_EPSILON)
    {
        c_sph.lon = a_sph.lon;
    }

    // Set all longitudes between -PI and PI.
    normalize_longitude(a_sph);
    normalize_longitude(b_sph);
    normalize_longitude(c_sph);

    // Then we modulate the points longitudes to that they are
    // never more than PI radians away from each other.
    while (a_sph.lon - b_sph.lon > lm::PI)
    {
        b_sph.lon += lm::PI * 2;
    }

    while (a_sph.lon - b_sph.lon < -lm::PI)
    {
        b_sph.lon -= lm::PI * 2;
    }

    while (a_sph.lon - c_sph.lon > lm::PI)
    {
        c_sph.lon += lm::PI * 2;
    }

    while (a_sph.lon - c_sph.lon < -lm::PI)
    {
        c_sph.lon -= lm::PI * 2;
    }
}

lm::dvec2 hrz::geo_to_web_mercator_pixels(GeoPosition2 geo)
{
    static const double max_lat = MERCATOR_MAX_LAT;
    static const double size = std::pow(2.0, 31.0) / (2 * lm::PI);

    normalize_longitude(geo);

    geo.lat = std::max(-max_lat, geo.lat);
    geo.lat = std::min(max_lat, geo.lat);

    double x = (geo.lon + lm::PI) * size;
    double y = (lm::PI - std::log(std::tan(lm::PI / 4 + geo.lat / 2))) * size;
    return lm::dvec2(x, y);
}

double hrz::distance_web_mercator_pixels(lm::dvec2 p0, lm::dvec2 p1)
{
    static const double size = std::pow(2.0, 31.0);
    double dx = std::abs(p1.x - p0.x);
    if (dx > size * 0.5)
    {
        dx = size - dx;
    }
    return lm::length(lm::dvec2(dx, p1.y - p0.y));
}

lm::dvec2 hrz::geo_to_web_mercator(GeoPosition2 geo)
{
    normalize_longitude(geo);

    double x = (geo.lon / lm::PI) * HALF_MERCATOR_RANGE;
    double y;

    if (std::abs(geo.lat) - lm::PI / 2 > -std::numeric_limits<double>::epsilon())
    {
        constexpr double inf = std::numeric_limits<double>::infinity();
        y = geo.lat < 0 ? -inf : inf;
    }
    else
    {
        y = std::log(std::tan(lm::PI / 4 + geo.lat / 2)) * EARTH_RADIUS;
    }

    return lm::dvec2(x, y);
}

lm::dbbox2 hrz::geo_to_web_mercator(const GeoBounds& geo)
{
    return lm::dbbox2(
        geo_to_web_mercator(GeoPosition2(geo.south, geo.west)),
        geo_to_web_mercator(GeoPosition2(geo.north, geo.east)));
}

hrz::GeoPosition2 hrz::web_mercator_to_geo2(lm::dvec2 web_mercator)
{
    double lat =
        (std::atan(std::exp((web_mercator.y / MERCATOR_MAX_LAT_METERS) * lm::PI)) - lm::PI / 4) * 2;
    double lon = (web_mercator.x / MERCATOR_MAX_LAT_METERS) * lm::PI;
    return {lat, lon};
}

hrz::GeoBounds hrz::web_mercator_bounds_to_geo(const lm::dbbox2& web_mercator_bounds)
{
    return GeoBounds(
        hrz::web_mercator_to_geo2(web_mercator_bounds.min),
        hrz::web_mercator_to_geo2(web_mercator_bounds.max));
}

std::optional<hrz::TileCoords> hrz::geo_to_mercator_tile(
    const GeoPosition2& geo,
    uint8_t lod,
    bool tms_coords)
{
    auto wmerc = geo_to_web_mercator(geo);

    if (std::abs(wmerc.y) >= hrz::MERCATOR_MAX_LAT_METERS)
    {
        return std::nullopt;
    }

    uint32_t tile_count_at_lod = 1 << lod;
    double tile_size = hrz::MERCATOR_RANGE / tile_count_at_lod;

    uint32_t tile_x = (uint32_t)((wmerc.x + hrz::HALF_MERCATOR_RANGE) / tile_size);
    uint32_t tile_y = (uint32_t)((wmerc.y + hrz::HALF_MERCATOR_RANGE) / tile_size);

    if (!tms_coords)
    {
        tile_y = (tile_count_at_lod - 1) - tile_y;
    }

    return TileCoords(tile_x, tile_y, lod);
}

lm::dbbox2 hrz::mercator_tile_bbox_meters(TileCoords tile, bool tms_coords)
{
    double tile_size = mercator_tile_size_meters(tile);
    uint32_t tile_y = tms_coords ? tile.y : (1 << tile.lod) - 1 - tile.y;
    return lm::dbbox2(
        {tile.x * tile_size - MERCATOR_MAX_LAT_METERS,
         tile_y * tile_size - MERCATOR_MAX_LAT_METERS},
        {(tile.x + 1) * tile_size - MERCATOR_MAX_LAT_METERS,
         (tile_y + 1) * tile_size - MERCATOR_MAX_LAT_METERS});
}

hrz::GeoBounds hrz::geodetic_tile_bounds(TileCoords tile, bool tms_coords)
{
    double tile_size = lm::PI / (1 << tile.lod);
    uint32_t tile_y = tms_coords ? tile.y : (1 << tile.lod) - 1 - tile.y;

    return GeoBounds{
        tile.x * tile_size - lm::PI, (tile.x + 1) * tile_size - lm::PI,
        tile_y * tile_size - lm::PI / 2.0, (tile_y + 1) * tile_size - lm::PI / 2.0};
}

lm::dvec2 hrz::mercator_tile_center_meters(TileCoords tile)
{
    double tile_size = mercator_tile_size_meters(tile);
    uint32_t tile_y = (1 << tile.lod) - 1 - tile.y;
    return lm::dvec2(
        (tile.x + 0.5) * tile_size - MERCATOR_MAX_LAT_METERS,
        (tile_y + 0.5) * tile_size - MERCATOR_MAX_LAT_METERS);
}

bool hrz::planet_intersection(const Ray& ray, lm::dvec3* hit, double altitude)
{
    static const lm::dvec3 inv_wgs84_ellipsoid(1.0, 1.0, 1.0 / hrz::WGS84_AXES_LENGTH_RATIO);

    const lm::dvec3 origin_sphere = ray.o * inv_wgs84_ellipsoid;
    const lm::dvec3 dir_sphere = lm::normalize(ray.dir * inv_wgs84_ellipsoid);

    double a = lm::length2(dir_sphere);
    double b = 2 * lm::dot(dir_sphere, origin_sphere);
    double c = lm::length2(origin_sphere)
        - (hrz::EARTH_RADIUS + altitude) * (hrz::EARTH_RADIUS + altitude);
    double d = b * b - 4 * a * c;

    if (d >= 0)
    {
        const double sqrt_delta = std::sqrt(d);
        const double t0 = (-b - sqrt_delta) / (2 * a);
        const double t1 = (-b + sqrt_delta) / (2 * a);
        double t = std::min(t0, t1);
        if (t < 0) t = std::max(t0, t1);

        if (t > 0)
        {
            *hit = ray.o + ray.dir * t;
            return true;
        }
    }

    return false;
}

double hrz::distance_to_horizon_squared(double altitude)
{
    return std::max(0.0, altitude * (hrz::EARTH_RADIUS * 2.0 + altitude));
}

double hrz::web_mercator_distance(const lm::dvec2& a, const lm::dvec2& b)
{
    // Compute distance between two mercator positions with a correction. See
    // https://gis.stackexchange.com/questions/14528/better-distance-measurements-in-web-mercator-projection/14532#14532
    static const double e = 0.081819191;
    static const double e_sq = e * e;

    const GeoPosition2 a_geo = hrz::web_mercator_to_geo2(a);
    const GeoPosition2 b_geo = hrz::web_mercator_to_geo2(b);

    const double mean_lat = lm::radians((a_geo.lat + b_geo.lat) * 0.5);
    const double cos_mean_lat = std::cos(mean_lat);
    const double sin_mean_lat = std::sin(mean_lat);
    const double sin_mean_lat_sq = sin_mean_lat * sin_mean_lat;

    const double dx = std::abs(b.x - a.x);
    const double dy = std::abs(b.y - a.y);
    const double adjusted_dx = dx * cos_mean_lat / std::sqrt(1.0 - e_sq * sin_mean_lat_sq);
    const double adjusted_dy =
        dy * cos_mean_lat * (1.0 - e_sq) / std::pow(1.0 - e_sq * sin_mean_lat_sq, 1.5);

    return std::sqrt(adjusted_dx * adjusted_dx + adjusted_dy * adjusted_dy);
}

bool hrz::intersect(const GeoBounds& left, const GeoBounds& right)
{
    if (left.is_empty() || right.is_empty()) return false;

    bool left_crosses_180 = left.east < left.west;
    bool right_crosses_180 = right.east < right.west;

    double left_lon_min = std::min(left.west, left.east);
    double left_lon_max = std::max(left.west, left.east);
    double right_lon_min = std::min(right.west, right.east);
    double right_lon_max = std::max(right.west, right.east);

    bool lon_intersection = true;

    if (!left_crosses_180 && !right_crosses_180)
    {
        lon_intersection = !(right_lon_max < left_lon_min || right_lon_min > left_lon_max);
    }
    else if (!left_crosses_180 && right_crosses_180)
    {
        lon_intersection = !(left_lon_min >= right_lon_min && left_lon_max <= right_lon_max);
    }
    else if (left_crosses_180 && !right_crosses_180)
    {
        lon_intersection = !(right_lon_min >= left_lon_min && right_lon_max <= left_lon_max);
    }
    // Else they both intersect the antimeridian and therefore each other.

    bool lat_intersection = !(right.north < left.south || right.south > left.north);

    return lon_intersection && lat_intersection;
}

hrz::GeoBounds hrz::normalize(const GeoBounds& bounds)
{
    GeoBounds norm_bounds;

    if (hrz::flt_eq(bounds.east, bounds.west))
    {
        norm_bounds.west = bounds.west;
        norm_bounds.east = bounds.west;
    }
    else if (hrz::flt_near(std::abs(bounds.east - bounds.west), lm::PI * 2.0, 1e-9))
    {
        norm_bounds.west = -lm::PI;
        norm_bounds.east = lm::PI;
    }
    else
    {
        norm_bounds.west = hrz::normalize_longitude(bounds.west);
        norm_bounds.east = hrz::normalize_longitude(bounds.east);
    }

    if (hrz::flt_eq(bounds.north, bounds.south))
    {
        norm_bounds.south = bounds.south;
        norm_bounds.north = bounds.south;
    }
    else if (bounds.north < bounds.south)
    {
        norm_bounds.south = 1.0;
        norm_bounds.north = -1.0;
    }
    else if (bounds.north - bounds.south >= lm::PI - 0.000001)
    {
        norm_bounds.south = -lm::PI * 0.5;
        norm_bounds.north = lm::PI * 0.5;
    }
    else
    {
        norm_bounds.south = hrz::clamp_latitude(bounds.south);
        norm_bounds.north = hrz::clamp_latitude(bounds.north);
    }

    return norm_bounds;
}

hrz::GeoBounds hrz::intersection(const GeoBounds& left, const GeoBounds& right)
{
    auto l = normalize(left);
    auto r = normalize(right);

    if (l.is_empty() || r.is_empty()) return GeoBounds::empty();

    if (l.west > l.east) l.west -= lm::PI * 2.0;
    if (r.west > r.east) r.west -= lm::PI * 2.0;

    return normalize(GeoBounds{
        std::max(l.west, r.west), std::min(l.east, r.east), std::max(l.south, r.south),
        std::min(l.north, r.north)});
}

hrz::GeoBounds hrz::merge(const GeoBounds& left, const GeoBounds& right)
{
    auto l = normalize(left);
    auto r = normalize(right);

    if (l.is_empty()) return r;
    if (r.is_empty()) return l;

    if (l.west > l.east) l.west -= lm::PI * 2.0;
    if (r.west > r.east) r.west -= lm::PI * 2.0;

    return normalize(GeoBounds{
        std::min(l.west, r.west), std::max(l.east, r.east), std::min(l.south, r.south),
        std::max(l.north, r.north)});
}

hrz::GeoBounds hrz::mercator_tile_bounds(TileCoords tile, bool tms_coords)
{
    lm::dbbox2 mercator_bbox = mercator_tile_bbox_meters(tile, tms_coords);

    auto south_west = web_mercator_to_geo2(mercator_bbox.min);
    auto north_east = web_mercator_to_geo2(mercator_bbox.max);

    GeoBounds bounds;
    bounds.south = south_west.lat;
    bounds.north = north_east.lat;
    bounds.west = south_west.lon;
    bounds.east = north_east.lon;

    return bounds;
}

double hrz::distance_to_wgs84_bbox(
    const lm::dvec3& ecef_pos,
    const GeoBounds& wgs84_bounds,
    double min_ele,
    double max_ele)
{
    if (wgs84_bounds.is_empty()) return std::numeric_limits<double>::max();

    auto lon_sub = [](double a, double b)
    {
        double diff = a - b;
        while (diff < -lm::PI)
        {
            diff += 2 * lm::PI;
        }
        while (diff > lm::PI)
        {
            diff -= 2 * lm::PI;
        }
        return diff;
    };

    auto geo_pos = hrz::ecef_to_geo3(ecef_pos);

    double ele_diff = 0;
    if (geo_pos.alt < min_ele) ele_diff = min_ele - geo_pos.alt;
    if (geo_pos.alt > max_ele) ele_diff = geo_pos.alt - max_ele;

    if (hrz::contains(wgs84_bounds, geo_pos.latlon()))
    {
        return ele_diff;
    }
    else if (geo_pos.lat >= wgs84_bounds.south && geo_pos.lat <= wgs84_bounds.north)
    {
        double west_lon_diff = lon_sub(geo_pos.lon, wgs84_bounds.west);
        double east_lon_diff = lon_sub(geo_pos.lon, wgs84_bounds.east);
        double lon_diff =
            std::abs(west_lon_diff) < std::abs(east_lon_diff) ? west_lon_diff : east_lon_diff;
        double ground_dist =
            hrz::geodesic_distance(geo_pos.latlon(), {geo_pos.lat, geo_pos.lon - lon_diff});
        return std::sqrt(ground_dist * ground_dist + ele_diff * ele_diff);
    }
    else if (geo_pos.lon >= wgs84_bounds.west && geo_pos.lon <= wgs84_bounds.east)
    {
        double lat_diff = geo_pos.lat < wgs84_bounds.south ? geo_pos.lat - wgs84_bounds.south
                                                           : geo_pos.lat - wgs84_bounds.north;
        double ground_dist =
            hrz::geodesic_distance(geo_pos.latlon(), {geo_pos.lat - lat_diff, geo_pos.lon});
        return std::sqrt(ground_dist * ground_dist + ele_diff * ele_diff);
    }
    else
    {
        double west_lon_diff = lon_sub(geo_pos.lon, wgs84_bounds.west);
        double east_lon_diff = lon_sub(geo_pos.lon, wgs84_bounds.east);

        double nearest_lon = std::abs(west_lon_diff) < std::abs(east_lon_diff) ? wgs84_bounds.west
                                                                               : wgs84_bounds.east;
        double nearest_lat =
            geo_pos.lat < wgs84_bounds.south ? wgs84_bounds.south : wgs84_bounds.north;

        double ground_dist = hrz::geodesic_distance(geo_pos.latlon(), {nearest_lat, nearest_lon});
        return std::sqrt(ground_dist * ground_dist + ele_diff * ele_diff);
    }
}

hrz::ScreenToEllipsoidTransform::ScreenToEllipsoidTransform(
    const lm::dvec3& position,
    const lm::dmat4& inv_pv_cc,
    const lm::vec2& subview_size)
{
    lm::dmat4 screen_to_ndc{
        {2.0 / subview_size.x, 0, 0, 0},
        {0, -2.0 / subview_size.y, 0, 0},
        {0, 0, 1, 0},
        {-1, 1, 0, 1}};

    _screen_to_ecef_direction = inv_pv_cc * screen_to_ndc;
    _origin = position;
}

std::optional<lm::dvec3> hrz::ScreenToEllipsoidTransform::planet_intersection(
    lm::vec2 view_pos,
    double altitude) const
{
    lm::dvec3 direction =
        lm::normalize((_screen_to_ecef_direction * lm::dvec4(view_pos, 0, 1)).xyz);

    hrz::Ray ray{_origin, direction};

    lm::dvec3 hit;
    if (hrz::planet_intersection(ray, &hit, altitude))
    {
        return hit;
    }
    else
    {
        return std::nullopt;
    }
}
