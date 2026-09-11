// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "sky/sky.glsl"

#if !defined(ATMOSPHERE_DISABLED)

// The appearance of the sky is the result of light being absorbed and redirected
// (scattered) by particles in the atmosphere.
//
// Whenever some light hits a particle, some of it is absorbed, and some of it
// is redirect is a random direction, following the distribution of the phase
// function. When some light in redirected towards the view direction, it's called
// in-scattered. When some light is redirected away from the view direction,
// it's called out-scattered.
//
//      + = In-scattering event
//     <====<==========+-----<--  Light ray
// Observer             \.
//
//          x = Out-scattering
//     <----<------x=========<==  Light ray
// Observer       /
//
//          o = Absorption event
//     <----<-------o========<==  Light ray
// Observer

// Scattering S is a function of 3 parameters: the wavelength of the light (λ),
// the density of particles, and the scattering angle (θ). In the atmosphere,
// the density of particles is a function of altitude (h).
//
// A simple form for S is S(λ, h, θ) = βs(λ) ρ(h) γ(θ).
//
// βs(λ) is the scattering coefficient for the given wavelength at sea level.
// This is the total amount of light scattered to every direction. Essensially
// it's the integral over θ of S(λ, 0, θ).
//
// ρ(h) is the atmosphere density ratio. It's the atmosphere density relative
// to sea-level. Hence it's 1 at sea level, and tends to 0 towards infinity.
// It's defined as a negative exponent with a reference altitude.
//
// γ(θ) is the phase function for the scattering. It gives the amount of light
// scattered to every direction.
//
// We also introduce βa(λ), the absorption coefficient, which is the amount of
// light absorbed at the scattering site.
//
// Finally we introduce the extinction coefficient, βe(λ) = βs(λ) + βa(λ) which
// is the total amount of light that does not continue along it's way after a
// scattering event. It's the sum of out-scattered and absorbed light.
//
// We introduce 3 types of events: Rayleigh, Mie, and ozone. The effects of
// in-scattering is additive. The effects of extinction is multiplicative.

float density_ratio(float altitude, float density_reference_altitude_inv)
{
    return exp(-altitude * density_reference_altitude_inv);
}

vec2 density_ratio(float altitude, vec2 density_reference_altitude_inv)
{
    return exp(-altitude * density_reference_altitude_inv);
}

float density_ratio_ozone(float altitude)
{
    // It looks pretty bad so divide by 2...
    return max(0.0, 1.0 - abs(altitude - 25000.0) / 15000.0) * 0.5;
}

// This is the scattering events caused by molecules. It's wavelength-dependent
// and contributes to the blue color of the sky, and the red tint at grazing angles.
// The following values are given by [Hillaire20](https://sebh.github.io/publications/egsr2020.pdf).

const vec3 rayleigh_scattering_coeff = vec3(5.8e-6, 1.35e-5, 3.31e-5);
const vec3 rayleigh_extinction_coeff = rayleigh_scattering_coeff;
const float rayleigh_reference_altitude_inv = 1.25e-4;

vec3 rayleigh_scattering(float altitude, float cos_angle)
{
    float phase = 0.05968310365946075091 * (1.0 + cos_angle * cos_angle);

    float density = density_ratio(
        altitude - EARTH_RADIUS,
        rayleigh_reference_altitude_inv);

    return (density * phase) * rayleigh_scattering_coeff;
}

// Mie scattering events are caused by dust, pollution, etc. It's not
// wavelength-dependent, and is responsible for the white halo around the sun,
// and the milky appearance of the sky. It's configurable to allow for a more
// "milky" look, but the higher is it set, the less accurate it becomes when
// evaluating it with single scattering (which is what we do). Hence we keep
// its value low. Same as above, the following values are from
// [Hillaire20](https://sebh.github.io/publications/egsr2020.pdf) and it has
// been pre-computed with g=0.8.

const float mie_scattering_coeff = 3.996e-6;
const float mie_extinction_coeff = 4.40e-6;
const float mie_reference_altitude_inv = 8.33e-4;

vec3 mie_scattering(float altitude, float cos_angle, float cloudiness)
{
    float density = density_ratio(
        altitude - EARTH_RADIUS,
        mie_reference_altitude_inv);

    float phase = 0.01627721008894384116
        * (1.0 + cos_angle * cos_angle)
        / pow(1.64 - 1.6 * cos_angle, 1.5);

    return vec3(density * phase * mie_scattering_coeff * cloudiness);
}

// Ozone doesn't scatter light, it only absorbs it. It's responsible
// for given even more of a blue tint to the sky.

const vec3 ozone_extinction_coeff = vec3(0.650e-6, 1.881e-6, 0.085e-6);

