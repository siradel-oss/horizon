#include "hrz_jobs_clipping.h"

#include <hrz_fnd_inlined_vector.h>

#include <assert.h>

#include <algorithm>
#include <limits>
#include <optional>

namespace
{
template<typename T>
struct Point
{
    using Scalar = T;

    lm::Vector<T, 2> p;

    constexpr const lm::Vector<T, 2>& pos() const { return p; }

    static inline Point<T> interp(const Point<T>& a, const Point<T>& b, T t)
    {
        lm::Vector<T, 2> p = a.p * ((T)1.0 - t) + b.p * t;
        return {p};
    }
};

template<typename T, typename Index>
struct IndexedPoint
{
    using Scalar = T;

    lm::Vector<T, 2> p;
    std::optional<Index> i;

    constexpr const lm::Vector<T, 2>& pos() const { return p; }

    static inline IndexedPoint<T, Index> interp(
        const IndexedPoint<T, Index>& a,
        const IndexedPoint<T, Index>& b,
        T t)
    {
        lm::Vector<T, 2> p = a.p * ((T)1.0 - t) + b.p * t;
        return {p, std::nullopt};
    }
};

template<typename T, typename Attr>
struct PointWithAttribute
{
    using Scalar = T;

    lm::Vector<T, 2> p;
    Attr attr{};

    constexpr const lm::Vector<T, 2>& pos() const { return p; }

    static inline PointWithAttribute<T, Attr> interp(
        const PointWithAttribute<T, Attr>& a,
        const PointWithAttribute<T, Attr>& b,
        T t)
    {
        lm::Vector<T, 2> p = a.p * ((T)1.0 - t) + b.p * t;
        Attr attr = a.attr * ((T)1.0 - t) + b.attr * t;
        return {p, attr};
    }
};

using PointWithUv = PointWithAttribute<float, lm::vec2>;

// n.x * x + n.y * y + c = 0
template<typename P>
struct HalfSpace
{
    using T = typename P::Scalar;

    lm::Vector<T, 2> n;
    T c;

    enum Side
    {
        Inside,
        OnLine,
        Outside,
    };

    inline Side side(const lm::Vector<T, 2>& p) const
    {
        T res = p.x * n.x + p.y * n.y + c;
        T eps = std::abs(res) * std::numeric_limits<T>::epsilon();
        if (res > eps)
            return Inside;
        else if (res < -eps)
            return Outside;
        else
            return OnLine;
    }

