#pragma once

#include <hrz_fnd_array_view.h>
#include <hrz_fnd_function_ref.h>

#include <lin_maths.h>

#include <functional>
#include <span>

namespace hrz
{
void clip_triangle(
    const lm::vec2& p0,
    const lm::vec2& p1,
    const lm::vec2& p2,
    const lm::vec2& uv0,
    const lm::vec2& uv1,
    const lm::vec2& uv2,
    const lm::bbox2& bbox,
    hrz::function_ref<void(
        const lm::vec2& p0,
        const lm::vec2& p1,
        const lm::vec2& p2,
        const lm::vec2& uv0,
        const lm::vec2& uv1,
        const lm::vec2& uv2)> rasterize_fn);

template<typename T>
void clip_triangle(
    const lm::Vector<T, 2>& p0,
    const lm::Vector<T, 2>& p1,
    const lm::Vector<T, 2>& p2,
    const lm::Bbox<T, 2>& bbox,
    hrz::function_ref<
        void(const lm::Vector<T, 2>& p0, const lm::Vector<T, 2>& p1, const lm::Vector<T, 2>& p2)>
        rasterize_fn);

template<typename T>
void clip_triangle(
    const lm::Vector<T, 2>& p0,
    const lm::Vector<T, 2>& p1,
    const lm::Vector<T, 2>& p2,
    T attr0,
    T attr1,
    T attr2,
    const lm::Bbox<T, 2>& bbox,
    hrz::function_ref<void(
        const lm::Vector<T, 2>& p0,
        const lm::Vector<T, 2>& p1,
        const lm::Vector<T, 2>& p2,
        T attr0,
        T attr1,
        T attr2)> rasterize_fn);

/**
 * Clips a triangle defined by the indices i0, i1, i2 by the bbox.
 * The rasterize_fn is called for each resulting triangle with the indices
 * of the triangle vertices.
 * The append_fn is called for each new vertex that is created during clipping.
 * It is given the position of the new vertex, and the blending factors for the
 * three original points, which can be used to interpolate additional attributes.
 * It should return the index of the new vertex.
 */
template<typename T, typename Index>
void clip_triangle(
    hrz::ArrayView<const lm::Vector<T, 2>> points,
    Index i0,
    Index i1,
    Index i2,
    const lm::Bbox<T, 2>& bbox,
    hrz::function_ref<void(Index i0, Index i1, Index i2)> rasterize_fn,
    hrz::function_ref<Index(const lm::Vector<T, 2>& pos, const lm::Vector<T, 3>& weight)>
        append_fn);

extern template void clip_triangle(
    hrz::ArrayView<const lm::dvec2> points,
    uint32_t i0,
    uint32_t i1,
    uint32_t i2,
    const lm::dbbox2& bbox,
    hrz::function_ref<void(uint32_t i0, uint32_t i1, uint32_t i2)> rasterize_fn,
    hrz::function_ref<uint32_t(const lm::dvec2&, const lm::dvec3&)> append_fn);

/**
 * Clips a segment to a bbox and calls the rasterize function with the
 * clipped points and the interpolation factors [0..1], which can be used
 * to interpolate additional attributes along the segment.
 */
template<typename T>
void clip_segment(
    lm::Vector<T, 2> p0,
    lm::Vector<T, 2> p1,
    const lm::Bbox<T, 2>& bbox,
    hrz::function_ref<void(const lm::Vector<T, 2>& p0, const lm::Vector<T, 2>& p1, T t0, T t1)>
        rasterize_fn);

extern template void clip_segment(
    lm::vec2 p0,
    lm::vec2 p1,
    const lm::bbox2& bbox,
    hrz::function_ref<void(const lm::vec2& p0, const lm::vec2& p1, float attr0, float attr1)>
        rasterize_fn);

extern template void clip_segment(
    lm::dvec2 p0,
    lm::dvec2 p1,
    const lm::dbbox2& bbox,
    hrz::function_ref<void(const lm::dvec2& p0, const lm::dvec2& p1, double attr0, double attr1)>
        rasterize_fn);

/**
 * The declare_point callback is called for each new point of the clipped polygon.
 * It gives the position of the new point, the indices of the two original points
 * and the interpolation factor [0..1].
 * The implicit index of the new point is pts.size() + N where N is the number of
 * points already declared.
 *
 * When done, the done callback is called with the list of clipped points and their
 * indices.
 */
template<typename T>
void clip_convex_polygon(
    std::span<const lm::Vector<T, 2>> pts,
    const lm::Bbox<T, 2>& bbox,
    hrz::function_ref<void(const lm::Vector<T, 2>&, int, int, float)> declare_point,
    hrz::function_ref<void(std::span<const std::pair<lm::Vector<T, 2>, int>>)> done);

extern template void clip_convex_polygon(
    std::span<const lm::vec2> pts,
    const lm::bbox2& bbox,
    hrz::function_ref<void(const lm::vec2&, int, int, float)> declare_point,
    hrz::function_ref<void(std::span<const std::pair<lm::vec2, int>>)> done);

extern template void clip_convex_polygon(
    std::span<const lm::dvec2> pts,
    const lm::dbbox2& bbox,
    hrz::function_ref<void(const lm::dvec2&, int, int, float)> declare_point,
    hrz::function_ref<void(std::span<const std::pair<lm::dvec2, int>>)> done);

} // namespace hrz
