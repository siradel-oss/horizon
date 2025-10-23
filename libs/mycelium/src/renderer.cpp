#include "arena.h"
#include "log.h"
#include "mycelium_renderer.h"

#include <assert.h>

#include <algorithm>
#include <stdio.h>
#include <vector>

#if MYCELIUM_NATIVE
#    include <xmmintrin.h>
#endif

namespace
{
struct SortHeader
{
    uint64_t key;
    size_t index;
};

} // namespace

namespace my
{
using BinMask = Renderer::BinMask;

// There is some magic going here. Since we want to do frustum-sphere tests
// using single-precision floating-point vector instructions, and we may work
// with large coordinates, we need to somehow reduce the coordinates range so they
// fit in float vectors.
// To do so, we extract the translation part from the view matrix, this is 'pos'.
// The matrix that is then used to compute the frustum planes is the
// projection * linearized view matrix.
// This means that we have to offset the incoming coordinates by 'pos' before.
// We'll still lose a bit of precision, especially for the far plane and for
// objects far from the eye position, but it's ok because those are far away points.
// The math behind all that is fairly trivial transformations of the input matrices.
//      -slerouzic, 2020-02-04

bool FrustumCuller::intersects(const lm::dvec3& p_dp, double radius_dp) const
{
    if (radius_dp < 0.0) return false;

    const lm::vec3 p(p_dp - pos);
    const float radius = (float)radius_dp;

    // @Note This test is not very accurate. There are some objects in
    // the corners of the frustum that may not be culled but that should be.
    // At least we don't get any false negative...

#if MYCELIUM_NATIVE
    // All of this is exactly equivalent to the normal code below
    // except it doesn't do early exit (for obvious reasons...)

    // @Todo Can we do better by computing multiple views at once?
    __m128 px = _mm_set_ps1(p.x);
    __m128 py = _mm_set_ps1(p.y);
    __m128 pz = _mm_set_ps1(p.z);
    __m128 r = _mm_set_ps1(-radius);

    __m128 pl_x1 = _mm_set_ps(planes[0].x, planes[1].x, planes[2].x, planes[3].x);
    __m128 pl_x2 = _mm_set_ps(planes[4].x, planes[5].x, planes[5].x, planes[5].x);
    __m128 pl_y1 = _mm_set_ps(planes[0].y, planes[1].y, planes[2].y, planes[3].y);
    __m128 pl_y2 = _mm_set_ps(planes[4].y, planes[5].y, planes[5].y, planes[5].y);
    __m128 pl_z1 = _mm_set_ps(planes[0].z, planes[1].z, planes[2].z, planes[3].z);
    __m128 pl_z2 = _mm_set_ps(planes[4].z, planes[5].z, planes[5].z, planes[5].z);
    __m128 pl_w1 = _mm_set_ps(planes[0].w, planes[1].w, planes[2].w, planes[3].w);
    __m128 pl_w2 = _mm_set_ps(planes[4].w, planes[5].w, planes[5].w, planes[5].w);

    __m128 x1 = _mm_mul_ps(px, pl_x1);
    __m128 x2 = _mm_mul_ps(px, pl_x2);
    __m128 y1 = _mm_mul_ps(py, pl_y1);
    __m128 y2 = _mm_mul_ps(py, pl_y2);
    __m128 z1 = _mm_mul_ps(pz, pl_z1);
    __m128 z2 = _mm_mul_ps(pz, pl_z2);

    // We sum all independent parts before summing the results so we
    // can break down dependencies and this improve pipelining.
    __m128 sum1_1 = _mm_add_ps(x1, y1);
    __m128 sum1_2 = _mm_add_ps(z1, pl_w1);
    __m128 sum2_1 = _mm_add_ps(x2, y2);
    __m128 sum2_2 = _mm_add_ps(z2, pl_w2);

    __m128 r1 = _mm_add_ps(sum1_1, sum1_2);
    __m128 r2 = _mm_add_ps(sum2_1, sum2_2);

    __m128 comp = _mm_or_ps(_mm_cmplt_ps(r1, r), _mm_cmplt_ps(r2, r));
    int mask = _mm_movemask_ps(comp);

    return mask == 0;
#else
    for (int i = 0; i < 6; ++i)
    {
        float result = lm::dot(planes[i], lm::vec4(p, 1)) + radius;
        if (result < 0)
        {
            return false;
        }
    }

    return true;
#endif
}

bool FrustumCuller::intersects(const OrientedBoundingBox& bbox) const
{
    // See https://gamedev.stackexchange.com/a/44501

    lm::dmat3 orientation = {
        {bbox.u_axis.x, bbox.v_axis.x, bbox.w_axis.x},
        {bbox.u_axis.y, bbox.v_axis.y, bbox.w_axis.y},
        {bbox.u_axis.z, bbox.v_axis.z, bbox.w_axis.z},
    };

    auto classify = [&](const lm::dvec4& plane)
    {
        lm::dvec3 normal = orientation * plane.xyz;

        // Maximum extent in direction of plane normal
        double r = std::abs(bbox.u_half_length * normal.x) + std::abs(bbox.v_half_length * normal.y)
            + std::abs(bbox.w_half_length * normal.z);

        // Signed distance between box center and plane
        double d = lm::dot(plane.xyz, bbox.center) + plane.w;

        // Return signed distance
        if (std::abs(d) < r)
        {
            return 0.0;
        }
        else if (d < 0.0)
        {
            return d + r;
        }
        return d - r;
    };

    for (const auto& plane : world_planes)
    {
        double side = classify(plane);
        if (side > 0)
        {
            return false;
        }
    }

    return true;
}

FrustumCuller FrustumCuller::from_view(const lm::dmat4& proj, const lm::dmat4& view)
{
    lm::dmat3 view_linear = lm::dmat3(view.x.xyz, view.y.xyz, view.z.xyz);
    lm::dvec3 view_translation = view.w.xyz;
    lm::dmat3 view_linear_inv = lm::inverse(view_linear);

    double a = proj.z.z;
    double b = proj.w.z;
    double c = proj.z.w;
    double d = proj.w.w;

    FrustumCuller baked;
    baked.pos = -(view_linear_inv * view_translation);
    baked.dir = lm::normalize(view_linear_inv * lm::dvec3(0, 0, -1));
    baked.near = (b + d) / (a + c);
    baked.far = (b - d) / (a - c);

    lm::dmat4 view_linearized(
        lm::dvec4(view_linear.x, 0), lm::dvec4(view_linear.y, 0), lm::dvec4(view_linear.z, 0),
        lm::dvec4(0, 0, 0, 1));

    lm::dmat4 m = lm::transpose(proj * view_linearized);
    lm::dvec4 planes_dp[] = {m.w + m.x, m.w - m.x, m.w + m.y, m.w - m.y, m.w + m.z, m.w - m.z};

    for (unsigned int i = 0; i < 6; ++i)
    {
        double inv_mag = 1.0 / lm::length(planes_dp[i].xyz);
        baked.planes[i] = lm::vec4(planes_dp[i] * inv_mag);
    }

    lm::dmat4 pv_t = lm::transpose(proj * view);

    auto normalize = [](const lm::dvec4& plane)
    {
        double inv_length = 1.0 / lm::length(plane.xyz);
        lm::dvec4 p = plane;
        p *= inv_length;
        return p;
    };

    baked.world_planes[0] = normalize(-pv_t.w - pv_t.x);
    baked.world_planes[1] = normalize(-pv_t.w + pv_t.x);
    baked.world_planes[2] = normalize(-pv_t.w - pv_t.y);
    baked.world_planes[3] = normalize(-pv_t.w + pv_t.y);
    baked.world_planes[4] = normalize(-pv_t.w - pv_t.z);
    baked.world_planes[5] = normalize(-pv_t.w + pv_t.z);

    return baked;
}

struct BakedView
{
    View source;
    FrustumCuller culler;

