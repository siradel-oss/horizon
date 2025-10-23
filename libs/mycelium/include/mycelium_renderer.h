#pragma once

#include "mycelium_backend.h"
#include "mycelium_types.h"

namespace my
{
enum
{
    MaxRenderGraphPasses = 64u,
    MaxBin = 32u,
    MaxAuxiliaryView = 31u,
    MainView = 0u,
    AllBins = 0xffffffffu,
};

enum class DepthSortMode
{
    NoSort,
    FrontToBack,
    BackToFront,
};

struct View
{
    lm::dmat4 projection;
    lm::dmat4 view;
};

struct OrientedBoundingBox
{
    lm::dvec3 center;
    lm::dvec3 u_axis;
    double u_half_length;
    lm::dvec3 v_axis;
    double v_half_length;
    lm::dvec3 w_axis;
    double w_half_length;
};

struct FrustumCuller
{
    lm::dvec3 pos, dir;
    double near, far;
    lm::vec4 planes[6];
    lm::dvec4 world_planes[6];

    inline double depth(const lm::dvec3& center) const
    {
        double dot = lm::dot(center - pos, dir);
        return (dot - near) / (far - near);
    }

    bool intersects(const lm::dvec3& p_dp, double radius_dp) const;

    bool intersects(const OrientedBoundingBox& bbox) const;

    constexpr bool operator==(const FrustumCuller& other) const = default;

    static FrustumCuller from_view(const lm::dmat4& proj, const lm::dmat4& view);
};

class ResourceBinder
{
public:
    struct State
    {
        uint32_t ubo_count;
        uint32_t texture_count;
        UboBinding* ubos;
        TextureBinding* textures;
    };

    static ResourceBinder* create();

    virtual ~ResourceBinder() = default;

    virtual void push_state() = 0;
    virtual void pop_state() = 0;

    virtual void bind(uint32_t count, const UboBinding*) = 0;
    virtual void bind(uint32_t count, const TextureBinding*) = 0;

    virtual State get_current_state() = 0;
};

class Renderer
{
public:
    using BinMask = uint32_t;
    using ViewId = uint8_t;
    using ViewMask = uint32_t;

    class Queue
    {
    public:
        using RenderFunction = void (*)(
            uint32_t render_type,
            RenderContext*,
            ResourceBinder*,
            const void* user_data,
            const void* render_data);

        virtual ~Queue() = default;

        template<typename T>
        inline T* enqueue(
            BinMask mask,
            RenderFunction fn,
            const T* data,
            const lm::dvec3& center,
            double radius,
            uint32_t user_sort = 0)
        {
            static_assert(
                std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>,
                "Renderable data must be trivially copyable and destructible");

            return (T*)enqueue_raw(mask, fn, data, sizeof(T), center, radius, user_sort);
        }

        template<typename T>
        inline T* write(const T& data)
        {
            return (T*)write_raw(&data, sizeof(T));
        }

        template<typename T>
        inline T* write_n(const T* data, size_t count)
        {
            return (T*)write_raw(data, sizeof(T) * count);
        }

        virtual void* write_raw(const void* data, size_t size) = 0;

        virtual void* enqueue_raw(
            BinMask,
            RenderFunction,
            const void* data,
            size_t data_size,
            const lm::dvec3& center,
            double radius,
            uint32_t user_sort) = 0;
    };

    class Culler
    {
    public:
        virtual ~Culler() = default;

        virtual bool is_visible_in_main_view(const lm::dvec3& center, double radius) const = 0;

        virtual bool is_visible_in_any_view(
            const lm::dvec3& center,
            double radius,
            BinMask bin_mask = AllBins) const = 0;

        virtual bool is_visible_in_some_views(
            const lm::dvec3& center,
            double radius,
            int view_count,
            const ViewId* views) const = 0;

        virtual bool is_visible_in_some_views(
            const lm::dvec3& center,
            double radius,
            ViewMask view_mask) const = 0;

        virtual bool is_visible_in_main_view(const OrientedBoundingBox& bbox) const = 0;

        virtual bool is_visible_in_any_view(
            const OrientedBoundingBox& bbox,
            BinMask bin_mask = AllBins) const = 0;

        virtual bool is_visible_in_some_views(
            const OrientedBoundingBox& bbox,
            int view_count,
            const ViewId* views) const = 0;

        virtual bool is_visible_in_some_views(const OrientedBoundingBox& bbox, ViewMask view_mask)
            const = 0;

        virtual lm::dvec3 get_eye_point(ViewId = MainView) const = 0;
        virtual View get_view(ViewId) const = 0;
    };

    class UserDataRenderable
    {
    public:
        virtual ~UserDataRenderable() = default;

        virtual void collect_render_info_user_data(Queue&, const Culler&, void*) const = 0;
    };

    class Renderable : public UserDataRenderable
    {
    public:
        virtual void collect_render_info(Queue&, const Culler&) const = 0;

        void collect_render_info_user_data(Queue& queue, const Culler& culler, void*) const override
        {
            collect_render_info(queue, culler);
        }
    };

    static Renderer* create();
    virtual ~Renderer() = default;

    virtual void set_main_view(const View&, BinMask bin_mask = AllBins) = 0;
    virtual ViewId add_auxiliary_view(const View&, BinMask bin_mask = AllBins) = 0;

    virtual void register_bin(unsigned int bit, DepthSortMode) = 0;

    virtual void collect_renderable(const Renderable&) = 0;
    virtual void collect_renderable(const UserDataRenderable&, void* user_data) = 0;

    virtual ViewMask make_view_mask(BinMask bin_mask) const = 0;
    virtual ViewMask make_view_mask(int view_count, const ViewId* views) const = 0;

    virtual void draw(
        uint32_t render_type,
        ViewId,
        uint32_t pass_count,
        const BinMask* pass_masks,
        RenderContext*,
        ResourceBinder*,
        const void* user_data) = 0;

    virtual void reset() = 0;

    virtual const Culler& as_culler() const = 0;
    virtual Queue& as_queue() = 0;
};

} // namespace my
