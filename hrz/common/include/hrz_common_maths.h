#pragma once

#include <hrz_fnd_maths.h>

#include <gsl/gsl-lite.hpp>
#include <lin_maths.h>

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
    float fovy;
    float aspect_ratio;
    double near;
    double far;
    lm::bbox2 subfrustum;

    bool operator==(const PerspectiveFrustum& f) const
    {
        return fovy == f.fovy && aspect_ratio == f.aspect_ratio && near == f.near && far == f.far
            && subfrustum == f.subfrustum;
    }

    bool operator!=(const PerspectiveFrustum& f) const
    {
        return fovy != f.fovy || aspect_ratio != f.aspect_ratio || near != f.near || far != f.far
            || subfrustum != f.subfrustum;
    }
};

template<typename T>
struct OrientedBBox2
{
    lm::Vector<T, 2> center;
    T half_width;
    T half_height;
    T angle;
};

template<typename T>
struct OrientedBBox3
{
    lm::Vector<T, 3> center;
    lm::Vector<T, 3> u_axis;
    T u_half_length;
    lm::Vector<T, 3> v_axis;
    T v_half_length;
    lm::Vector<T, 3> w_axis;
    T w_half_length;
};

inline lm::dmat4 make_frame_transform(
    const lm::vec3& front,
    const lm::vec3& up,
    double right_handed = true)
{
    lm::vec3 x = lm::cross(front, up);

    if (!right_handed)
    {
        x = -x;
    }

    if (lm::length2(x) == 0)
    {
        return lm::dmat4::identity();
    }
    else
    {
        return lm::dmat4(
            lm::dvec4(x.x, front.x, up.x, 0.0), lm::dvec4(x.y, front.y, up.y, 0.0),
            lm::dvec4(x.z, front.z, up.z, 0.0), lm::dvec4(0.0, 0.0, 0.0, 1.0));
    }
}

static void split_vec2d(const lm::dvec2& in, lm::vec2& base, lm::vec2& partial)
{
    lm::dvec2 base_d = lm::floor(in / SPLIT_F) * SPLIT_F;
    base = lm::vec2(base_d);
    partial = lm::vec2(in - base_d);
}

static void split_vec3d(const lm::dvec3& in, lm::vec3& base, lm::vec3& partial)
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
BSphere<T> compute_bounding_sphere(gsl::span<const lm::Vector<T, 3>> pts);

template<typename T>
BSphere<T> merge_bounding_spheres(const BSphere<T>& a, const BSphere<T>& b);

template<typename T>
BSphere<T> merge_bounding_spheres(gsl::span<const BSphere<T>> bspheres);

template<typename T>
void compute_convex_hull(
    gsl::span<const lm::Vector<T, 2>> pts,
    std::vector<lm::Vector<T, 2>>& hull);

template<typename T>
OrientedBBox2<T> compute_minimum_bbox(gsl::span<const lm::Vector<T, 2>> pts);

template<typename T>
static lm::Matrix<T, 3> compute_normal_transform_matrix(const lm::Matrix<T, 4>& m)
{
    // https://github.com/graphitemaster/normals_revisited
    lm::Matrix<T, 3> n;
    n.col[0] = lm::cross(m.col[1].xyz, m.col[2].xyz);
    n.col[1] = lm::cross(m.col[2].xyz, m.col[0].xyz);
    n.col[2] = lm::cross(m.col[0].xyz, m.col[1].xyz);
    return n;
}

static lm::dvec3 extract_translation(const lm::dmat4& transform)
{
    return {transform.col[3].x, transform.col[3].y, transform.col[3].z};
}

static lm::dmat4 remove_translation(const lm::dmat4& transform)
{
    lm::dmat4 res = transform;
    res.col[3] = lm::dvec4(0, 0, 0, transform.col[3].w);
    return res;
}

static lm::dmat3 to_mat3(const lm::dmat4& matrix)
{
    return lm::dmat3(matrix.col[0].xyz, matrix.col[1].xyz, matrix.col[2].xyz);
}

