#pragma once

#include <assert.h>
#include <lin_maths.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace hrz
{

inline int clamp(int x, int min, int max)
{
    return (x < min ? min : (x > max ? max : x));
}

inline unsigned int clamp(unsigned int x, unsigned int min, unsigned int max)
{
    return (x < min ? min : (x > max ? max : x));
}

inline float clamp(float x, float min, float max)
{
    return (x < min ? min : (x > max ? max : x));
}

inline double clamp(double x, double min, double max)
{
    return (x < min ? min : (x > max ? max : x));
}

template<typename TFrom, typename TTo>
inline TTo clamp_cast(TFrom x)
{
    TFrom min = (TFrom)std::numeric_limits<TTo>::lowest();
    TFrom max = (TFrom)std::numeric_limits<TTo>::max();
    return (TTo)(x < min ? min : (x > max ? max : x));
}

// This must be a power of 2 as to not completely lose precision.
static constexpr double SPLIT_F = 65536.0;

// From https://help.agi.com/AGIComponents/html/BlogPrecisionsPrecisions.htm
inline void split_double(double double_value, float& float_low, float& float_high)
{
    if (double_value >= 0.0)
    {
        double double_high = std::floor(double_value / SPLIT_F) * SPLIT_F;
        float_high = (float)double_high;
        float_low = (float)(double_value - double_high);
    }
    else
    {
        double double_high = std::floor(-double_value / SPLIT_F) * SPLIT_F;
        float_high = (float)-double_high;
        float_low = (float)(double_value + double_high);
    }
}

template<typename T>
constexpr T lerp(T start, T end, T t)
{
    return (end - start) * t + start;
}

/**
 * Lerps two values at once with different interpolation factors.
 * This is useful to avoid creating temporary variables to store
 * interpolated values without overriding the source values.
 * Example: `std::tie(a, b) = hrz::lerp_two(a, b, t0, t1);`
 */
template<typename T>
constexpr std::pair<T, T> lerp_two(T start, T end, T t0, T t1)
{
    return std::make_pair(lerp(start, end, t0), lerp(start, end, t1));
}

template<typename T>
inline T clamped_lerp(T start, T end, T t)
{
    return clamp(lerp(start, end, t), start, end);
}

template<typename T>
constexpr bool is_power_of_two(T x)
{
    static_assert(std::is_unsigned_v<T>, "T must be an unsigned integral type");
    return (x != 0) && (x & (x - 1)) == 0;
}

inline uint32_t next_power_of_two(uint32_t x)
{
    x -= 1;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

inline uint64_t next_power_of_two(uint64_t x)
{
    x -= 1;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    return x + 1;
}

inline uint32_t previous_power_of_two(uint32_t x)
{
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x - (x >> 1);
}

inline uint64_t previous_power_of_two(uint64_t x)
{
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    return x - (x >> 1);
}

inline double round_to_power_of_two(double x)
{
    return std::pow(2.0, std::round(std::log2(x)));
}

template<typename T>
inline T align_up_po2(T x, T align)
{
    static_assert(std::is_unsigned_v<T>, "T must be an unsigned integral type");
    assert(is_power_of_two(align));
    return (x + align - 1) & ~(align - 1);
}

template<typename T>
inline T align_up_any(T x, T align)
{
    static_assert(std::is_unsigned_v<T>, "T must be an unsigned integral type");
    if (T mod = x % align; mod != 0)
    {
        return x + align - mod;
    }
    else
    {
        return x;
    }
}

// https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
constexpr uint32_t count_set_bits(uint32_t v)
{
    v = v - ((v >> 1) & 0x5555'5555);
    v = (v & 0x3333'3333) + ((v >> 2) & 0x3333'3333);
    return (((v + (v >> 4)) & 0x0F0F'0F0F) * 0x0101'0101) >> 24;
}

constexpr uint64_t count_set_bits(uint64_t v)
{
    v = v - ((v >> 1) & 0x5555'5555'5555'5555);
    v = (v & 0x3333'3333'3333'3333) + ((v >> 2) & 0x3333'3333'3333'3333);
    return (((v + (v >> 4)) & 0x0F0F'0F0F'0F0F'0F0F) * 0x0101'0101'0101'0101) >> 56;
}

// Rounded down
// AKA position of the highest set bit
inline uint32_t log2(uint32_t x)
{
    if (x == 0) return UINT32_MAX;
    return count_set_bits(previous_power_of_two(x) - 1);
}

// Rounded down
// AKA position of the highest set bit
inline uint32_t log2(uint64_t x)
{
    if (x == 0) return UINT32_MAX;
    return count_set_bits(previous_power_of_two(x) - 1);
}

#ifdef __EMSCRIPTEN__
// For some reason emcc can't infer that it should use the u32 implementation here.
inline size_t next_power_of_two(size_t x)
{
    static_assert(sizeof(size_t) == 4);
    return (size_t)next_power_of_two((uint32_t)x);
}
#endif

template<typename T>
inline bool flt_eq(T a, T b)
{
    return std::abs(a - b)
        <= std::numeric_limits<T>::epsilon() * std::fmax(std::abs(a), std::abs(b));
}

template<typename T>
inline bool flt_near(T a, T b, T eps)
{
    return std::abs(a - b) <= eps;
}

inline double horizontal_to_vertical_fov(double hfov_rad, double aspect_ratio)
{
    return 2.0 * std::atan(std::tan(hfov_rad / 2.0) / aspect_ratio);
}

inline double vertical_to_horizontal_fov(double vfov_rad, double aspect_ratio)
{
    return 2.0 * std::atan(std::tan(vfov_rad / 2.0) * aspect_ratio);
}

// Normalize an angle in [0; 2*pi[
inline double normalize_angle_positive(double angle)
{
    return angle - std::floor(angle / (2 * lm::PI)) * 2 * lm::PI;
}

// Normalize an angle in [-pi;pi[
inline double normalize_angle_around_zero(double angle)
{
    return angle - std::floor((angle + lm::PI) / (2 * lm::PI)) * 2 * lm::PI;
}

// Angles must be in [-pi;pi[
inline double distance_between_normalized_angles(double from, double to)
{
    double a = to - from;
    a += (a > lm::PI) ? -lm::PI * 2 : (a < -lm::PI) ? lm::PI * 2 : 0;
    return a;
}

// Used to clamp an angle between two bounds.
// The valid range for angles once clamped is between min and max,
// from min to max in the rotational direction angles are defined.
// This means that the range can exceed 180 degrees. It also means
// that the numerical value for max can be less than that of the
// min.
//
// * min: 0°, max: 90°: covers a 90° range
// * min: 0°, max: 180°: covers a 180° range
// * min: 0°, max: 270°: covers a 270° range
// * min: 90°, max: 270°: covers a 180° range, passing through ±180°
// * min: 270°, max: 90°: covers a 180° range, passing through 0°
// * min: -90°, max: 90°: covers a 180° range, passing through 0°
// * min: -50°, max: -20°: covers a 330° range, passing through 0° and ±180°
// (Degrees are for the example, but all values must be in radians.)
//
// If min and max are equal, the range is 0° wide. Except if min is -180°
// and max is +180°, in which case the range covers the whole circle.
struct PrecomputedClampAngle
{
    double min, max;
    double min_to_max; // going around the circle in the opposite direction
    double diff;
    double center_angle;

    PrecomputedClampAngle(double min, double max)
    {
        double param_min = min;
        double param_max = max;

        min = normalize_angle_positive(min);
        max = normalize_angle_positive(max);

        if (max == min && min == lm::PI && param_max > param_min)
        {
            // Special case for [-pi,+pi]
            // Other cases where the two normalised values are equal
            // are considered empty intervals.
            this->min = -lm::PI;
            this->max = lm::PI;
            this->min_to_max = 0;
            this->diff = lm::PI * 2;
            this->center_angle = 0;
        }
        else
        {
            if (max < min)
            {
                max += lm::PI * 2;
            }

            min_to_max = (max - lm::PI * 2) - min;
            diff = max - min;
            center_angle = normalize_angle_around_zero(min + diff * 0.5);

            this->min = normalize_angle_around_zero(min);
            this->max = normalize_angle_around_zero(max);
        }
    }

    inline double clamp(double angle) const
    {
        try_clamp(&angle);
        return angle;
    }

    // Returns true if the angle has been clamped, false if unmodified.
    bool try_clamp(double* angle) const
    {
        double norm_angle = normalize_angle_around_zero(*angle);

        double min_to_angle = distance_between_normalized_angles(min, norm_angle);
        if (min_to_angle < min_to_max)
        {
            min_to_angle += lm::PI * 2;
        }

        double max_to_angle = distance_between_normalized_angles(max, norm_angle);
        if (max_to_angle > -min_to_max)
        {
            max_to_angle -= lm::PI * 2;
        }

        if (min_to_angle < 0)
        {
            if (max_to_angle > 0 && max_to_angle < -min_to_angle)
            {
                *angle = max;
            }
            else
            {
                *angle = min;
            }
            return true;
        }

        if (max_to_angle > 0)
        {
            *angle = max;
            return true;
        }

        return false;
    }
};

inline double clamp_angle(double angle, double min, double max)
{
    const PrecomputedClampAngle c(min, max);
    return c.clamp(angle);
}

} // namespace hrz