    static BakedView from_view(const View& view)
    {
        BakedView baked;
        baked.source = view;
        baked.culler = FrustumCuller::from_view(view.projection, view.view);

        return baked;
    }
};

struct QueueImpl
{
    using RenderFunction = Renderer::Queue::RenderFunction;
    using ViewId = Renderer::ViewId;

    struct CullEntry
    {
        BinMask bin_mask;
        Renderer::ViewMask view_mask;
        lm::dvec3 center;
        uint32_t user_sort;
    };

    struct RenderEntry
    {
        const void* data;
        RenderFunction fn;
    };

    Arena _arena{2 * 1024 * 1024};
    std::vector<RenderEntry> _render_entries;
    std::vector<CullEntry> _cull_entries;
    BinMask _all_masks = 0;
    uint8_t _pass_id;

    // We use this buffer for sorting primitives. We declare it to recycle its memory.
    std::vector<SortHeader> _sort_headers;

    void* write_raw(const void* data, size_t size)
    {
        void* dst_data = _arena.alloc(size);
        if (data)
        {
            memcpy(dst_data, data, size);
        }
        return dst_data;
    }

    void* enqueue_raw(
        BinMask mask,
        const void* src_data,
        size_t data_size,
        RenderFunction fn,
        const lm::dvec3& center,
        uint32_t user_sort,
        Renderer::ViewMask view_mask)
    {
        void* dst_data = write_raw(src_data, data_size);

        _render_entries.push_back(RenderEntry{dst_data, fn});
        _cull_entries.push_back(CullEntry{mask, view_mask, center, user_sort});
        _all_masks |= mask;

        return dst_data;
    }