// From glm
static lm::vec3 extract_euler_angles_xyz(const lm::mat4& m)
{
    float t1 = std::atan2(m.m[2][1], m.m[2][2]);
    float c2 = std::sqrt(m.m[0][0] * m.m[0][0] + m.m[1][0] * m.m[1][0]);
    float t2 = std::atan2(-m.m[2][0], c2);
    float s1 = std::sin(t1);
    float c1 = std::cos(t1);
    float t3 = std::atan2(s1 * m.m[0][2] - c1 * m.m[0][1], c1 * m.m[1][1] - s1 * m.m[1][2]);
    return {-t1, -t2, -t3};
}

static lm::mat4 euler_angles_xyz_to_mat4(lm::vec3 angles)
{
    float c1 = cos(-angles.x);
    float c2 = cos(-angles.y);
    float c3 = cos(-angles.z);
    float s1 = sin(-angles.x);
    float s2 = sin(-angles.y);
    float s3 = sin(-angles.z);

    lm::mat4 m;
    m.m[0][0] = c2 * c3;
    m.m[0][1] = -c1 * s3 + s1 * s2 * c3;
    m.m[0][2] = s1 * s3 + c1 * s2 * c3;
    m.m[0][3] = 0.0;
    m.m[1][0] = c2 * s3;
    m.m[1][1] = c1 * c3 + s1 * s2 * s3;
    m.m[1][2] = -s1 * c3 + c1 * s2 * s3;
    m.m[1][3] = 0.0;
    m.m[2][0] = -s2;
    m.m[2][1] = s1 * c2;
    m.m[2][2] = c1 * c2;
    m.m[2][3] = 0.0;
    m.m[3][0] = 0.0;
    m.m[3][1] = 0.0;
    m.m[3][2] = 0.0;
    m.m[3][3] = 1.0;
    return m;
}

template<typename T>
bool is_clockwise(gsl::span<const T> pts);

template<typename T>
inline lm::Vector<T, 3> srgb_to_linear(lm::Vector<T, 3> color)
{
    color = lm::pow(color, lm::Vector<T, 3>((T)2.2));
    return color;
}

template<typename T>
inline lm::Vector<T, 3> linear_to_srgb(lm::Vector<T, 3> color)
{
    color = lm::pow(color, lm::Vector<T, 3>((T)(1.0 / 2.2)));
    return color;
}

template<typename T>
inline lm::Vector<T, 4> srgb_to_linear(lm::Vector<T, 4> color)
{
    color.rgb = srgb_to_linear(color.rgb);
    return color;
}

template<typename T>
inline lm::Vector<T, 4> linear_to_srgb(lm::Vector<T, 4> color)
{
    color.rgb = linear_to_srgb(color.rgb);
    return color;
}

struct alignas(16) GlslStd140Mat3
{
    lm::vec4 cols[3];

    GlslStd140Mat3() = default;
    GlslStd140Mat3(const GlslStd140Mat3&) = default;
    GlslStd140Mat3(GlslStd140Mat3&&) = default;
    GlslStd140Mat3& operator=(const GlslStd140Mat3&) = default;
    GlslStd140Mat3& operator=(GlslStd140Mat3&&) = default;

    constexpr GlslStd140Mat3(const lm::mat3& mat)
    {
        cols[0] = lm::vec4(mat.col[0], 0);
        cols[1] = lm::vec4(mat.col[1], 0);
        cols[2] = lm::vec4(mat.col[2], 0);
    }

    constexpr GlslStd140Mat3& operator=(const lm::mat3& mat)
    {
        cols[0] = lm::vec4(mat.col[0], 0);
        cols[1] = lm::vec4(mat.col[1], 0);
        cols[2] = lm::vec4(mat.col[2], 0);
        return *this;
    }

    bool operator!=(const GlslStd140Mat3& other) const
    {
        return cols[0] != other.cols[0] || cols[1] != other.cols[1] || cols[2] != other.cols[2];
    }
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
        if (std::abs(value) < threshold) return 0.0f;
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
        _energy_quantity_this_frame((1.0f - _energy_reduction_this_frame) / _lambda)
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