// Transmittance is the amount of light that is absorbed or out-scattered
// along a ray. It is defines as transmittance = exp(-βe . optical_depth).
// It is multiplicative so we can combine all three phenomenon.

// Optical depth is a vector of the optical depth for rayleigh, mie, and ozone.
vec3 compute_transmittance(float cloudiness, vec3 optical_depth)
{
    // Ozone and rayleigh have the same reference altitude, so the same optical depth
    return exp(
        -rayleigh_extinction_coeff * optical_depth.x
        -mie_extinction_coeff * cloudiness * optical_depth.y
        -ozone_extinction_coeff * optical_depth.z);
}

vec3 scattering(float altitude, float cos_angle, float cloudiness)
{
    return mie_scattering(altitude, cos_angle, cloudiness) + rayleigh_scattering(altitude, cos_angle);
}

bool test_intersect_sphere(
    float radius,
    float altitude,
    float horizon_angle)
{
    float d0, d1;
    return intersect_sphere(radius, altitude, horizon_angle, d0, d1) > 0;
}

// Optical depth is the amount of "stuff" that a ray encounters as it traverses
// the atmosphere. It is the integral of the density of the atmosphere.
//
// The analytical form of this integral exists but is a bit complex, so instead
// we integral it numerically. We divide the segment from the observation point
// to the end of the atmosphere (or the ground) into equal length segments,
// and integrate by sampling in the middle of each segment.

vec3 compute_optical_depth(
    float altitude,
    float horizon_angle,
    vec2 density_reference_altitudes_inv)
{
    float from, to;
    if (!atmosphere_intersection(altitude, horizon_angle, from, to))
    {
        return vec3(0.0);
    }

    float ray_length = to;

    Ray ray = make_ray(altitude, horizon_angle);

    const int subdivisions = 50;
    float ds = ray_length / float(subdivisions);
    vec3 result = vec3(0.0);

    for (int i = 0; i <= subdivisions; ++i)
    {
        float dist_so_far = ds * float(i);
        float current_altitude = ray_altitude_at(ray, dist_so_far);

        vec2 densities_r_m = density_ratio(current_altitude - EARTH_RADIUS, density_reference_altitudes_inv);
        float density_o = density_ratio_ozone(current_altitude - EARTH_RADIUS);

        float w = (i == 0 || i == subdivisions) ? 0.5 : 1.0;
        result += vec3(densities_r_m, density_o) * ds * w;
    }

    return result;
}


void decode_transmittance_uv(vec2 uv, out float altitude, out float horizon_angle)
{
    const float H = sqrt(STRAT_RADIUS * STRAT_RADIUS - EARTH_RADIUS * EARTH_RADIUS);
    float rho = H * uv.y;

    float r = sqrt(rho * rho + EARTH_RADIUS * EARTH_RADIUS);

    float d_min = STRAT_RADIUS - r;
    float d_max = rho + H;
    float d = d_min + uv.x * (d_max - d_min);
    float mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);

    altitude = r;
    horizon_angle = asin(mu);
}

void decode_sky_view_uv(vec2 uv, float horizon_horizon_angle, out float azimuth, out float horizon_angle)
{
    // We compress more data towards the horizon, and put the horizon of
    // the planet at y=0.5, so that this has the maximum precision.
    // 0 -> -Pi/2, 0.5 -> horizon_horizon_angle, 1 -> Pi/2
    // So this makes 2 affine sections. But we distribute them quadratically to
    // pack more precision near the horizon.
    float horizon_angle_coeff;

    float y = uv.y * 2.0 - 1.0;
    y = sign(y) * y * y;
    if (y >= 0.0)
    {
        horizon_angle_coeff = PI * 0.5 - horizon_horizon_angle;
    }
    else
    {
        horizon_angle_coeff = horizon_horizon_angle + PI * 0.5;
    }

    horizon_angle = y * horizon_angle_coeff + horizon_horizon_angle;
    azimuth = uv.x * PI;
}

// In order to compute the final appearance of the sky, we must integrate
// in-scattering and out-scattering along the view direction. There are two main
// components:
//     - The light that is in-scattered from the sun direction.
//     - The light from whatever is in the view direction (space or ground) and
//       that is partially out-scattered and absorbed.
//
// In theory, we should in-scatter light from all directions but this requires
// recursively evaluating scattering, which is difficult (this is called
// multi-scattering). In most cases during daytime and when the sky is not too
// cloudy (low Mie coefficient), multi-scattering has little to no effect. Since
// those are the cases that we want to simulate, we don't mind using simply
// single-scattering.
//
// The light that comes from the sun and is in-scattered towards the observer
// follows the following steps:
//     - It is partially out-scattered and absorbed until it reaches the scattering
//       location.
//     - It is in-scattered towards the observer.
//     - It is again partially out-scattered and absorbed on its way from the
//       scattering location to the observer.
//
// This means that by write P as the observer, S as the scattering location and θ
// the angle between the view direction and the sun direction, we can compute the
// amount of light that reaches the observer as (sun denotes infinity in the
// direction of the sun)
//
// T(P->S) * S(λ, h(S), θ) * T(S->sun)
//
// By integrating this along the view direction, we find the total amount of
// in-scattered light. Again we do this numerically.
//
// Some light comes from the background object. It is simply out-scattered and
// absorbed.

