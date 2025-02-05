#pragma once

#include "defines.glsl"

#include "common/maths.glsl"

const float EARTH_RADIUS = float(HRZ_S_EARTH_RADIUS);
const float STRAT_RADIUS = float(HRZ_S_STRAT_RADIUS);
const float NEAR = float(HRZ_S_NEAR);
const float FAR = float(HRZ_S_FAR);
const float SKY_AERIAL_MAX_DEPTH = float(HRZ_S_SKY_AERIAL_MAX_DEPTH);
const float SKY_AERIAL_DEPTH = float(HRZ_S_SKY_AERIAL_DEPTH);
const float SKY_VIEW_SIZE = float(HRZ_S_SKY_VIEW_SIZE);
const float SKY_TRANSMITTANCE_LUT_SIZE = float(HRZ_S_SKY_TRANSMITTANCE_LUT_SIZE);

// Direction must be normalized
vec2 encode_sky_view_uv(vec3 direction, float horizon_horizon_angle)
{
    vec2 polar;
    polar.x = atan(direction.y, direction.x) / PI;

    // See decode_sky_view_uv.
    // This is the reverse
    float horizon_angle = asin(direction.z) - horizon_horizon_angle;
    float horizon_angle_coeff;

    // Since the LUT is continuous at the horizon and we don't want to
    // interpolate across the horizon, we clamp half a pixel above or
    // below the horizon depending on what part we are sampling.
    float clamp_min;
    float clamp_max;

    if (horizon_angle >= 0.0)
    {
        horizon_angle_coeff = 1.0 / (PI * 0.5 - horizon_horizon_angle);
        clamp_min = 0.5 + 0.5 / float(SKY_VIEW_SIZE);
        clamp_max = 1.0;
    }
    else
    {
        horizon_angle_coeff = 1.0 / (horizon_horizon_angle + PI * 0.5);
        clamp_min = 0.0;
        clamp_max = 0.5 - 0.5 / float(SKY_VIEW_SIZE);
    }

    polar.y = sign(horizon_angle) * sqrt(abs(horizon_angle * horizon_angle_coeff)) * 0.5 + 0.5;
    polar.y = clamp(polar.y, clamp_min, clamp_max);

    return polar;
}

// We will always work in a tangential frame around the Earth. Every position
// and direction around the Earth is defined as an altitude and an angle from the
// horizon plane (the plane tangent to the Earth surface).
//
// By rotational symmetry, we don't have to care about latitude, longitude, or
// the heading of the direction. We treat the Earth as spherical.

// This is a simple sphere-ray intersection, but the parameters are simplified
// by the properties explained above.
//
// d0 and d1 are the first and potentially second intersection of the ray with
// the sphere. The returned value is the number of intersection.
// Note that we are only interested in the intersection "forward", so d0 and
// d1 are always positive.
int intersect_sphere(
    float radius,
    float altitude,
    float horizon_angle,
    out float d0,
    out float d1)
{
    float b = 2.0 * altitude * sin(horizon_angle);
    float c = altitude * altitude - radius * radius;
    float delta = b * b - 4.0 * c;

    if (delta < 0.0)
    {
        return 0;
    }
    else if (delta > 0.0)
    {
        float sr_delta = sqrt(delta);
        d0 = (-b - sr_delta) / 2.0;
        d1 = (-b + sr_delta) / 2.0;

        if (d0 < 0.0)
        {
            if (d1 < 0.0)
            {
                return 0;
            }
            else
            {
                d0 = d1;
                return 1;
            }
        }
        else
        {
            return 2;
        }
    }
    else // delta == 0
    {
        float d = -b / 2.0;

        if (d < 0.0)
        {
            return 0;
        }
        else
        {
            d0 = d;
            return 1;
        }
    }
}

#if !defined(ATMOSPHERE_DISABLED)

vec2 encode_sun_color_uv(in float altitude, in float horizon_angle)
{
    float y = sqrt((altitude - EARTH_RADIUS) / (STRAT_RADIUS - EARTH_RADIUS));
    float x = sin(horizon_angle) * 0.5 + 0.5;

    return vec2(x, y);
}

const float AERIAL_LOG_DISTRIB = 3.0;

// Returns the depth of an aerial perspective depth slice, with z in [0; 1]
float aerial_depth_slice(float z)
{
    z = (exp(AERIAL_LOG_DISTRIB * z) - 1.0) / (exp(AERIAL_LOG_DISTRIB) - 1.0);
    return z * (SKY_AERIAL_MAX_DEPTH - NEAR) + NEAR;
}

float aerial_depth_to_slice(float depth)
{
    depth = (depth - NEAR) / (SKY_AERIAL_MAX_DEPTH - NEAR);
    return log(depth * (exp(AERIAL_LOG_DISTRIB) - 1.0) + 1.0) / AERIAL_LOG_DISTRIB * (SKY_AERIAL_DEPTH - 1.0);
}

// Rays used to be represented as the struct below, however, because of
// @Workaround(006-Android-LowFloatPrecisionInStructs), we can't do that
// anymore and need to store them as a vec4 avec use macros to at least make it
// usable.
// struct Ray
// {
//     float alt_b;
//     float alt_c;
//     float r0;
//     float d0;
// };

