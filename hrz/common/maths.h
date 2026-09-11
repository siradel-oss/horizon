// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/fnd/maths.h"

#include <lin_maths.h>

#include <span>
#include <vector>

namespace hrz
{

struct Ray
{
    lm::dvec3 o;
    lm::dvec3 dir;
};

struct PerspectiveFrustum
{
    float fovy{};
    float aspect_ratio{};
    double near{};
    double far{};
    lm::bbox2 subfrustum;

    constexpr bool operator ==(const PerspectiveFrustum& f) const = default;
};

template<typename T>
struct OrientedBBox2
{
    lm::Vector<T, 2> center;
    T half_width{};
    T half_height{};
    T angle{};
};

template<typename T>
struct OrientedBBox3
{
    lm::Vector<T, 3> center;
    lm::Vector<T, 3> u_axis;
    T u_half_length{};
    lm::Vector<T, 3> v_axis;
    T v_half_length{};
    lm::Vector<T, 3> w_axis;
    T w_half_length{};
};

lm::dmat4 make_frame_transform(const lm::vec3& front, const lm::vec3& up, bool right_handed = true);

constexpr void split_vec2d(const lm::dvec2& in, lm::vec2& base, lm::vec2& partial)
{
    lm::dvec2 base_d = lm::floor(in / SPLIT_F) * SPLIT_F;
    base = lm::vec2(base_d);
    partial = lm::vec2(in - base_d);
}

constexpr void split_vec3d(const lm::dvec3& in, lm::vec3& base, lm::vec3& partial)
{
    lm::dvec3 base_d = lm::floor(in / SPLIT_F) * SPLIT_F;
    base = lm::vec3(base_d);
    partial = lm::vec3(in - base_d);
}

double distance_to_triangle(
    const lm::dvec3& a,
    const lm::dvec3& b,
    const lm::dvec3& c,
    const lm::dvec3& p);

template<typename T>
struct BSphere
{
    lm::Vector<T, 3> center;
    T radius = -1.0;
};

template<typename T>
BSphere<T> compute_bounding_sphere(std::span<const lm::Vector<T, 3>> pts);

extern template BSphere<float> compute_bounding_sphere<float>(std::span<const lm::vec3> pts);
extern template BSphere<double> compute_bounding_sphere<double>(std::span<const lm::dvec3> pts);

template<typename T>
BSphere<T> merge_bounding_spheres(const BSphere<T>& a, const BSphere<T>& b);

extern template BSphere<float> merge_bounding_spheres(
    const BSphere<float>& a,
    const BSphere<float>& b);
extern template BSphere<double> merge_bounding_spheres(
    const BSphere<double>& a,
    const BSphere<double>& b);

template<typename T>
BSphere<T> merge_bounding_spheres(std::span<const BSphere<T>> bspheres);

extern template BSphere<float> merge_bounding_spheres(std::span<const BSphere<float>> bspheres);
extern template BSphere<double> merge_bounding_spheres(std::span<const BSphere<double>> bspheres);

template<typename T>
void compute_convex_hull(
    std::span<const lm::Vector<T, 2>> pts,
    std::vector<lm::Vector<T, 2>>& hull);

extern template void compute_convex_hull(
    std::span<const lm::vec2> pts,
    std::vector<lm::vec2>& hull);
extern template void compute_convex_hull(
    std::span<const lm::dvec2> pts,
    std::vector<lm::dvec2>& hull);

template<typename T>
OrientedBBox2<T> compute_minimum_bbox(std::span<const lm::Vector<T, 2>> pts);

extern template OrientedBBox2<float> compute_minimum_bbox(std::span<const lm::vec2> pts);
extern template OrientedBBox2<double> compute_minimum_bbox(std::span<const lm::dvec2> pts);

template<typename T>
lm::Matrix<T, 3> compute_normal_transform_matrix(const lm::Matrix<T, 4>& m)
{
    // https://github.com/graphitemaster/normals_revisited
    lm::Matrix<T, 3> n;
    n.col[0] = lm::cross(m.col[1].xyz, m.col[2].xyz);
    n.col[1] = lm::cross(m.col[2].xyz, m.col[0].xyz);
    n.col[2] = lm::cross(m.col[0].xyz, m.col[1].xyz);
    return n;
}

constexpr lm::dvec3 extract_translation(const lm::dmat4& transform)
{
    return {transform.col[3].x, transform.col[3].y, transform.col[3].z};
}

constexpr lm::dmat4 remove_translation(const lm::dmat4& transform)
{
    lm::dmat4 res = transform;
    res.col[3] = lm::dvec4(0, 0, 0, transform.col[3].w);
    return res;
}

constexpr lm::dmat3 to_mat3(const lm::dmat4& matrix)
{
    return lm::dmat3(matrix.col[0].xyz, matrix.col[1].xyz, matrix.col[2].xyz);
}

lm::vec3 extract_euler_angles_xyz(const lm::mat4& m);
lm::mat4 euler_angles_xyz_to_mat4(lm::vec3 angles);

template<typename T>
bool is_clockwise(std::span<const T> pts);

struct alignas(16) GlslStd140Mat3
{
    lm::vec4 cols[3]{};

