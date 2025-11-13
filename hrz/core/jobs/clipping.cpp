#include "hrz/core/jobs/clipping.h"

#include "hrz/fnd/inlined_vector.h"
#include "hrz/fnd/maths.h"

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
        const lm::Vector<T, 2> p = a.p * ((T)1.0 - t) + b.p * t;
        return {p};
    }

    static inline Point<T> interp(
        const Point<T>& a,
        const Point<T>& b,
        const Point<T>& c,
        const lm::Vector<T, 3>& w)
    {
        const lm::Vector<T, 2> p = a.p * w.x + b.p * w.y + c.p * w.z;
        return {p};
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
        const lm::Vector<T, 2> p = a.p * ((T)1.0 - t) + b.p * t;
        const Attr attr = a.attr * ((T)1.0 - t) + b.attr * t;
        return {p, attr};
    }

    static inline PointWithAttribute<T, Attr> interp(
        const PointWithAttribute<T, Attr>& a,
        const PointWithAttribute<T, Attr>& b,
        const PointWithAttribute<T, Attr>& c,
        const lm::Vector<T, 3>& w)
    {
        const lm::Vector<T, 2> p = a.p * w.x + b.p * w.y + c.p * w.z;
        const Attr attr = a.attr * w.x + b.attr * w.y + c.attr * w.z;
        return {p, attr};
    }
};

using PointWithUv = PointWithAttribute<float, lm::vec2>;

// n.x * x + n.y * y + c = 0
template<typename T>
struct HalfSpace
{
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

    T intersect(const lm::Vector<T, 2>& pa, const lm::Vector<T, 2>& pb) const
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

        return -(lm::dot(n, pa) + c) / lm::dot(n, (pb - pa));
    }
};

template<typename P>
struct HalfSpaceWithPayload : public HalfSpace<typename P::Scalar>
{
    P intersect_interp(const P& pa, const P& pb) const
    {
        auto t = HalfSpace<typename P::Scalar>::intersect(pa.pos(), pb.pos());
        return P::interp(pa, pb, t);
    }
};

/**
 * Clips a triangle defined by the three points by the bbox.
 * The rasterize_fn is called for each resulting triangle with the three points.
 * The append_fn is called for each new vertex that is created during clipping.
 * Each vertex is guaranteed to be appended prior to being used in a rasterize_fn call.
 * The first three vertices have implicit indices 0, 1, 2.
 * Each subsequent vertex has an implicit index of 3 + N where N is the number
 * of vertices already appended.
 */
template<typename T>
void _clip_triangle(
    const lm::Vector<T, 2>& pt0,
    const lm::Vector<T, 2>& pt1,
    const lm::Vector<T, 2>& pt2,
    const lm::Bbox<T, 2>& bbox,
    hrz::function_ref<void(
        const lm::Vector<T, 2>& p0,
        const lm::Vector<T, 2>& p1,
        const lm::Vector<T, 2>& p2,
        int i0,
        int i1,
        int i2)> rasterize_fn,
    hrz::function_ref<void(const lm::Vector<T, 2>&, const lm::Vector<T, 3>& w)> append_fn)
{
    using Vec = lm::Vector<T, 2>;

    const Vec pts[3] = {pt0, pt1, pt2};

    struct PointWithWeights
    {
        Vec p;

        // The three components (x, y, z) are the interpolation weights
        // for the three source points (p0, p1, p2).
        // They are used to reconstruct attributes of the clipped points.
        lm::Vector<T, 3> w;

        // Each point is appended only once.
        // We keep this information here and don't append in the callback
        // of clip_convex_polygon because we only want to append the final
        // points, not the intermediate ones.
        std::optional<int> appended = std::nullopt;
    };

    hrz::InlinedVector<PointWithWeights, 24> pts_out;
    pts_out.push_back({pt0, {1, 0, 0}});
    pts_out.push_back({pt1, {0, 1, 0}});
    pts_out.push_back({pt2, {0, 0, 1}});

    int next_append_index = 3;

    auto maybe_append_final_point = [append_fn, &next_append_index](PointWithWeights& p)
    {
        if (!p.appended)
        {
            append_fn(p.p, p.w);
            p.appended = next_append_index++;
        }
    };

    hrz::clip_convex_polygon<T>(
        pts, bbox,
        [&pts_out](const lm::Vector<T, 2>& p, int i0, int i1, float t)
        {
            auto w = lm::mix(pts_out[i0].w, pts_out[i1].w, static_cast<T>(t));
            pts_out.push_back({p, w});
        },
        [rasterize_fn, &pts_out,
         &maybe_append_final_point](std::span<const std::pair<Vec, int>> clipped)
        {
            if (clipped.size() >= 3)
            {
                // We clipped a convex polygon by another one, which gives us
                // a convex polygon, so it's OK to triangulate it naively using
                // triangle strips.

                const int i0 = clipped[0].second;
                auto& pa = pts_out[i0];
                maybe_append_final_point(pa);

                for (int i = 1; i < (int)clipped.size() - 1; ++i)
                {
                    const int i1 = clipped[i].second;
                    const int i2 = clipped[i + 1].second;

                    auto& pb = pts_out[i1];
                    auto& pc = pts_out[i2];

                    maybe_append_final_point(pb);
                    maybe_append_final_point(pc);

                    rasterize_fn(
                        pa.p, pb.p, pc.p, pa.appended.value(), pb.appended.value(),
                        pc.appended.value());
                }
            }
        });
}