// The returned vector is:
// - .rgb: in-scattering.
// - .a: monochromatic extinction (absorption and out-scattering).
vec4 in_scattering(
    float altitude,
    float horizon_angle,
    float azimuth,
    float sun_horizon_angle,
    float from,
    float to,
    float cloudiness,
    sampler2D transmittance_lut)
{
    vec3 sun_dir = vec3(
        -sin(azimuth) * cos(sun_horizon_angle),
        cos(azimuth) * cos(sun_horizon_angle),
        sin(sun_horizon_angle)
    );

    vec3 view_dir = vec3(0, cos(horizon_angle), sin(horizon_angle));
    float cos_scattering_angle = dot(sun_dir, view_dir);

    Ray ray = make_ray(altitude, horizon_angle);

    const int subdivisions = 32;
    float ds = (to - from) / float(subdivisions);

    vec3 in_scattered = vec3(0.0);
    vec3 optical_depth = vec3(0.0);
    const vec2 ref_altitudes_inv = vec2(rayleigh_reference_altitude_inv, mie_reference_altitude_inv);

    for (int i = 0; i <= subdivisions; ++i)
    {
        float w = (i == 0 || i == subdivisions) ? 0.5 : 1.0;
        float current_dist = from + ds * float(i);
        float current_altitude = ray_altitude_at(ray, current_dist);
        float current_horizon_angle = ray_horizon_angle_at(ray, current_dist);

        // Move the sampled point to above the surface to avoid discontinuities
        // in depth, since we use bilinear sampling of the aerial LUT.
        current_altitude = max(current_altitude, EARTH_RADIUS);

        // We integrate the optical depth as we go. This avoids the headache of
        // using the transmittance LUT while keeping as much precision as possible...
        vec2 densities_r_m = density_ratio(current_altitude - EARTH_RADIUS, ref_altitudes_inv);
        float density_o = density_ratio_ozone(current_altitude - EARTH_RADIUS);
        optical_depth += vec3(densities_r_m, density_o) * ds * w;

        // As we move along the view direction, the direction of the sun in the
        // tangential frame will change. As explain above, it will rotate along
        // the X axis. Since we know the current horizontal angle of the view
        // direction, we can compute how much the tangential frame has rotated
        // relative to the starting horizontal angle. The sun direction will have
        // rotated by the same amount, so we rotate the `sun_direction` vector,
        // and compute its horizontal angle in the current tangential frame with a
        // little bit of trigonometry. Note that for all the following
        // calculations that will use the angle of the sun, we don't care about
        // the azimuth thanks to axis symmetry along the Z axis of the tangential
        // frame.
        float angle_change = current_horizon_angle - horizon_angle;
        float current_sun_horizon_angle =
            asin(sin(angle_change) * sun_dir.y + cos(angle_change) * sun_dir.z);

        // Sometimes, the sun will be behind the Earth so its light will be
        // blocked by the Earth and this won't participate in in-scattering. We
        // test for this case using a sphere intersection. This is especially
        // important for sunsets.
        bool intersect_earth = test_intersect_sphere(EARTH_RADIUS - 1.0, current_altitude, current_sun_horizon_angle);
        if (!intersect_earth)
        {
            vec3 trans_p_s = compute_transmittance(cloudiness, optical_depth);
            vec3 trans_s_sun = sample_transmittance(current_altitude, current_sun_horizon_angle, transmittance_lut);
            vec3 scattered_light = scattering(current_altitude, cos_scattering_angle, cloudiness);

            in_scattered += ds * trans_p_s * scattered_light * trans_s_sun * w;
        }
    }

    vec3 transmittance = compute_transmittance(cloudiness, optical_depth);

    return vec4(in_scattered, dot(transmittance, vec3(1.0 / 3.0)));
}

void decode_sun_color_uv(in vec2 uv, out float altitude, out float horizon_angle)
{
    // We pack more samples near the ground
    altitude = uv.y * uv.y * (STRAT_RADIUS - EARTH_RADIUS) + EARTH_RADIUS;
    horizon_angle = asin(uv.x * 2.0 - 1.0);
}

#endif // !defined(ATMOSPHERE_DISABLED)