    GlslStd140Mat3() = default;

    // NOLINTNEXTLINE(*explicit*)
    constexpr GlslStd140Mat3(const lm::mat3& mat)
    {
        cols[0] = lm::vec4(mat.col[0], 0);
        cols[1] = lm::vec4(mat.col[1], 0);
        cols[2] = lm::vec4(mat.col[2], 0);
    }

    constexpr bool operator ==(const GlslStd140Mat3& other) const = default;
};

static_assert(sizeof(GlslStd140Mat3) == 12 * sizeof(float), "Size of GlslStd140Mat3");
static_assert(alignof(GlslStd140Mat3) == 16, "Align of GlslStd140Mat3");
static_assert(offsetof(GlslStd140Mat3, cols[0]) == 0, "Offsets in GlslStd140Mat3");
static_assert(offsetof(GlslStd140Mat3, cols[1]) == 4 * sizeof(float), "Offsets in GlslStd140Mat3");
static_assert(offsetof(GlslStd140Mat3, cols[2]) == 8 * sizeof(float), "Offsets in GlslStd140Mat3");

// Used to modify quantities that are affected by inertia.
// It is initialized by the desired energy half time and the dt of the last frame, then some values
// are precomputed, that are then used to apply inertia as needed.
// In order to not create or lose energy, get_quantity_this_frame and apply_energy_loss should both
// be used, or neither of them.
class Inertia
{
    const double _frame_dt{};
    const double _lambda{};
    const double _energy_reduction_this_frame{};
    const double _energy_quantity_this_frame{};

    inline double zero_if_under(double value, double threshold) const
    {
        if (std::abs(value) < threshold) return 0.0;
        return value;
    }

public:
    static inline double lambda(double energy_half_time)
    {
        return std::log(2.0) / energy_half_time;
    }

    Inertia(double energy_half_time, double frame_dt) :
        _frame_dt(frame_dt),
        _lambda(lambda(energy_half_time)),
        _energy_reduction_this_frame(std::exp(-_lambda * _frame_dt)),
        _energy_quantity_this_frame((1.0 - _energy_reduction_this_frame) / _lambda)
    {
    }

    // Computes the energy necessary to apply quantity q during this frame.
    constexpr double apply_quantity_this_frame(double q) const
    {
        return q / _energy_quantity_this_frame;
    }

    // Computes the energy necessary to apply a change of the given velocity during this frame.
    constexpr double apply_velocity_this_frame(double v) const
    {
        return v * _frame_dt / _energy_quantity_this_frame;
    }

    // Computes the energy necessary to apply the given quantity q from now until the energy is
    // fully dissipated.
    constexpr double apply_quantity_total(double q) const { return q * _lambda; }

    // Returns the remaining energy after applying the loss for the current frame to the quantity of
    // energy "e". If this quantity is under the given threshold, it is set to 0. This prevents
    // having infinitesimal quantities of energy for an infinite (or very large) amount of time.
    inline double apply_energy_loss(double e, double threshold) const
    {
        return zero_if_under(e * _energy_reduction_this_frame, threshold);
    }

    // Returns the quantity to apply given the current energy "e" this frame.
    constexpr double get_quantity_this_frame(double e) const
    {
        return e * _energy_quantity_this_frame;
    }
};

} // namespace hrz