    P intersect(const P& pa, const P& pb) const
    {
        // We have the half plane defined by n.p + c = 0
        // The segment from pa to pb is defined by
        //      x(t) = pa.x + (pb.x - pa.x) * t
        //      y(t) = pa.y + (pb.y - pa.y) * t
        // By injecting the segment equation into the half-plane equation
        // we can solve for t and we get
        //         n . pa + c
        // t = - ---------------
        //        n . (pb - pa)
        //
        // Note that we never check for parallel lines, the caller is responsible
        // for this.
        // t then gives us the intersecting point position by lerping.

        T t = -(lm::dot(n, pa.pos()) + c) / lm::dot(n, (pb.pos() - pa.pos()));
        return P::interp(pa, pb, t);
    }
};

template<typename P>
void _clip_triangle(
    const P& pt0,
    const P& pt1,
    const P& pt2,
    const lm::Bbox<typename P::Scalar, 2>& bbox,
    const std::function<void(const P& p0, const P& p1, const P& p2)>& rasterize_fn)
{
    using T = typename P::Scalar;

    const auto& p0 = pt0.pos();
    const auto& p1 = pt1.pos();
    const auto& p2 = pt2.pos();

    // We compute the bbox of the triangle so we can avoid clipping against
    // half spaces that we are completely inside or outside of.
    T txmin = std::min(std::min(p0.x, p1.x), p2.x);
    T txmax = std::max(std::max(p0.x, p1.x), p2.x);
    T tymin = std::min(std::min(p0.y, p1.y), p2.y);
    T tymax = std::max(std::max(p0.y, p1.y), p2.y);
    lm::Bbox<T, 2> bounds =
        lm::Bbox<T, 2>(lm::Vector<T, 2>(txmin, tymin), lm::Vector<T, 2>(txmax, tymax));

    // If we are completely outside of the clip space, we don't do anything.
    if (!lm::intersect(bbox, bounds))
    {
        return;
    }

    // We'll be ping-ponging those two buffers during clipping.
    static const int MAX_POINTS = 24;
    P points_0[MAX_POINTS];
    P points_1[MAX_POINTS];

    P* points_in = (P*)points_0;
    P* points_out = (P*)points_1;
    int in_count = 0;
    int out_count = 0;

    points_out[out_count++] = pt0;
    points_out[out_count++] = pt1;
    points_out[out_count++] = pt2;

    // https://en.wikipedia.org/wiki/Sutherland%E2%80%93Hodgman_algorithm
    auto clip = [&](const HalfSpace<P>& hs)
    {
        std::swap(points_in, points_out);
        std::swap(in_count, out_count);
        out_count = 0;

        P prev = points_in[in_count - 1];
        typename HalfSpace<P>::Side prev_side = hs.side(prev.pos());

        for (int i = 0; i < in_count; ++i)
        {
            P curr = points_in[i];
            typename HalfSpace<P>::Side curr_side = hs.side(curr.pos());

            // One in and one out, add the intersected point
            if ((curr_side == HalfSpace<P>::Outside && prev_side == HalfSpace<P>::Inside)
                || (curr_side == HalfSpace<P>::Inside && prev_side == HalfSpace<P>::Outside))
            {
                // Compute intersected here
                assert(out_count < MAX_POINTS);
                points_out[out_count++] = hs.intersect(prev, curr);
            }

            if (curr_side != HalfSpace<P>::Outside)
            {
                assert(out_count < MAX_POINTS);
                points_out[out_count++] = curr;
            }

            prev = curr;
            prev_side = curr_side;
        }
    };

    // We always test the bounding box before clipping to we can avoid
    // unnecessary work.
    if (bounds.min.x < bbox.min.x)
    {
        clip({{1, 0}, -bbox.min.x});
    }

    if (bounds.max.x > bbox.max.x)
    {
        clip({{-1, 0}, bbox.max.x});
    }

    if (bounds.min.y < bbox.min.y)
    {
        clip({{0, 1}, -bbox.min.y});
    }

    if (bounds.max.y > bbox.max.y)
    {
        clip({{0, -1}, bbox.max.y});
    }

    if (out_count >= 3)
    {
        // We clipped a convex polygon by another one, which gives us
        // a convex polygon, so it's OK to triangulate it naively using
        // triangle strips.

        const P& pa = points_out[0];

        for (int i = 1; i < out_count - 1; ++i)
        {
            const P& pb = points_out[i];
            const P& pc = points_out[i + 1];

            rasterize_fn(pa, pb, pc);
        }
    }
}

template<typename P>
void _clip_segment(
    P pt0,
    P pt1,
    const lm::Bbox<typename P::Scalar, 2>& bbox,
    const std::function<void(const P& p0, const P& p1)>& rasterize_fn)
{
    using T = typename P::Scalar;

    const auto& p0 = pt0.pos();
    const auto& p1 = pt1.pos();

    // We compute the bbox of the segment so we can avoid clipping against
    // half spaces that we are completely inside or outside of.
    T txmin = std::min(p0.x, p1.x);
    T txmax = std::max(p0.x, p1.x);
    T tymin = std::min(p0.y, p1.y);
    T tymax = std::max(p0.y, p1.y);
    lm::Bbox<T, 2> bounds =
        lm::Bbox<T, 2>(lm::Vector<T, 2>(txmin, tymin), lm::Vector<T, 2>(txmax, tymax));

    // If we are completely outside of the clip space, we don't do anything.
    if (!lm::intersect(bbox, bounds))
    {
        return;
    }

    auto clip = [&](const HalfSpace<P>& hs) -> bool
    {
        typename HalfSpace<P>::Side side0 = hs.side(pt0.pos());
        typename HalfSpace<P>::Side side1 = hs.side(pt1.pos());

        if (side0 == HalfSpace<P>::Outside && side1 == HalfSpace<P>::Outside)
        {
            return false;
        }
        else if (side0 == HalfSpace<P>::Outside && side1 != HalfSpace<P>::Outside)
        {
            pt0 = hs.intersect(pt0, pt1);
            return true;
        }
        else if (side0 != HalfSpace<P>::Outside && side1 == HalfSpace<P>::Outside)
        {
            pt1 = hs.intersect(pt0, pt1);
            return true;
        }
        else
        {
            return true;
        }
    };

    // We always test the bounding box before clipping to we can avoid
    // unnecessary work.
    if (bounds.min.x < bbox.min.x)
    {
        if (!clip({{1, 0}, -bbox.min.x})) return;
    }

    if (bounds.max.x > bbox.max.x)
    {
        if (!clip({{-1, 0}, bbox.max.x})) return;
    }

    if (bounds.min.y < bbox.min.y)
    {
        if (!clip({{0, 1}, -bbox.min.y})) return;
    }

    if (bounds.max.y > bbox.max.y)
    {
        if (!clip({{0, -1}, bbox.max.y})) return;
    }

    rasterize_fn(pt0, pt1);
}
} // namespace

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
        const lm::vec2& uv2)>& rasterize_fn)
{
    _clip_triangle<PointWithUv>(
        {p0, uv0}, {p1, uv1}, {p2, uv2}, bbox,
        [&](const PointWithUv& a, const PointWithUv& b, const PointWithUv& c)
        { rasterize_fn(a.p, b.p, c.p, a.attr, b.attr, c.attr); });
}

template<typename T>
void clip_triangle(
    const lm::Vector<T, 2>& p0,
    const lm::Vector<T, 2>& p1,
    const lm::Vector<T, 2>& p2,
    const lm::Bbox<T, 2>& bbox,
    const std::function<
        void(const lm::Vector<T, 2>& p0, const lm::Vector<T, 2>& p1, const lm::Vector<T, 2>& p2)>&
        rasterize_fn)
{
    _clip_triangle<Point<T>>(
        {p0}, {p1}, {p2}, bbox,
        [&](const Point<T>& a, const Point<T>& b, const Point<T>& c)
        { rasterize_fn(a.p, b.p, c.p); });
}