    void reset()
    {
        _arena.reset();
        _cull_entries.clear();
        _render_entries.clear();
        _all_masks = 0;
    }

    void draw(
        uint32_t render_type,
        ViewId view_id,
        const BakedView& view,
        unsigned int bin,
        my::DepthSortMode depth_sort_mode,
        RenderContext* r,
        ResourceBinder* rb,
        const void* user_data)
    {
        const BinMask mask = (BinMask)1 << bin;
        const Renderer::ViewMask view_mask = 1U << view_id;

        const bool use_depth = depth_sort_mode != my::DepthSortMode::NoSort;
        const bool invert_depth = depth_sort_mode == my::DepthSortMode::BackToFront;

        for (uint32_t i = 0; i < _cull_entries.size(); ++i)
        {
            const CullEntry& cull = _cull_entries[i];
            if ((cull.bin_mask & mask) && (cull.view_mask & view_mask))
            {
                uint64_t sort_key = ((uint64_t)cull.user_sort) << 32;
                if (use_depth)
                {
                    double depth = std::min(1.0, std::max(0.0, view.culler.depth(cull.center)));
                    if (invert_depth) depth = 1.0 - depth;
                    sort_key |= (uint64_t)(depth * 0xffffffff) & 0xffffffff;
                }

                _sort_headers.push_back(SortHeader{sort_key, i});
            }
        }

        std::ranges::sort(
            _sort_headers,
            [](const SortHeader& a, const SortHeader& b) -> bool { return a.key < b.key; });

        for (const SortHeader& it : _sort_headers)
        {
            const RenderEntry& render = _render_entries[it.index];
            render.fn(render_type, r, rb, user_data, render.data);
        }

        _sort_headers.clear();
    }
};

class RendererImpl : public Renderer, public Renderer::Culler, public Renderer::Queue
{
    struct Bin
    {
        DepthSortMode sort_mode;
        ViewMask view_mask;
    };

    BinMask _registered_bins = 0;
    uint32_t _bin_count = 0;
    Bin _bins[MaxBin];
    uint32_t _aux_view_count = 0;
    BakedView _views[MaxAuxiliaryView + 1];

    QueueImpl _queue;

public:
    void* enqueue_raw(
        BinMask mask,
        RenderFunction fn,
        const void* data,
        size_t data_size,
        const lm::dvec3& center,
        double radius,
        uint32_t user_sort) override
    {
        ViewMask view_mask = make_view_mask(mask);
        ViewMask culled_view_mask = 0;

        for (uint32_t i = 0; i < _aux_view_count + 1; ++i)
        {
            if ((view_mask & (1u << i)) && _views[i].culler.intersects(center, radius))
            {
                culled_view_mask |= 1u << i;
            }
        }

        if (culled_view_mask)
        {
            return _queue.enqueue_raw(
                mask, data, data_size, fn, center, user_sort, culled_view_mask);
        }

        return nullptr;
    }

    void* write_raw(const void* data, size_t size) override { return _queue.write_raw(data, size); }

    void register_bin(unsigned int bit, DepthSortMode sort_mode) override
    {
        if (bit >= MaxBin)
        {
            assert(!"Bin bit out of bounds (max 31)");
            return;
        }

        _registered_bins |= (BinMask)1 << bit;
        _bin_count += 1;
        _bins[bit].sort_mode = sort_mode;
    }

    void register_view(ViewId view_id, const View& view, BinMask bin_mask)
    {
        _views[view_id] = BakedView::from_view(view);

        for (uint32_t bin = 0; bin < _bin_count; ++bin)
        {
            if (bin_mask & (1u << bin))
            {
                _bins[bin].view_mask |= 1u << view_id;
            }
        }
    }

    void set_main_view(const View& view, BinMask bin_mask) override
    {
        register_view(MainView, view, bin_mask);
    }

    ViewId add_auxiliary_view(const View& view, BinMask bin_mask) override
    {
        if (_aux_view_count >= MaxAuxiliaryView)
        {
            assert(!"Too many auxiliary views");
            return 0;
        }

        ViewId view_id = _aux_view_count + 1;
        register_view(view_id, view, bin_mask);
        _aux_view_count += 1;
        return view_id;
    }