template<typename P>
void _clip_triangle(
    const P& pt0,
    const P& pt1,
    const P& pt2,
    const lm::Bbox<typename P::Scalar, 2>& bbox,
    hrz::function_ref<void(const P& p0, const P& p1, const P& p2)> rasterize_fn)
{
    using T = typename P::Scalar;

    hrz::InlinedVector<P, 24> clipped_points;
    clipped_points.emplace_back(pt0);
    clipped_points.emplace_back(pt1);
    clipped_points.emplace_back(pt2);

    _clip_triangle<T>(
        pt0.pos(), pt1.pos(), pt2.pos(), bbox,
        [&clipped_points, rasterize_fn](
            const lm::Vector<T, 2>&, const lm::Vector<T, 2>&, const lm::Vector<T, 2>&, int i0,
            int i1, int i2)
        { rasterize_fn(clipped_points[i0], clipped_points[i1], clipped_points[i2]); },
        [&clipped_points](const lm::Vector<T, 2>&, const lm::Vector<T, 3>& w)
        {
            clipped_points.push_back(
                P::interp(clipped_points[0], clipped_points[1], clipped_points[2], w));
        });
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
    hrz::function_ref<void(
        const lm::vec2& p0,
        const lm::vec2& p1,
        const lm::vec2& p2,
        const lm::vec2& uv0,
        const lm::vec2& uv1,
        const lm::vec2& uv2)> rasterize_fn)
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
    hrz::function_ref<
        void(const lm::Vector<T, 2>& p0, const lm::Vector<T, 2>& p1, const lm::Vector<T, 2>& p2)>
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
    hrz::function_ref<void(const lm::vec2& p0, const lm::vec2& p1, const lm::vec2& p2)>
        rasterize_fn);

template void clip_triangle(
    const lm::dvec2& p0,
    const lm::dvec2& p1,
    const lm::dvec2& p2,
    const lm::dbbox2& bbox,
    hrz::function_ref<void(const lm::dvec2& p0, const lm::dvec2& p1, const lm::dvec2& p2)>
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
        T attr2)> rasterize_fn)
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
    hrz::function_ref<void(
        const lm::vec2& p0,
        const lm::vec2& p1,
        const lm::vec2& p2,
        float attr0,
        float attr1,
        float attr2)> rasterize_fn);

template void clip_triangle(
    const lm::dvec2& p0,
    const lm::dvec2& p1,
    const lm::dvec2& p2,
    double attr0,
    double attr1,
    double attr2,
    const lm::dbbox2& bbox,
    hrz::function_ref<void(
        const lm::dvec2& p0,
        const lm::dvec2& p1,
        const lm::dvec2& p2,
        double attr0,
        double attr1,
        double attr2)> rasterize_fn);

template<typename T, typename Index>
void clip_triangle(
    hrz::ArrayView<const lm::Vector<T, 2>> points,
    Index i0,
    Index i1,
    Index i2,
    const lm::Bbox<T, 2>& bbox,
    hrz::function_ref<void(Index i0, Index i1, Index i2)> rasterize_fn,
    hrz::function_ref<Index(const lm::Vector<T, 2>&, const lm::Vector<T, 3>& w)> append_fn)
{
    hrz::InlinedVector<Index, 12> outside_indices;
    outside_indices.push_back(i0);
    outside_indices.push_back(i1);
    outside_indices.push_back(i2);

    _clip_triangle<T>(
        points[i0], points[i1], points[i2], bbox,
        [&outside_indices, rasterize_fn](
            const lm::Vector<T, 2>&, const lm::Vector<T, 2>&, const lm::Vector<T, 2>&, int i0,
            int i1, int i2)
        { rasterize_fn(outside_indices[i0], outside_indices[i1], outside_indices[i2]); },
        [&outside_indices, append_fn](const lm::Vector<T, 2>& p, const lm::Vector<T, 3>& w)
        { outside_indices.push_back(append_fn(p, w)); });
}

template void clip_triangle(
    hrz::ArrayView<const lm::dvec2> points,
    uint32_t i0,
    uint32_t i1,
    uint32_t i2,
    const lm::dbbox2& bbox,
    hrz::function_ref<void(uint32_t i0, uint32_t i1, uint32_t i2)> rasterize_fn,
    hrz::function_ref<uint32_t(const lm::dvec2&, const lm::dvec3&)> append_fn);

