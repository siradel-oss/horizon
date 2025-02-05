#pragma once

#include <hrz_fnd_array_view.h>

#include <gsl/gsl-lite.hpp>
#include <lin_maths.h>

#include <functional>

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
    const std::function<void(
        const lm::vec2& p0,
        const lm::vec2& p1,
        const lm::vec2& p2,
        const lm::vec2& uv0,
        const lm::vec2& uv1,
        const lm::vec2& uv2)>& rasterize_fn);

template<typename T>
void clip_triangle(
    const lm::Vector<T, 2>& p0,
    const lm::Vector<T, 2>& p1,
    const lm::Vector<T, 2>& p2,
    const lm::Bbox<T, 2>& bbox,
    const std::function<
        void(const lm::Vector<T, 2>& p0, const lm::Vector<T, 2>& p1, const lm::Vector<T, 2>& p2)>&
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
    const std::function<void(
        const lm::Vector<T, 2>& p0,
        const lm::Vector<T, 2>& p1,
        const lm::Vector<T, 2>& p2,
        T attr0,
        T attr1,
        T attr2)>& rasterize_fn);

template<typename T, typename Index>
void clip_triangle(
    hrz::ArrayView<const lm::Vector<T, 2>> points,
    Index i0,
    Index i1,
    Index i2,
    Index next_index,
    const lm::Bbox<T, 2>& bbox,
    const std::function<void(Index i0, Index i1, Index i2)>& rasterize_fn,
    const std::function<void(const lm::Vector<T, 2>&)>& append_fn);

template<typename T>
void clip_segment(
    const lm::Vector<T, 2>& p0,
    const lm::Vector<T, 2>& p1,
    T attr0,
    T attr1,
    const lm::Bbox<T, 2>& bbox,
    const std::function<
        void(const lm::Vector<T, 2>& p0, const lm::Vector<T, 2>& p1, T attr0, T attr1)>&
        rasterize_fn);

} // namespace hrz
