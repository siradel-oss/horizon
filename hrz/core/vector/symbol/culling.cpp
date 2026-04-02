#include "hrz/core/vector/symbol/culling.h"

#include "hrz/common/monitoring_defs.h"
#include "hrz/core/clock.h"
#include "hrz/core/global_flags.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/jobs/vector_tiles_jobs_params.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/screen_space.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/inlined_vector.h"

namespace
{

static constexpr double CullDelayS = 0.6;

static constexpr uint32_t BITSET_TEXTURE_WIDTH =
    hrz_jobs::SymbolCullingResponse::BITSET_TEXTURE_WIDTH;

struct BitsetTexture
{
    uint32_t count;
    lm::uvec2 texture_size;
    std::vector<uint32_t> bits;

    lm::ubbox2 ones_bbox = lm::ubbox2::invalid();
    lm::ubbox2 dirty_bbox = lm::ubbox2::invalid();

    hrz::monitoring::ResourceOwner resource_owner;
    std::vector<std::pair<hrz::MetadataString, hrz::MetadataString>> metadata;
    my::ResourceHandle texture = my::ResourceHandle::null();

    BitsetTexture(
        uint32_t count_,
        const hrz::monitoring::ResourceOwner& resource_owner_,
        std::initializer_list<std::pair<hrz::MetadataString, hrz::MetadataString>> metadata_) :
        count{count_}, resource_owner{resource_owner_}
    {
        uint32_t pixel_count = (count + 31U) / 32U;
        uint32_t width = std::min(pixel_count, BITSET_TEXTURE_WIDTH);
        uint32_t height = (pixel_count + BITSET_TEXTURE_WIDTH - 1) / BITSET_TEXTURE_WIDTH;
        assert(width * height >= pixel_count);

        texture_size = lm::uvec2{width, height};
        bits.resize(width * height, 0);

        for (const auto& it : metadata_)
        {
            metadata.push_back({it.first, it.second});
        }
    }

    void initialize_gpu(hrz::Render* render)
    {
        if (!texture && !bits.empty())
        {
            auto data = std::as_bytes(std::span<const uint32_t>(bits));

            my::TextureResource res;
            res.layout.type = my::TextureLayout::Type2D;
            res.layout.format = my::TextureFormat::R32UI;
            res.layout.width = texture_size.x;
            res.layout.height = texture_size.y;
            res.layout.depth = 1;
            res.layout.levels = 1;
            res.data = {&data, 1};
            res.generate_mipmaps = false;

            texture = render->rc->alloc(&res, resource_owner, metadata);
            render->rc->monitoring->register_gpu_resource_metadata(
                texture, "culling bitset"_ss, ""_ss);

            dirty_bbox = lm::ubbox2::invalid();
        }
    }

    void work_gpu(hrz::Render* render)
    {
        if (texture && lm::is_valid(dirty_bbox))
        {
            lm::uvec2 dirty_size = lm::size(dirty_bbox);
            size_t uint32_offset = dirty_bbox.min.x + dirty_bbox.min.y * BITSET_TEXTURE_WIDTH;
            render->my->update_texture(
                texture, my::TextureFormat::R32UI, 0, dirty_bbox.min.x, dirty_bbox.min.y, 0,
                dirty_size.x + 1, dirty_size.y + 1, 1,
                std::as_bytes(std::span<const uint32_t>(bits).subspan(uint32_offset)),
                my::Instance::DataHasTargetTextureSize);
            dirty_bbox = lm::ubbox2::invalid();
        }
    }

    void destroy(hrz::Render* render)
    {
        if (texture)
        {
            render->rc->dealloc(texture);
        }
    }

    // Returns whether work_gpu must be called
    bool set_ones(std::span<const uint32_t> new_data, lm::ubbox2 new_ones_bbox)
    {
        if (new_data.size() != bits.size())
        {
            assert(!"Wrong bitset size");
            return false;
        }
        else if (lm::is_valid(new_ones_bbox))
        {
            memcpy(bits.data(), new_data.data(), new_data.size_bytes());
            dirty_bbox = lm::merge(dirty_bbox, lm::merge(ones_bbox, new_ones_bbox));
            ones_bbox = new_ones_bbox;
            return true;
        }
        else
        {
            return reset();
        }
    }

    // Returns whether work_gpu must be called
    bool reset()
    {
        if (lm::is_valid(ones_bbox))
        {
            std::ranges::fill(bits, 0);
            dirty_bbox = ones_bbox;
            ones_bbox = lm::ubbox2::invalid();
            return true;
        }
        return false;
    }

