#include "hrz_common_maths.h"

#include <assert.h>

#include <algorithm>

namespace
{
template<typename T>
inline T _dist(
    const lm::Vector<T, 2> p,
    const lm::Vector<T, 2>& origin,
    const lm::Vector<T, 2>& dir)
{
    return lm::dot(p - origin, dir);
}

// See https://en.wikipedia.org/wiki/Quickhull
template<typename T>
void _quickhull_inner(
    gsl::span<lm::Vector<T, 2>> pts,
    const lm::Vector<T, 2>& p,
    const lm::Vector<T, 2>& q,
    std::vector<lm::Vector<T, 2>>& hull)
{
    if (pts.size() == 0) return;

    lm::Vector<T, 2> normal(p.y - q.y, q.x - p.x);

    size_t max_index = 0;
    T max_dist = _dist(pts[0], p, normal);

    // We can't use max_element here because otherwise we'd have to evaluate
    // the distance multiple times for each point.
    for (size_t i = 1; i < pts.size(); ++i)
    {
        T dist = _dist(pts[i], p, normal);
        if (dist > max_dist)
        {
            max_index = i;
            max_dist = dist;
        }
    }

    lm::Vector<T, 2> c = pts[max_index];

    // Remove c from the working points.
    pts[max_index] = pts[pts.size() - 1];
    pts = pts.subspan(0, pts.size() - 1);

    lm::Vector<T, 2> pc_normal(p.y - c.y, c.x - p.x);
    lm::Vector<T, 2> cq_normal(c.y - q.y, q.x - c.x);

    auto pivot_pc = std::partition(
        pts.begin(), pts.end(),
        [&](const lm::Vector<T, 2>& v) -> bool { return _dist(v, p, pc_normal) > (T)0; });

    size_t pivot_pc_index = std::distance(pts.begin(), pivot_pc);
    gsl::span<lm::Vector<T, 2>> above_pc = pts.subspan(0, pivot_pc_index);
    pts = pts.subspan(pivot_pc_index);

    auto pivot_cq = std::partition(
        pts.begin(), pts.end(),
        [&](const lm::Vector<T, 2>& v) -> bool { return _dist(v, c, cq_normal) > (T)0; });

    size_t pivot_cq_index = std::distance(pts.begin(), pivot_cq);
    gsl::span<lm::Vector<T, 2>> above_cq = pts.subspan(0, pivot_cq_index);

    _quickhull_inner(above_pc, p, c, hull);
    hull.push_back(c);
    _quickhull_inner(above_cq, c, q, hull);
}

} // namespace

namespace hrz
{
template<typename T>
void compute_convex_hull(gsl::span<const lm::Vector<T, 2>> pts, std::vector<lm::Vector<T, 2>>& hull)
{
    hull.clear();

    if (pts.size() == 0) return;

    // Find extrema along X axis.
    // This will define the first segment we partition the space with.
    auto extrema_it = std::minmax_element(
        pts.begin(), pts.end(),
        [](const lm::Vector<T, 2>& a, const lm::Vector<T, 2>& b) { return a.x < b.x; });

    lm::Vector<T, 2> pt_a = *extrema_it.first;
    lm::Vector<T, 2> pt_b = *extrema_it.second;

    // This is a copy of pts. We use it so we can reorder stuff in it.
    std::vector<lm::Vector<T, 2>> working_pts;
    working_pts.reserve(pts.size());

    std::remove_copy_if(
        pts.begin(), pts.end(), std::back_inserter(working_pts),
        [&](const lm::Vector<T, 2>& v) -> bool { return v == pt_a || v == pt_b; });

    lm::Vector<T, 2> ab_normal(pt_a.y - pt_b.y, pt_b.x - pt_a.x);

    // Partition the points in the array depending on what side of the AB
    // segment then fall on.
    auto pivot = std::partition(
        working_pts.begin(), working_pts.end(),
        [&](const lm::Vector<T, 2>& v) -> bool { return _dist(v, pt_a, ab_normal) > (T)0; });

    size_t pivot_index = std::distance(working_pts.begin(), pivot);
    gsl::span<lm::Vector<T, 2>> working_pts_span(working_pts);
    gsl::span<lm::Vector<T, 2>> positive_span = working_pts_span.subspan(0, pivot_index);
    gsl::span<lm::Vector<T, 2>> negative_span = working_pts_span.subspan(pivot_index);

    assert(positive_span.size() + negative_span.size() == working_pts.size());

    hull.push_back(pt_a);
    _quickhull_inner(positive_span, pt_a, pt_b, hull);
    hull.push_back(pt_b);
    _quickhull_inner(negative_span, pt_b, pt_a, hull);
}

template void compute_convex_hull(gsl::span<const lm::vec2> pts, std::vector<lm::vec2>& hull);

template void compute_convex_hull(gsl::span<const lm::dvec2> pts, std::vector<lm::dvec2>& hull);

} // namespace hrz
