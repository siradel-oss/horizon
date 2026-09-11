// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/core/camera/types.h"

#include <lin_maths.h>

namespace hrz::render
{

// Compute a factor to convert a size in device pixels into a size in meters
// necessary for the object at the given position to be viewed as the wanted size in pixels.
double compute_device_pixels_to_meters(
    const lm::dvec3& ecef_pos,
    const hrz::CameraViewInfo& view_info);

// Compute a factor to convert a size in logical pixels into a size in meters
// necessary for the object at the given position to be viewed as the wanted size in pixels.
double compute_logical_pixels_to_meters(
    const lm::dvec3& ecef_pos,
    const hrz::CameraViewInfo& view_info);

// Compute a factor to convert a size in logical pixels to a size in meters for an object at a
// distance of 1.
double compute_logical_pixel_size_in_meters(const hrz::CameraViewInfo& view_info);

class ScreenSpaceError
{
    double _sse_denominator;
    double _viewport_height;

public:
    ScreenSpaceError() : _sse_denominator(1), _viewport_height(0) {}

    ScreenSpaceError(double fov_y, double viewport_height, float device_pixel_ratio) :
        _sse_denominator(2.0 * std::tan(0.5 * fov_y) * device_pixel_ratio),
        _viewport_height(viewport_height)
    {
    }

    /**
     * Each tile has a "geometric error" property. This value is a length. It gives
     * the size of the smallest detail in the tile. (For example, if the tile consists
     * of just a textured plane, this is the size of a texel in world-space.)
     * Using the distance to the tile from the camera, we compute the size (in metres)
     * of a viewport pixel on a plane parallel to the camera plane, tangent to the
     * tile geometry.
     * The size of a pixel is compared to the geometric error of the tile, giving us
     * an error ratio, the screen-space error.
     * If the value is less than one, it means that one screen pixel covers more the
     * the smallest detail of the tile, or conversely, that the smallest detail spans
     * less than one pixel. The tile has therefore enough details and does not need to
     * be refined. (On the contrary, it may need to be unrefined.)
     * If the value is more than one, a screen pixel covers less than the smallest
     * detail of the tile, so the tile does not have enough details relatively to the
     * distance at which it is viewed and should be refined.
     * Because different tilesets may have opted for different ways of computing what
     * their geometry error value is, a parameter named "max screen-space error" is
     * passed along with the tileset. The screen-space error is divided by this
     * parameter before being used by the rest of the system.
     *
     * The equation for the screen-space error is:
     *              tile.geometric_error * viewport_height
     *     error = ----------------------------------------
     *               cam_tile_distance * 2 * tan(fovy / 2)
     * The value `2 * tan(fovy / 2)` is the same for every tile, and is computed in
     * advance and passed as a parameter to this function.
     */
    inline double compute_screen_space_size(double world_size, double distance) const
    {
        distance = std::max(distance, 0.0000001); // Avoid dividing by 0.
        return (world_size * _viewport_height) / (distance * _sse_denominator);
    }

    inline double compute_screen_space_error(
        double geometric_error,
        double distance,
        double max_screen_space_error) const
    {
        return compute_screen_space_size(geometric_error, distance) / max_screen_space_error;
    }

    constexpr bool operator ==(const ScreenSpaceError& other) const = default;
};

} // namespace hrz::render