    void all_ones()
    {
        std::ranges::fill(bits, 0xffffffffU);
        ones_bbox = lm::ubbox2{{0, 0}, {texture_size.x - 1, texture_size.y - 1}};
        dirty_bbox = ones_bbox;
    }
};

} // namespace

namespace hrz
{

struct SymbolCullingSystem
{
    struct Group
    {
        symbol_culling::GroupInfo group_info;
        hrz::BlobArray<hrz::vt::AnchorSpan> anchor_spans;
        hrz::BlobArray<hrz::vt::AnchorCullingInfo> anchors;

        bool requires_culling;

        BitsetTexture bitset_textures[SCENE_VIEW_COUNT];

        Group(
            symbol_culling::GroupInfo&& group_info_,
            hrz::BlobArray<hrz::vt::AnchorSpan> anchor_spans_,
            hrz::BlobArray<hrz::vt::AnchorCullingInfo> anchors_,
            bool ignore_occlusions,
            const hrz::monitoring::ResourceOwner& resource_owner,
            std::initializer_list<std::pair<hrz::MetadataString, hrz::MetadataString>> metadata) :
            group_info{std::move(group_info_)},
            anchor_spans{std::move(anchor_spans_)},
            anchors{std::move(anchors_)},
            bitset_textures{
                BitsetTexture(anchors.size(), resource_owner, metadata),
                BitsetTexture(anchors.size(), resource_owner, metadata)
            }
        {
            requires_culling = false;
            for (const auto& info : group_info.anchors)
            {
                if (!(info.flags & vt::AnchorFlag_CanOverlapOtherSymbols)
                    || (info.flags & vt::AnchorFlag_HidesOtherSymbols) || ignore_occlusions)
                {
                    requires_culling = true;
                    break;
                }
            }

            if (anchors.size() == 0)
            {
                requires_culling = false;
            }

            if (!requires_culling)
            {
                for (auto& texture : bitset_textures)
                {
                    texture.all_ones();
                }
            }
        }
    };

    using GroupIndexPool = hrz::GenIndexPool<uint64_t, 32, 32>;
    using GroupPool = hrz::GenObjectPool<Group, GroupIndexPool, 128>;

    GroupPool group_pool;

    hrz::flat_hash_set<uint64_t> groups_with_some_symbols_shown;
    hrz::flat_hash_set<uint64_t> groups_to_gpu_update;
    hrz::flat_hash_set<uint64_t> groups_to_destroy;

    // During a frame that is drawn, the vector tiles systems first call the schedule_draw_group
    // function for all groups that may appear on screen. Then the draw function is called. So we
    // accumulate the newly-drawn frame data in the new_ variables here, then in draw we swap them
    // with the saved_ variables. This allows us to always of the most recent drawn frame data on
    // hand.

    hrz::flat_hash_map<uint64_t, hrz::SceneViewBitset> new_groups_drawn;
    hrz::flat_hash_map<uint64_t, hrz::SceneViewBitset> saved_groups_drawn;

    // These hashes rely on the fact that the schedule_draw_group will be called in a deterministic
    // order for each displayed group, so any change in the hash will reflect a change in the groups
    // that are shown or not, and thus requires re-culling the symbols.

    uint64_t new_groups_drawn_hash = 0;
    uint64_t saved_groups_drawn_hash = 0;
    uint64_t last_cull_groups_drawn_hash = 0;

    uint64_t last_frame_draw_requested = 0;
    uint64_t last_frame_cull_updated = 0;

    uint64_t new_groups_frame;
    uint64_t saved_groups_frame;

    // Frame number of the last group that finished culling.
    uint64_t last_group_culled_frame = 0;

    double last_cull_start_time_s = 0;

    std::optional<hrz_jobs::CullSymbolsTicket> cull_ticket;