template<typename T>
void clip_segment(
    lm::Vector<T, 2> p0,
    lm::Vector<T, 2> p1,
    const lm::Bbox<T, 2>& bbox,
    hrz::function_ref<void(const lm::Vector<T, 2>&, const lm::Vector<T, 2>&, T, T)> rasterize_fn)
{
    // We compute the bbox of the segment so we can avoid clipping against
    // half spaces that we are completely inside or outside of.
    const lm::Bbox<T, 2> bounds(lm::min(p0, p1), lm::max(p0, p1));

    // If we are completely outside of the clip space, we don't do anything.
    if (!lm::intersect(bbox, bounds))
    {
        return;
    }

    T t0 = 0;
    T t1 = 1;

    auto clip = [&](const HalfSpace<T>& hs) -> bool
    {
        const typename HalfSpace<T>::Side side0 = hs.side(p0);
        const typename HalfSpace<T>::Side side1 = hs.side(p1);

        if (side0 == HalfSpace<T>::Outside && side1 == HalfSpace<T>::Outside)
        {
            return false;
        }
        else if (side0 == HalfSpace<T>::Outside && side1 != HalfSpace<T>::Outside)
        {
            auto t = hs.intersect(p0, p1);
            p0 = lm::mix(p0, p1, t);
            t0 = hrz::lerp(t0, t1, t);
            return true;
        }
        else if (side0 != HalfSpace<T>::Outside && side1 == HalfSpace<T>::Outside)
        {
            auto t = hs.intersect(p0, p1);
            p1 = lm::mix(p0, p1, t);
            t1 = hrz::lerp(t0, t1, t);
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

    rasterize_fn(p0, p1, t0, t1);
}

template void clip_segment(
    lm::vec2 p0,
    lm::vec2 p1,
    const lm::bbox2& bbox,
    hrz::function_ref<void(const lm::vec2& p0, const lm::vec2& p1, float attr0, float attr1)>
        rasterize_fn);

template void clip_segment(
    lm::dvec2 p0,
    lm::dvec2 p1,
    const lm::dbbox2& bbox,
    hrz::function_ref<void(const lm::dvec2& p0, const lm::dvec2& p1, double attr0, double attr1)>
        rasterize_fn);

template<typename T>
void clip_convex_polygon(
    std::span<const lm::Vector<T, 2>> pts,
    const lm::Bbox<T, 2>& bbox,
    hrz::function_ref<void(const lm::Vector<T, 2>&, int, int, float)> declare_point,
    hrz::function_ref<void(std::span<const std::pair<lm::Vector<T, 2>, int>>)> done)
{
    assert(pts.size() >= 3);

    // We compute the bbox of the triangle so we can avoid clipping against
    // half spaces that we are completely inside or outside of.
    lm::Bbox<T, 2> bounds = lm::Bbox<T, 2>::invalid();
    for (const auto& p : pts)
    {
        bounds = lm::expand(bounds, p);
    }

    // If we are completely outside of the clip space, we don't do anything.
    if (!lm::intersect(bbox, bounds))
    {
        return;
    }

    // We'll be ping-ponging those two buffers during clipping.
    static constexpr int kBufferSize = 24;

    // Point coordinates and index.
    hrz::InlinedVector<std::pair<lm::Vector<T, 2>, int>, kBufferSize> points_0;
    hrz::InlinedVector<std::pair<lm::Vector<T, 2>, int>, kBufferSize> points_1;

    auto* points_in = &points_0;
    auto* points_out = &points_1;
    auto next_index = static_cast<int>(pts.size());

    for (size_t i = 0; i < pts.size(); ++i)
    {
        points_out->emplace_back(pts[i], static_cast<int>(i));
    }

    // https://en.wikipedia.org/wiki/Sutherland%E2%80%93Hodgman_algorithm
    auto clip = [&](const HalfSpace<T>& hs)
    {
        std::swap(points_in, points_out);
        points_out->clear();

        if (points_in->empty())
        {
            return;
        }

        auto prev = points_in->back();
        typename HalfSpace<T>::Side prev_side = hs.side(prev.first);

        for (size_t i = 0; i < points_in->size(); ++i)
        {
            const auto curr = (*points_in)[i];
            const typename HalfSpace<T>::Side curr_side = hs.side(curr.first);

            // One in and one out, add the intersected point
            if ((curr_side == HalfSpace<T>::Outside && prev_side == HalfSpace<T>::Inside)
                || (curr_side == HalfSpace<T>::Inside && prev_side == HalfSpace<T>::Outside))
            {
                // Compute intersected here
                const auto t = hs.intersect(prev.first, curr.first);
                auto new_pos = lm::mix(prev.first, curr.first, t);
                declare_point(new_pos, prev.second, curr.second, (float)t);
                points_out->emplace_back(new_pos, next_index++);
            }

            if (curr_side != HalfSpace<T>::Outside)
            {
                points_out->push_back(curr);
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

    if (points_out->size() >= 3)
    {
        done(*points_out);
    }
}

template void clip_convex_polygon(
    std::span<const lm::vec2> pts,
    const lm::bbox2& bbox,
    hrz::function_ref<void(const lm::vec2&, int, int, float)> declare_point,
    hrz::function_ref<void(std::span<const std::pair<lm::vec2, int>>)> done);

template void clip_convex_polygon(
    std::span<const lm::dvec2> pts,
    const lm::dbbox2& bbox,
    hrz::function_ref<void(const lm::dvec2&, int, int, float)> declare_point,
    hrz::function_ref<void(std::span<const std::pair<lm::dvec2, int>>)> done);

} // namespace hrz