#define Ray vec4
#define ray_alt_b(R) R.x
#define ray_alt_c(R) R.y
#define ray_r0(R) R.z
#define ray_d0(R) R.w

Ray make_ray(float altitude, float horizon_angle)
{
    Ray ray;
    ray_r0(ray) = altitude * cos(horizon_angle);
    ray_d0(ray) = altitude * sin(horizon_angle);
    ray_alt_b(ray) = 2.0 * ray_d0(ray);
    ray_alt_c(ray) = altitude * altitude;
    return ray;
}

// When raymarching, we want to always stay in a tangential frame.
// To do so, the following functions reparameterize the ray along its direction.
float ray_altitude_at(Ray ray, float d)
{
    return sqrt(d * d + d * ray_alt_b(ray) + ray_alt_c(ray));
}

float ray_horizon_angle_at(Ray ray, float d)
{
    return atan(d + ray_d0(ray), ray_r0(ray));
}

bool atmosphere_intersection(float altitude, float horizon_angle, out float from, out float to)
{
    float dist_strat_0 = 0.0;
    float dist_strat_1 = 0.0;
    float dist_earth_0 = 0.0;
    float dist_earth_1 = 0.0;

    int intersect_strat = intersect_sphere(STRAT_RADIUS, altitude, horizon_angle,
        dist_strat_0, dist_strat_1);

    int intersect_earth = intersect_sphere(EARTH_RADIUS, altitude, horizon_angle,
        dist_earth_0, dist_earth_1);

    // Shoot ray straight from space into space.
    if (intersect_strat == 0 && intersect_earth == 0)
    {
        return false;
    }

    // Outside the atmosphere
    if (altitude >= STRAT_RADIUS)
    {
        // Just touches the stratosphere, without entering it
        if (intersect_strat < 2)
        {
            return false;
        }
        else // Goes through the atmosphere
        {
            // Intersects the ground inside the atmosphere
            if (intersect_earth > 0)
            {
                from = dist_strat_0;
                to = dist_earth_0;
                return true;
            }
            else // Goes through the atmosphere without ever touching the ground.
            {
                from = dist_strat_0;
                to = dist_strat_1;
                return true;
            }
        }
    }
    else if (altitude >= EARTH_RADIUS) // Inside the atmosphere
    {
        // The ray touches the earth
        if (intersect_earth > 0)
        {
            from = 0.0;
            to = dist_earth_0;
            return true;
        }
        else // The ray exits the atmosphere
        {
            from = 0.0;
            to = dist_strat_0;
            return true;
        }
    }
    else // Inside the earth
    {
        // Here all rays exit the earth first, then the atmosphere.
        from = dist_earth_0;
        to = dist_strat_0;
        return true;
    }
}

// This always has to be executed inside the atmosphere.
// We can simplify the normal sphere intersection a lot because we know the ray
// always starts inside the sphere, there is spherical symmetry, and we only
// want the positive distance (towards the view direction).
float distance_to_top_atmosphere_inside(float altitude, float horizon_angle)
{
    float mu = sin(horizon_angle);
    float delta = altitude * altitude * (mu * mu - 1.0) + STRAT_RADIUS * STRAT_RADIUS;
    float sr_delta = sqrt(delta);
    return -altitude * mu + sr_delta;
}

// The transmittance LUT parameterization is the one from
// https://github.com/sebh/UnrealEngineSkyAtmosphere/blob/master/Resources/Bruneton17/functions.glsl#L402
// It gives no discontinuities, and thus no artifacts, especially at the horizon.
vec2 encode_transmittance_uv(float altitude, float horizon_angle)
{
    float mu = sin(horizon_angle);
    const float H = sqrt(STRAT_RADIUS * STRAT_RADIUS - EARTH_RADIUS * EARTH_RADIUS);
    float rho = sqrt(altitude * altitude - EARTH_RADIUS * EARTH_RADIUS);
    float d = distance_to_top_atmosphere_inside(altitude, horizon_angle);
    float d_min = STRAT_RADIUS - altitude;
    float d_max = rho + H;
    float x = (d - d_min) / (d_max - d_min);
    float y = rho / H;

    return vec2(x, 1.0 - y);
}

vec3 sample_transmittance(float altitude, float horizon_angle, sampler2D transmittance_lut)
{
    if (altitude < EARTH_RADIUS)
    {
        altitude = EARTH_RADIUS;
    }

    if (altitude >= STRAT_RADIUS)
    {
        float from, to;
        if (atmosphere_intersection(altitude, horizon_angle, from, to))
        {
            Ray ray = make_ray(altitude, horizon_angle);
            altitude = ray_altitude_at(ray, from);
            horizon_angle = ray_horizon_angle_at(ray, from);
        }
    }

    const float transmittance_lut_bias = 0.5 / SKY_TRANSMITTANCE_LUT_SIZE;
    const float transmittance_lut_rescale = (SKY_TRANSMITTANCE_LUT_SIZE - 1.0) / SKY_TRANSMITTANCE_LUT_SIZE;

    vec2 uv = encode_transmittance_uv(altitude, horizon_angle);
    uv = uv * transmittance_lut_rescale + transmittance_lut_bias;
    return texture(transmittance_lut, uv).rgb;
}

#endif // !defined(ATMOSPHERE_DISABLED)