template void clip_triangle(
    const lm::vec2& p0,
    const lm::vec2& p1,
    const lm::vec2& p2,
    const lm::bbox2& bbox,
    const std::function<void(const lm::vec2& p0, const lm::vec2& p1, const lm::vec2& p2)>&
        rasterize_fn);

template void clip_triangle(
    const lm::dvec2& p0,
    const lm::dvec2& p1,
    const lm::dvec2& p2,
    const lm::dbbox2& bbox,
    const std::function<void(const lm::dvec2& p0, const lm::dvec2& p1, const lm::dvec2& p2)>&
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
        T attr2)>& rasterize_fn)
{
    _clip_triangle<PointWithAttribute<T, T>>(
        {p0, attr0}, {p1, attr1}, {p2, attr2}, bbox,
        [&](const PointWithAttribute<T, T>& a, const PointWithAttribute<T, T>& b,
            const PointWithAttribute<T, T>& c)
        { rasterize_fn(a.p, b.p, c.p, a.attr, b.attr, c.attr); });
}

template void clip_triangle(
    const lm::vec2& p0,
    const lm::vec2& p1,
    const lm::vec2& p2,
    float attr0,
    float attr1,
    float attr2,
    const lm::bbox2& bbox,
    const std::function<void(
        const lm::vec2& p0,
        const lm::vec2& p1,
        const lm::vec2& p2,
        float attr0,
        float attr1,
        float attr2)>& rasterize_fn);

template void clip_triangle(
    const lm::dvec2& p0,
    const lm::dvec2& p1,
    const lm::dvec2& p2,
    double attr0,
    double attr1,
    double attr2,
    const lm::dbbox2& bbox,
    const std::function<void(
        const lm::dvec2& p0,
        const lm::dvec2& p1,
        const lm::dvec2& p2,
        double attr0,
        double attr1,
        double attr2)>& rasterize_fn);

template<typename T, typename Index>
void clip_triangle(
    hrz::ArrayView<const lm::Vector<T, 2>> points,
    Index i0,
    Index i1,
    Index i2,
    Index next_index,
    const lm::Bbox<T, 2>& bbox,
    const std::function<void(Index i0, Index i1, Index i2)>& rasterize_fn,
    const std::function<void(const lm::Vector<T, 2>&)>& append_fn)
{
    // A triangle can intersect the bbox up to 6 times
    hrz::InlinedVector<IndexedPoint<T, Index>, 6> new_points;

    auto get_index = [&](const IndexedPoint<T, Index> p) -> Index
    {
        if (!p.i.has_value())
        {
            for (const auto& new_point : new_points)
            {
                if (new_point.p == p.p)
                {
                    return new_point.i.value();
                }
            }

            append_fn(p.p);
            new_points.push_back({p.p, next_index});
            return next_index++;
        }

        return p.i.value();
    };

    _clip_triangle<IndexedPoint<T, Index>>(
        {points[i0], i0}, {points[i1], i1}, {points[i2], i2}, bbox,
        [&](const IndexedPoint<T, Index>& p0, const IndexedPoint<T, Index>& p1,
            const IndexedPoint<T, Index>& p2)
        { rasterize_fn(get_index(p0), get_index(p1), get_index(p2)); });
}

template void clip_triangle(
    hrz::ArrayView<const lm::dvec2> points,
    uint32_t i0,
    uint32_t i1,
    uint32_t i2,
    uint32_t next_index,
    const lm::dbbox2& bbox,
    const std::function<void(uint32_t i0, uint32_t i1, uint32_t i2)>& rasterize_fn,
    const std::function<void(const lm::dvec2&)>& append_fn);

template<typename T>
void clip_segment(
    const lm::Vector<T, 2>& p0,
    const lm::Vector<T, 2>& p1,
    T attr0,
    T attr1,
    const lm::Bbox<T, 2>& bbox,
    const std::function<
        void(const lm::Vector<T, 2>& p0, const lm::Vector<T, 2>& p1, T attr0, T attr1)>&
        rasterize_fn)
{
    _clip_segment<PointWithAttribute<T, T>>(
        {p0, attr0}, {p1, attr1}, bbox,
        [&](const PointWithAttribute<T, T>& a, const PointWithAttribute<T, T>& b)
        { rasterize_fn(a.p, b.p, a.attr, b.attr); });
}

template void clip_segment(
    const lm::vec2& p0,
    const lm::vec2& p1,
    float attr0,
    float attr1,
    const lm::bbox2& bbox,
    const std::function<void(const lm::vec2& p0, const lm::vec2& p1, float attr0, float attr1)>&
        rasterize_fn);

template void clip_segment(
    const lm::dvec2& p0,
    const lm::dvec2& p1,
    double attr0,
    double attr1,
    const lm::dbbox2& bbox,
    const std::function<void(const lm::dvec2& p0, const lm::dvec2& p1, double attr0, double attr1)>&
        rasterize_fn);

} // namespace hrz