    constexpr bool should_update() const
    {
        return saved_groups_drawn_hash != last_cull_groups_drawn_hash
            || last_frame_draw_requested > last_frame_cull_updated;
    }
};

namespace symbol_culling
{

SymbolCullingSystem* create()
{
    return new SymbolCullingSystem();
}

void destroy(SymbolCullingSystem* sys)
{
    // We should probably destroy a bunch of stuff here, but also it's a singleton system so we
    // don't really care. If we care one day, destroy the bitset textures properly.
    delete sys;
}

RenderRequest work(
    SymbolCullingSystem* sys,
    JobScheduler* js,
    std::span<const RenderViewInfo> views_info)
{
    RenderRequest render_request;

    if (!sys->cull_ticket
        && (sys->last_cull_start_time_s + CullDelayS < hrz::clock::CurrentFrameRealTime.s)
        && sys->should_update())
    {
        hrz_jobs::SymbolCullingParams params;
        hrz::InlinedVector<my::FrustumCuller, SCENE_VIEW_COUNT> cullers;

        for (const auto& view_info : views_info)
        {
            hrz_jobs::SymbolCullingParams::ViewInfo out_view_info;
            out_view_info.view_index = view_info.view;
            out_view_info.position = view_info.cam_view_info.cam.pos;
            out_view_info.view_cc = lm::mat4(view_info.cam_view_info.cam.view_cc);
            out_view_info.proj = lm::mat4(view_info.cam_view_info.proj);
            out_view_info.inv_view = lm::mat4(view_info.cam_view_info.cam.inv_view);
            out_view_info.pixel_size_in_meters =
                render::compute_logical_pixel_size_in_meters(view_info.cam_view_info);
            out_view_info.small_size = lm::vec2(2.0)
                / (view_info.cam_view_info.viewport.size
                   * view_info.cam_view_info.viewport.device_pixel_ratio);
            out_view_info.perceived_distance = (float)view_info.perceived_distance;
            params.views.push_back(out_view_info);

            cullers.push_back(
                my::FrustumCuller::from_view(
                    view_info.cam_view_info.proj, view_info.cam_view_info.cam.view));
        }

        for (const auto& entry : sys->saved_groups_drawn)
        {
            auto* group = sys->group_pool.get_object(entry.first);

            if (!group || !group->requires_culling) continue;

            const auto& bsphere = group->group_info.culling_bsphere;
            SceneViewBitset visible_in_views;
            for (size_t i = 0; i < views_info.size(); ++i)
            {
                if (entry.second.is_set(views_info[i].view)
                    && cullers[i].intersects(bsphere.center, bsphere.radius))
                {
                    visible_in_views.set(views_info[i].view);
                }
            }

            if (visible_in_views.any())
            {
                hrz_jobs::SymbolCullingParams::Group out_group;
                out_group.handle = entry.first;
                out_group.visible_in_views = visible_in_views.bits();
                out_group.anchor_protos = group->group_info.anchors;
                out_group.anchor_spans = group->anchor_spans;
                out_group.anchors = group->anchors;
                out_group.origin = group->group_info.group_origin;
                out_group.z_index = group->group_info.representation_z_index;

                params.groups.push_back(std::move(out_group));
            }
            else
            {
                bool must_update = false;
                for (auto& texture : group->bitset_textures)
                {
                    must_update = texture.reset() || must_update;
                }

                if (must_update)
                {
                    sys->groups_to_gpu_update.insert(entry.first);
                }
            }
        }

        params.frame = sys->saved_groups_frame;

        sys->cull_ticket = hrz_jobs::add_job_cull_symbols(
            js, std::move(params), hrz::monitoring::systems::Symbols);
        sys->last_cull_groups_drawn_hash = sys->saved_groups_drawn_hash;
        sys->last_cull_start_time_s = hrz::clock::CurrentFrameRealTime.s;
    }
    else if (sys->cull_ticket && hrz_jobs::is_job_finished(js, sys->cull_ticket.value()))
    {
        auto ticket = std::exchange(sys->cull_ticket, std::nullopt).value();
        if (hrz_jobs::get_job_status(js, ticket) == job_scheduler::JobStatus::Finished_Success)
        {
            auto response = hrz_jobs::get_job_response(js, ticket);

            for (uint64_t handle : sys->groups_with_some_symbols_shown)
            {
                auto* group = sys->group_pool.get_object(handle);
                if (group)
                {
                    for (auto& texture : group->bitset_textures)
                    {
                        if (texture.reset())
                        {
                            sys->groups_to_gpu_update.insert(handle);
                        }
                    }
                }
            }

            for (auto& group_id : response.groups_to_reset)
            {
                auto* group = sys->group_pool.get_object(group_id.handle);
                if (group)
                {
                    if (group->bitset_textures[group_id.scene_view].reset())
                    {
                        sys->groups_to_gpu_update.insert(group_id.handle);
                    }
                }
            }

            std::array<hrz::BlobArray<uint32_t>::Data, SCENE_VIEW_COUNT> view_bitset_data;
            for (size_t i = 0; i < SCENE_VIEW_COUNT; ++i)
            {
                view_bitset_data[i] = response.view_bitsets[i].get_cdata();
            }

            sys->groups_with_some_symbols_shown.clear();
            for (auto& group_update : response.groups_to_update)
            {
                auto* group = sys->group_pool.get_object(group_update.handle);
                if (group)
                {
                    auto span = view_bitset_data[group_update.scene_view].as_span().subspan(
                        group_update.first_bitset_bucket, group_update.bitset_bucket_count);

                    if (group->bitset_textures[group_update.scene_view].set_ones(
                            span, group_update.ones_bbox))
                    {
                        sys->groups_to_gpu_update.insert(group_update.handle);
                    }

                    if (lm::is_valid(group_update.ones_bbox))
                    {
                        sys->groups_with_some_symbols_shown.insert(group_update.handle);
                    }
                }
            }

            sys->last_group_culled_frame = response.frame;

            render_request.request_visual_render(RenderRequest::VisualCause::SymbolCulling);
            sys->last_frame_cull_updated = hrz::clock::CurrentFrameNumber;
        }

        hrz_jobs::cancel_job(js, ticket);
    }
    else if (
        sys->saved_groups_drawn_hash == sys->last_cull_groups_drawn_hash
        && sys->last_group_culled_frame != sys->saved_groups_frame)
    {
        sys->last_group_culled_frame = sys->saved_groups_frame;
        render_request.request_visual_render(RenderRequest::VisualCause::SymbolCulling);
    }

    return render_request;
}

void work_gpu(SymbolCullingSystem* sys, Render* render)
{
    if (hrz::get_flag(hrz::Flag::DebugFreezeVectorTilesCulling))
    {
        return;
    }

    for (uint64_t handle : sys->groups_to_gpu_update)
    {
        auto* group = sys->group_pool.get_object(handle);
        if (group)
        {
            for (auto& texture : group->bitset_textures)
            {
                texture.work_gpu(render);
            }
        }
    }
    sys->groups_to_gpu_update.clear();

    for (uint64_t handle : sys->groups_to_destroy)
    {
        auto* group = sys->group_pool.get_object(handle);
        if (group)
        {
            for (auto& texture : group->bitset_textures)
            {
                texture.destroy(render);
            }
            sys->group_pool.release(handle);
        }
    }
    sys->groups_to_destroy.clear();
}

void draw(SymbolCullingSystem* sys, const RenderRequest& render_request)
{
    if (render_request.is_visual_render_caused_by(RenderRequest::VisualCause::Scene)
        || render_request.is_visual_render_caused_by(
            RenderRequest::VisualCause::VectorTileVisibilitySet))
    {
        sys->new_groups_drawn.swap(sys->saved_groups_drawn);
        sys->new_groups_drawn.clear();

        sys->saved_groups_drawn_hash = std::exchange(sys->new_groups_drawn_hash, 0);

        sys->saved_groups_frame = sys->new_groups_frame;
        sys->last_frame_draw_requested = hrz::clock::CurrentFrameNumber;
    }
}

bool is_working(SymbolCullingSystem* sys)
{
    return sys->cull_ticket.has_value() || sys->should_update()
        || !sys->groups_to_gpu_update.empty();
}

GroupHandle register_group(
    SymbolCullingSystem* sys,
    GroupInfo&& group_info,
    hrz::BlobArray<hrz::vt::AnchorSpan> anchor_spans,
    hrz::BlobArray<hrz::vt::AnchorCullingInfo> anchors,
    bool ignore_occlusions,
    const hrz::monitoring::ResourceOwner& resource_owner,
    std::initializer_list<std::pair<hrz::MetadataString, hrz::MetadataString>> metadata)
{
    auto handle = sys->group_pool.alloc(
        std::move(group_info), anchor_spans, anchors, ignore_occlusions, resource_owner, metadata);
    return {handle};
}

void unregister_group(SymbolCullingSystem* sys, GroupHandle handle)
{
    sys->groups_to_destroy.insert(handle.o);
}

std::array<my::ResourceHandle, SCENE_VIEW_COUNT> initialize_visibility_textures(
    SymbolCullingSystem* sys,
    GroupHandle handle,
    Render* render)
{
    std::array<my::ResourceHandle, SCENE_VIEW_COUNT> textures;

    auto* group = sys->group_pool.get_object(handle.o);
    if (group)
    {
        for (size_t i = 0; i < SCENE_VIEW_COUNT; ++i)
        {
            group->bitset_textures[i].initialize_gpu(render);
            textures[i] = group->bitset_textures[i].texture;
        }
    }

    return textures;
}

void schedule_draw_group(
    SymbolCullingSystem* sys,
    GroupHandle handle,
    SceneViewBitset views_visibility)
{
    if (views_visibility.any())
    {
        sys->new_groups_drawn.insert(std::make_pair(handle.o, views_visibility));
        sys->new_groups_drawn_hash =
            hrz::hash_values(sys->new_groups_drawn_hash, handle.o, views_visibility.bits());
        sys->new_groups_frame = hrz::clock::CurrentFrameNumber;
    }
}

uint64_t get_last_culled_frame(SymbolCullingSystem* sys)
{
    return sys->last_group_culled_frame;
}

} // namespace symbol_culling
} // namespace hrz