    ViewMask make_view_mask(BinMask bin_mask) const override
    {
        ViewMask view_mask = 0;

        for (uint32_t i = 0; i < (uint32_t)_bin_count; ++i)
        {
            if (bin_mask & (1u << i))
            {
                view_mask |= _bins[i].view_mask;
            }
        }

        return view_mask;
    }

    ViewMask make_view_mask(int view_count, const ViewId* views) const override
    {
        ViewMask view_mask = 0;

        for (uint32_t i = 0; i < (uint32_t)view_count; ++i)
        {
            view_mask |= (1u << views[i]);
        }

        return view_mask;
    }

    bool is_visible_in_main_view(const lm::dvec3& center, double radius) const override
    {
        return _views[0].culler.intersects(center, radius);
    }

    bool is_visible_in_some_views(const lm::dvec3& center, double radius, ViewMask view_mask)
        const override
    {
        for (uint32_t i = 0; i < _aux_view_count + 1; ++i)
        {
            if ((view_mask & (1u << i)) && _views[i].culler.intersects(center, radius))
            {
                return true;
            }
        }

        return false;
    }

    bool is_visible_in_some_views(
        const lm::dvec3& center,
        double radius,
        int view_count,
        const ViewId* views) const override
    {
        return is_visible_in_some_views(center, radius, make_view_mask(view_count, views));
    }

    bool is_visible_in_any_view(const lm::dvec3& center, double radius, BinMask bin_mask)
        const override
    {
        return is_visible_in_some_views(center, radius, make_view_mask(bin_mask));
    }

    bool is_visible_in_main_view(const OrientedBoundingBox& bbox) const override
    {
        return _views[0].culler.intersects(bbox);
    }

    bool is_visible_in_some_views(const OrientedBoundingBox& bbox, ViewMask view_mask)
        const override
    {
        for (uint32_t i = 0; i < _aux_view_count + 1; ++i)
        {
            if ((view_mask & (1u << i)) && _views[i].culler.intersects(bbox))
            {
                return true;
            }
        }

        return false;
    }

    bool is_visible_in_some_views(
        const OrientedBoundingBox& bbox,
        int view_count,
        const ViewId* views) const override
    {
        return is_visible_in_some_views(bbox, make_view_mask(view_count, views));
    }

    bool is_visible_in_any_view(const OrientedBoundingBox& bbox, BinMask bin_mask = AllBins)
        const override
    {
        return is_visible_in_some_views(bbox, make_view_mask(bin_mask));
    }

    lm::dvec3 get_eye_point(ViewId view_id) const override
    {
        if (view_id > _aux_view_count + 1)
        {
            MY_LOG_ERROR("View out of bounds");
            return lm::dvec3(0);
        }
        else
        {
            return _views[view_id].culler.pos;
        }
    }

    void collect_renderable(const Renderable& r) override { r.collect_render_info(*this, *this); }

    void collect_renderable(const UserDataRenderable& r, void* user_data) override
    {
        r.collect_render_info_user_data(*this, *this, user_data);
    }

    void draw(
        uint32_t render_type,
        ViewId view_id,
        uint32_t pass_count,
        const BinMask* pass_masks,
        RenderContext* r,
        ResourceBinder* rb,
        const void* user_data) override
    {
        if (view_id > _aux_view_count + 1)
        {
            MY_LOG_ERROR("View out of bounds");
            return;
        }

        if (pass_count > 32)
        {
            MY_LOG_ERROR("Too many passes");
        }

        for (uint8_t pass = 0; pass < pass_count; ++pass)
        {
            BinMask mask = pass_masks[pass];

            mask = mask & _registered_bins & _queue._all_masks;
            if (mask == 0) continue;

            const BakedView& view = _views[view_id];

            BinMask current_mask = 1;
            for (unsigned int i = 0; i < MaxBin; ++i)
            {
                if (mask & current_mask)
                {
                    _queue.draw(
                        render_type, view_id, view, i, _bins[i].sort_mode, r, rb, user_data);
                }

                current_mask <<= 1;
            }
        }
    }

    void reset() override
    {
        _queue.reset();
        _aux_view_count = 0;
        for (uint32_t i = 0; i < MaxBin; ++i)
        {
            _bins[i].view_mask = 0;
        }
    }

    const Renderer::Culler& as_culler() const override { return *this; }

    Renderer::Queue& as_queue() override { return *this; }

    View get_view(ViewId view_id) const override
    {
        if (view_id > _aux_view_count + 1)
        {
            MY_LOG_ERROR("View out of bounds");
            return View{lm::dmat4::identity(), lm::dmat4::identity()};
        }
        else
        {
            return _views[view_id].source;
        }
    }
};

Renderer* Renderer::create()
{
    return new RendererImpl();
}

} // namespace my
