#include "vector/hrz_core_vector_tiles.h"

#include "camera/hrz_core_camera_system.h"
#include "hrz_core_debug_draw.h"
#include "hrz_core_global_flags.h"
#include "hrz_core_render.h"
#include "hrz_core_selection.h"
#include "vector/hrz_core_vector_repr.h"
#include "vector/hrz_core_vector_tiles_actor.h"

#include <hrz_common_attributes.h>
#include <hrz_common_fmt.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proto_geo.h>
#include <hrz_common_style.h>
#include <hrz_common_tile_coords.h>
#include <hrz_common_vector_tiles.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_gen_index_pool.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_inlined_vector.h>
#include <hrz_fnd_meta.h>
#include <hrz_fnd_static_vector.h>
#include <hrz_protocol_all.h>

#include <lin_maths.h>
#include <mycelium_renderer.h>

extern "C"
{
#include <microui/microui.h>
}

#include <optional>
#include <utility>

namespace hrz::vt
{
namespace
{
const char* elevation_source_str(ElevationSource source)
{
    switch (source)
    {
        case ElevationSource::None: return "None";
        case ElevationSource::InferredFromParent: return "Parent";
        case ElevationSource::InferredFromChild: return "Child";
        case ElevationSource::GroundTruth: return "DTM";
        default: assert(false); return "<Unknown>";
    }
}

struct TileData
{
    hrz::vector_data::FeatureIds feature_ids;
    hrz::InlinedVector<hrz::vector_data::AttributeValues, 16> attributes;
    hrz::BlobArrayView<lm::dvec3> anchors;
};
} // namespace

struct VectorTiles
{
    bool _force_update_selection = false;
    uint64_t _layer_id{};
    SceneViewBitset _visible_in;

    std::optional<hrz::StaticVector<VectorTilesCuller, SCENE_VIEW_COUNT>> _cullers;
    bool _request_debug_info_for_debug_draw = false;
    bool _request_debug_info_for_dev_ui = false;

    bool _should_schedule_flat_overlays = false;

    hrz::flat_hash_set<hrz_proto::VectorReprType> _repr_types;
    hrz::flat_hash_map<hrz_proto::VectorReprType, uint64_t> _repr_channel_ids;

    VectorTilesActorChannel _actor_channel;

    // The visibility set that is currently displayable.
    std::optional<VisibilitySet> _current_visibility_set = std::nullopt;
    uint64_t _current_visibility_set_frame = 0;

    // The visibility sets that have been scheduled but not yet available.
    std::deque<std::pair<VisibilitySet, uint64_t>> _visibility_set_frame_queue;

    // The last visibility set that has been constructed by traversing the tiles, but that has not
    // been scheduled.
    std::optional<VisibilitySet> _new_visibility_set = std::nullopt;

    using TileDataId = uint32_t;
    using TileDataIdPool = hrz::GenIndexPool<TileDataId, 4, 20>;
    using TileDataPool = hrz::GenObjectPool<TileData, TileDataIdPool>;
    TileDataPool _tile_data_pool;
    hrz::flat_hash_map<TileId, TileDataId> _tile_id_to_tile_data_id;
    uint8_t _data_min_lod{};
    uint8_t _data_max_lod{};

    VectorTiles(
        uint64_t global_layer_id,
        uint32_t vector_data_layer,
        hrz_proto::MissingTilePolicy missing_tile_policy,
        bool static_tiles,
        uint32_t system_layer_picking_id,
        hrz::PlanetSurface* planet,
        VectorDataLoader* vdl,
        ActorRunner* ar) :
        // The VectorTiles object may have been re-created by the layer, so even
        // though the selection system has not been updated we need to get the brand
        // new selection at the first draw.
        _force_update_selection(true),
        _layer_id(global_layer_id),
        _actor_channel(spawn_vector_tiles_actor(
            global_layer_id,
            vector_data_layer,
            missing_tile_policy,
            static_tiles,
            system_layer_picking_id,
            planet,
            vdl,
            ar))
    {
    }

    ~VectorTiles()
    {
        for (const auto& it : _tile_id_to_tile_data_id)
        {
            const auto& tile_data_id = it.second;
            _tile_data_pool.release(tile_data_id);
        }
    }

    RenderRequest work(
        BlobAllocator* ba,
        JobScheduler* js,
        ReprRegistry* repr_reg,
        const SelectionSystem* selection)
    {
        HRZ_SCOPED_SAMPLE("vector tiles work");

        RenderRequest render_request;

        for (auto& generic_message : _actor_channel.receive())
        {
            std::visit(
                hrz::overload{
                    [&](from_actor::RegisterProperties& message)
                    {
                        assert(message.parser != nullptr);
                        repr_reg->register_properties(_layer_id, *message.parser);

                        _actor_channel.send(to_actor::SignalPropertiesRegistered{message.parser});
                    },
                    [&](from_actor::TileCoords& message)
                    {
                        TileDataId tile_data_id{};

                        auto it = _tile_id_to_tile_data_id.find(message.tile_id);
                        if (it == _tile_id_to_tile_data_id.end())
                        {
                            tile_data_id = _tile_data_pool.alloc();
                            _tile_id_to_tile_data_id.insert({message.tile_id, tile_data_id});
                            _data_min_lod = std::min(_data_min_lod, message.tile_coords.lod);
                            _data_max_lod = std::max(_data_max_lod, message.tile_coords.lod);
                        }
                    },
                    [&](from_actor::TileFeatureIds& message)
                    {
                        TileDataId tile_data_id{};

                        auto it = _tile_id_to_tile_data_id.find(message.tile_id);
                        if (it != _tile_id_to_tile_data_id.end())
                        {
                            tile_data_id = it->second;
                        }
                        else
                        {
                            tile_data_id = _tile_data_pool.alloc();
                            _tile_id_to_tile_data_id.insert({message.tile_id, tile_data_id});
                        }

                        auto tile_data = _tile_data_pool.get_object(tile_data_id);
                        tile_data->feature_ids = std::move(message.feature_ids);
                    },
                    [&](from_actor::TileAttributes& message)
                    {
                        TileDataId tile_data_id{};

                        auto it = _tile_id_to_tile_data_id.find(message.tile_id);
                        if (it != _tile_id_to_tile_data_id.end())
                        {
                            tile_data_id = it->second;
                        }
                        else
                        {
                            tile_data_id = _tile_data_pool.alloc();
                            _tile_id_to_tile_data_id.insert({message.tile_id, tile_data_id});
                        }

                        auto tile_data = _tile_data_pool.get_object(tile_data_id);
                        tile_data->attributes.push_back(std::move(message.attribute_values));
                    },
                    [&](from_actor::TileFeatureAnchors& message)
                    {
                        TileDataId tile_data_id{};

                        auto it = _tile_id_to_tile_data_id.find(message.tile_id);
                        if (it != _tile_id_to_tile_data_id.end())
                        {
                            tile_data_id = it->second;
                        }
                        else
                        {
                            tile_data_id = _tile_data_pool.alloc();
                            _tile_id_to_tile_data_id.insert({message.tile_id, tile_data_id});
                        }

                        auto tile_data = _tile_data_pool.get_object(tile_data_id);
                        tile_data->anchors = std::move(message.feature_anchors);
                    },
                    [&](from_actor::DiscardTile& message)
                    {
                        auto it = _tile_id_to_tile_data_id.find(message.tile_id);
                        if (it != _tile_id_to_tile_data_id.end())
                        {
                            auto tile_data_id = it->second;
                            _tile_data_pool.release(tile_data_id);
                            _tile_id_to_tile_data_id.erase(it);
                        }
                    },
                    [&](from_actor::RenderRequest& message)
                    { render_request |= message.render_request; },
                    [&](from_actor::NewVisibilitySet& message)
                    { _new_visibility_set = std::move(message.visibility_set); },
                },
                generic_message);
        }

        if (_should_schedule_flat_overlays)
        {
            _should_schedule_flat_overlays = false;
            render_request.schedule_flat_overlay_render();
        }

        if (_force_update_selection
            || (selection::has_changed_since_last_frame(selection)
                && selection::has_changed_since_last_frame(selection, _layer_id)))
        {
            size_t count = selection::selected_objects_count(selection, _layer_id);

            std::vector<vector_data::FeatureIdHash> selected_object_ids(count);
            selection::get_selected_objects(
                selection, _layer_id, std::span<vector_data::FeatureIdHash>(selected_object_ids));

            hrz::flat_hash_set<vector_data::FeatureIdHash> selected_features;
            for (auto id : selected_object_ids)
            {
                // id == 0 represents an empty feature ID. If the layer has no
                // feature IDs, we don't want to select and highlight the whole
                // layer.
                if (id != 0)
                {
                    selected_features.insert(id);
                }
            }

            _actor_channel.send(to_actor::UpdateSelection{std::move(selected_features)});

            _force_update_selection = false;
            render_request.request_visual_render();
        }

        if (_visible_in.any() && _new_visibility_set.has_value())
        {
            render_request.request_visual_render(
                RenderRequest::VisualCause::VectorTileVisibilitySet);
        }

        return render_request;
    }

    bool is_working()
    {
        if (!_visible_in.any())
        {
            return false;
        }

        if (_should_schedule_flat_overlays)
        {
            return true;
        }

        if (_new_visibility_set.has_value())
        {
            return true;
        }

        if (!_visibility_set_frame_queue.empty())
        {
            return true;
        }

        return false;
    }

    void set_bounds(
        const std::optional<hrz::GeoBounds>& bounds,
        std::optional<uint8_t> min_lod,
        std::optional<uint8_t> max_lod)
    {
        _actor_channel.send(to_actor::SetBounds{bounds, min_lod, max_lod});
    }

    void set_clamping(const hrz_proto::VectorClamping& clamping)
    {
        _actor_channel.send(to_actor::SetClamping{clamping});
    }

    void set_max_screen_space_error(unsigned int sse)
    {
        _actor_channel.send(to_actor::SetMaxScreenSpaceError{sse});
    }

    void set_attributes(std::span<const hrz_proto::StylingAttributeRef* const> attribs)
    {
        to_actor::SetAttributes set_attributes;
        set_attributes.attributes.reserve(attribs.size());
        for (auto attrib : attribs)
        {
            set_attributes.attributes.push_back(*attrib);
        }

        _actor_channel.send(std::move(set_attributes));
    }

    void set_palettes(std::span<const hrz_proto::Palette* const> palettes)
    {
        to_actor::SetPalettes set_palettes;
        set_palettes.palettes.reserve(palettes.size());
        for (auto palette : palettes)
        {
            set_palettes.palettes.push_back(*palette);
        }

        _actor_channel.send(std::move(set_palettes));
    }

    // Return [TileData*, feature index]
    std::optional<std::pair<TileData*, uint32_t>> get_feature_data_for_object(
        const picking::ObjectReference& obj)
    {
        auto tile_id = hrz::vt::extract_tile_id(obj);

        auto it = _tile_id_to_tile_data_id.find(tile_id);
        if (it == _tile_id_to_tile_data_id.end())
        {
            HRZ_LOG_WARNING("Tile {} not found", tile_id);
            return std::nullopt;
        }

        auto tile_data = _tile_data_pool.get_object(it->second);
        if (tile_data == nullptr)
        {
            HRZ_LOG_WARNING("Tile {} not found", tile_id);
            return std::nullopt;
        }

        auto feature_index = hrz::vt::extract_feature_index(obj);
        if (feature_index >= tile_data->feature_ids.size())
        {
            HRZ_LOG_WARNING("Invalid feature index for tile {}: {}", tile_id, feature_index);
            return std::nullopt;
        }

        return std::make_pair(tile_data, feature_index);
    }

    std::optional<vector_data::FeatureId> get_feature_id_for_object(
        const picking::ObjectReference& obj)
    {
        if (auto feature_data = get_feature_data_for_object(obj); feature_data.has_value())
        {
            return {feature_data->first->feature_ids.at(feature_data->second)};
        }

        return std::nullopt;
    }

    bool pick_feature(const picking::ObjectReference& obj, hrz_proto::PickLayerResult* result)
    {
        if (auto feature_data = get_feature_data_for_object(obj); feature_data.has_value())
        {
            auto [tile_data, feature_index] = *feature_data;

            auto feature_id = tile_data->feature_ids.at(feature_index);
            if (feature_id.is_null())
            {
                return false;
            }

            auto* vector_result = result->mutable_vector();

            if (tile_data->anchors.blob().is_valid())
            {
                auto anchors_view = tile_data->anchors.get_data_view();
                if (anchors_view.size() > feature_index)
                {
                    lm::dvec3 anchor = anchors_view.at(feature_index);
                    *vector_result->mutable_feature_anchor() =
                        hrz::to_proto(hrz::web_mercator_to_geo3(anchor));
                }
            }

            feature_id.to_proto(vector_result->mutable_feature_id());

            for (const auto& attr : tile_data->attributes)
            {
                if (attr.values.size() > feature_index)
                {
                    vector_result->add_ids(attr.attribute_id);
                    *vector_result->add_values() =
                        vector_data::attr_from<vector_data::ApiAttributeValue>(
                            attr.get_reader().as_ref(feature_index));
                }
            }

            return true;
        }

        return false;
    }

    void set_style_script(std::string_view script)
    {
        _actor_channel.send(to_actor::SetStyleScript{std::string(script.begin(), script.end())});
    }

    void send_repr_channel_to_actor_if_needed(
        hrz_proto::VectorReprType type,
        ReprRegistry* repr_reg)
    {
        // If `_repr_channel_ids` has an entry, it means that the channel has already been
        // sent to the actor.
        if (!_repr_channel_ids.contains(type))
        {
            auto id_and_channel = repr_reg->get(type).create_channel();
            _repr_channel_ids.insert({type, id_and_channel.first});
            _actor_channel.send(to_actor::ReprChannel{
                type, std::move(id_and_channel.second),
                repr_reg->get(type).always_schedule_instantly(),
                repr_reg->get(type).uses_z_coordinates()});
        }
    }

    void add_representation(ReprRegistry* repr_reg, hrz_proto::VectorRepr&& new_repr)
    {
        auto type = new_repr.type();

        _repr_types.insert(type);
        send_repr_channel_to_actor_if_needed(type, repr_reg);

        _actor_channel.send(to_actor::AddRepresentation{std::move(new_repr)});
    }

    void set_all_representations(
        ReprRegistry* repr_reg,
        std::span<const hrz_proto::VectorRepr* const> new_reprs)
    {
        _repr_types.clear();

        to_actor::SetRepresentations set_representations;
        set_representations.reprs.reserve(new_reprs.size());
        for (auto repr : new_reprs)
        {
            auto type = repr->type();

            _repr_types.insert(type);
            send_repr_channel_to_actor_if_needed(type, repr_reg);

            set_representations.reprs.push_back(*repr);
        }

        _actor_channel.send(std::move(set_representations));
    }

    void update_representation(ReprRegistry* repr_reg, size_t index, hrz_proto::VectorRepr&& repr)
    {
        auto type = repr.type();

        _repr_types.insert(type);
        send_repr_channel_to_actor_if_needed(type, repr_reg);

        _actor_channel.send(to_actor::UpdateRepresentation{index, std::move(repr)});
    }

    void remove_representation(ReprRegistry* repr_reg, size_t index)
    {
        _actor_channel.send(to_actor::RemoveRepresentation{index});
    }

    void set_special_attributes(
        std::string_view anchor_z_attribute_name,
        std::string_view anchor_angle_attribute_name,
        std::string_view feature_type_attribute_name)
    {
        _actor_channel.send(to_actor::SetSpecialAttributes{
            std::string(anchor_z_attribute_name.begin(), anchor_z_attribute_name.end()),
            std::string(anchor_angle_attribute_name.begin(), anchor_angle_attribute_name.end()),
            std::string(feature_type_attribute_name.begin(), feature_type_attribute_name.end())});
    }

    void set_clip_id(int32_t clip_id) { _actor_channel.send(to_actor::SetClipId{clip_id}); }

    void set_rng_seed(uint64_t rng_seed) { _actor_channel.send(to_actor::SetRngSeed{rng_seed}); }

    void set_lighting(const render::LightingSettings& settings)
    {
        _actor_channel.send(to_actor::SetLighting{settings});
    }

    void work_gpu(Render* render, std::span<const RenderViewInfo> views_info)
    {
        HRZ_SCOPED_SAMPLE("vector tiles work_gpu");

        if (_visible_in.none() || hrz::get_flag(hrz::Flag::DebugFreezeVectorTilesCulling))
        {
            return;
        }

        hrz::StaticVector<VectorTilesCuller, SCENE_VIEW_COUNT> cullers;

        for (const auto& view_info : views_info)
        {
            VectorTilesCuller culler;
            culler.cam_pos = view_info.cam_view_info.cam.pos;
            culler.view = view_info.view;
            culler.sse = render::ScreenSpaceError(
                view_info.cam_view_info.cam.fovy, (double)view_info.cam_view_info.viewport.size.y,
                view_info.cam_view_info.viewport.device_pixel_ratio);
            culler.frustum_culler = my::FrustumCuller::from_view(
                view_info.cam_view_info.proj, view_info.cam_view_info.cam.view);
            culler.horizon_culler = HorizonCuller(view_info.cam_view_info.cam.pos);

            cullers.push_back(culler);
        }

        if (!_cullers.has_value() || cullers != _cullers.value())
        {
            _cullers = {std::move(cullers)};

            _actor_channel.send(to_actor::GenerateNewVisibilitySet{
                _cullers.value(),
                _request_debug_info_for_debug_draw || _request_debug_info_for_dev_ui});
        }
    }

    void draw(
        Render* render,
        const RenderRequest& render_request,
        ReprRegistry* repr_reg,
        std::span<const RenderViewInfo> views_info,
        SymbolCullingSystem* symbol_culling,
        AttributionRegistry* attributions)
    {
        HRZ_SCOPED_SAMPLE("vector tiles draw");

        if (_visible_in.none()) return;

        if ((render_request.is_visual_render_caused_by(RenderRequest::VisualCause::Scene)
             || render_request.is_visual_render_caused_by(RenderRequest::VisualCause::SymbolCulling)
             || render_request.is_visual_render_caused_by(
                 RenderRequest::VisualCause::VectorTileVisibilitySet))
            && !hrz::get_flag(hrz::Flag::DebugFreezeVectorTilesCulling))
        {
            auto displayable_frame = hrz::Render::CurrentFrame;
            for (auto type : _repr_types)
            {
                displayable_frame =
                    std::min(displayable_frame, repr_reg->get(type).next_displayable_frame());
            }

            auto set_current_visibility_set = [&](VisibilitySet&& set, uint64_t frame)
            {
                if (_current_visibility_set.has_value())
                {
                    _actor_channel.send(
                        to_actor::SignalVisibilitySetDestroyed{_current_visibility_set->id});
                }

                _current_visibility_set = std::move(set);
                _current_visibility_set_frame = frame;

                // A flat overlay render should be scheduled in case the new set contains flat
                // overlay representations.
                _should_schedule_flat_overlays = true;
            };

            // Returns true if we must wait before the visibility set can be drawn.
            auto schedule_visibility_set = [&](VisibilitySet& set)
            {
                if (!set.can_be_scheduled_immediately)
                {
                    bool must_wait = false;

                    for (const auto& repr : set.reprs.get_cdata())
                    {
                        if (!repr_reg->get(repr.type).schedule_draw_soon(
                                _repr_channel_ids[repr.type], repr.id, repr.in_views.bits(),
                                symbol_culling))
                        {
                            must_wait = true;
                        }
                    }
                    return must_wait;
                }
                else
                {
                    return false;
                }
            };

            if (_new_visibility_set.has_value())
            {
                bool visibility_set_must_wait =
                    schedule_visibility_set(_new_visibility_set.value());

                if (visibility_set_must_wait)
                {
                    _visibility_set_frame_queue.emplace_back(
                        std::move(_new_visibility_set.value()), hrz::Render::CurrentFrame);
                }
                else
                {
                    set_current_visibility_set(
                        std::move(_new_visibility_set.value()), hrz::Render::CurrentFrame);
                }

                _new_visibility_set = std::nullopt;
            }
            else
            {
                if (!_visibility_set_frame_queue.empty())
                {
                    schedule_visibility_set(_visibility_set_frame_queue.back().first);
                }
                else if (_current_visibility_set.has_value())
                {
                    schedule_visibility_set(*_current_visibility_set);
                }
            }

            // If a frame newer that the one currently displayed can be displayed, fetch its data
            // from the queue, and yeet old visibility sets.
            if (displayable_frame > _current_visibility_set_frame)
            {
                while (!_visibility_set_frame_queue.empty()
                       && _visibility_set_frame_queue.front().second <= displayable_frame)
                {
                    auto& set = _visibility_set_frame_queue.front();
                    if (set.second <= displayable_frame)
                    {
                        set_current_visibility_set(std::move(set.first), set.second);
                    }
                    _visibility_set_frame_queue.pop_front();
                }
            }

            // Purge old sets
            while (!_visibility_set_frame_queue.empty()
                   && _visibility_set_frame_queue.front().second <= _current_visibility_set_frame)
            {
                _visibility_set_frame_queue.pop_front();
            }

            // Don't grow too large!
            while (_visibility_set_frame_queue.size() > 100)
            {
                _visibility_set_frame_queue.pop_front();
            }
        }

        // Finally we can draw the tiles.
        if (_current_visibility_set.has_value())
        {
            for (const auto& repr : _current_visibility_set->reprs.get_cdata())
            {
                repr_reg->get(repr.type).schedule_draw_now(
                    _repr_channel_ids[repr.type], repr.id, (_visible_in & repr.in_views).bits());
            }

            for (const auto& tile : _current_visibility_set->tiles.get_cdata())
            {
                attribution::use_this_frame(attributions, tile.attribution);

                if (hrz::get_flag(hrz::Flag::DebugDrawVectorTileBounds)
                    && tile.debug_info.has_value())
                {
                    hrz::GeoVolumeBounds tile_bounds(
                        hrz::mercator_tile_bounds(tile.debug_info->coords),
                        tile.debug_info->min_elevation, tile.debug_info->max_elevation);

                    static auto dd = DebugDraw({0, 1, 1, 1});
                    dd.wgs84_box(tile_bounds);
                }

                if (hrz::get_flag(hrz::Flag::DebugDrawHorizonOcclusionPoints)
                    && tile.debug_info.has_value()
                    && tile.debug_info->horizon_occlusion_point.has_value())
                {
                    auto horizon_occlusion_point = tile.debug_info->horizon_occlusion_point.value();

                    auto bounds =
                        GeoVolumeBounds(mercator_tile_bounds(tile.debug_info->coords), 0.0, 0.0);
                    auto center = hrz::geo_to_ecef(
                        {(bounds.south + bounds.north) * 0.5, (bounds.west + bounds.east) * 0.5});

                    auto draw_line = [&](lm::dvec3& p, const lm::vec4& color)
                    {
                        lm::dvec3 points[2] = {center, p};
                        debug_draw::polyline(
                            {(const double*)points, 6}, color, debug_draw::Space::Ecef,
                            debug_draw::Group_Vector);
                    };

                    static constexpr lm::vec4 color = {1, 0, 1, 1};
                    draw_line(horizon_occlusion_point, color);
                    debug_draw::points(
                        horizon_occlusion_point.m, color, debug_draw::Space::Ecef,
                        debug_draw::Group_Vector);
                }
            }
        }

        _request_debug_info_for_debug_draw = hrz::get_flag(hrz::Flag::DebugDrawVectorTileBounds)
            || hrz::get_flag(hrz::Flag::DebugDrawHorizonOcclusionPoints);
    }

    void set_visible_in(SceneViewBitset views)
    {
        _visible_in = views;

        _actor_channel.send(to_actor::SetVisibility{_visible_in});
    }
};

VectorTiles* create(
    uint64_t global_layer_id,
    uint32_t vector_data_layer,
    hrz_proto::MissingTilePolicy missing_tile_policy,
    bool static_tiles,
    uint32_t object_reference_layer_id_partial,
    hrz::PlanetSurface* planet,
    VectorDataLoader* vdl,
    ActorRunner* ar)
{
    assert(vdl);
    return new VectorTiles(
        global_layer_id, vector_data_layer, missing_tile_policy, static_tiles,
        object_reference_layer_id_partial, planet, vdl, ar);
}

void destroy(VectorTiles* vt)
{
    delete vt;
}

RenderRequest work(
    VectorTiles* vt,
    BlobAllocator* ba,
    JobScheduler* js,
    ReprRegistry* repr_reg,
    const SelectionSystem* selection)
{
    return vt->work(ba, js, repr_reg, selection);
}

bool is_working(VectorTiles* vt)
{
    return vt->is_working();
}

void work_gpu(VectorTiles* vt, Render* render, std::span<const RenderViewInfo> views_info)
{
    vt->work_gpu(render, views_info);
}

void draw(
    VectorTiles* vt,
    Render* render,
    const RenderRequest& render_request,
    ReprRegistry* repr_reg,
    std::span<const RenderViewInfo> views_info,
    SymbolCullingSystem* symbol_culling,
    AttributionRegistry* attributions)
{
    vt->draw(render, render_request, repr_reg, views_info, symbol_culling, attributions);
}

void set_bounds(
    VectorTiles* vt,
    const std::optional<hrz::GeoBounds>& bounds,
    std::optional<uint8_t> min_lod,
    std::optional<uint8_t> max_lod)
{
    vt->set_bounds(bounds, min_lod, max_lod);
}

void set_clamping(VectorTiles* vt, const hrz_proto::VectorClamping& clamping)
{
    vt->set_clamping(clamping);
}

void set_attributes(
    VectorTiles* vt,
    std::span<const hrz_proto::StylingAttributeRef* const> attributes)
{
    vt->set_attributes(attributes);
}

void set_max_screen_space_error(VectorTiles* vt, unsigned int sse)
{
    vt->set_max_screen_space_error(sse);
}

void set_palettes(VectorTiles* vt, std::span<const hrz_proto::Palette* const> palettes)
{
    vt->set_palettes(palettes);
}

std::optional<vector_data::FeatureId> get_feature_id_from_object(
    VectorTiles* vt,
    const picking::ObjectReference& obj)
{
    return vt->get_feature_id_for_object(obj);
}

bool pick_feature(
    VectorTiles* vt,
    const picking::ObjectReference& obj,
    hrz_proto::PickLayerResult* result)
{
    return vt->pick_feature(obj, result);
}

void set_style_script(VectorTiles* vt, std::string_view script)
{
    vt->set_style_script(script);
}

void add_representation(VectorTiles* vt, ReprRegistry* repr_reg, hrz_proto::VectorRepr&& repr)
{
    vt->add_representation(repr_reg, std::move(repr));
}

void set_all_representations(
    VectorTiles* vt,
    ReprRegistry* repr_reg,
    std::span<const hrz_proto::VectorRepr* const> reprs)
{
    vt->set_all_representations(repr_reg, reprs);
}

void update_representation(
    VectorTiles* vt,
    ReprRegistry* repr_reg,
    size_t index,
    hrz_proto::VectorRepr&& repr)
{
    vt->update_representation(repr_reg, index, std::move(repr));
}

void remove_representation(VectorTiles* vt, ReprRegistry* repr_reg, size_t index)
{
    vt->remove_representation(repr_reg, index);
}

void set_special_attributes(
    VectorTiles* vt,
    std::string_view anchor_z_attribute_name,
    std::string_view anchor_angle_attribute_name,
    std::string_view feature_type_attribute_name)
{
    vt->set_special_attributes(
        anchor_z_attribute_name, anchor_angle_attribute_name, feature_type_attribute_name);
}

void set_clip_id(VectorTiles* vt, int32_t clip_id)
{
    vt->set_clip_id(clip_id);
}

void set_visible_in(VectorTiles* vt, SceneViewBitset views)
{
    vt->set_visible_in(views);
}

void set_rng_seed(VectorTiles* vt, uint64_t rng_seed)
{
    vt->set_rng_seed(rng_seed);
}

void set_lighting(VectorTiles* vt, const render::LightingSettings& lighting)
{
    vt->set_lighting(lighting);
}

void dev_ui(VectorTiles* vt, mu_Context* ctx)
{
    if (!vt->_current_visibility_set.has_value()) return;

    bool needs_debug_info = false;

    fmt::memory_buffer buffer;

    int layout = -1;
    mu_layout_row(ctx, 1, &layout, 0);

    mu_text(
        ctx,
        hrz::format_to_buffer(buffer, "{} tiles drawn", vt->_current_visibility_set->tiles.size()));

    if (mu_begin_treenode_ex(
            ctx, "Tiles",
            hrz::format_to_buffer(
                buffer, "Drawn tiles ({})", vt->_current_visibility_set->tiles.size()),
            0))
    {
        needs_debug_info = true;

        static const int tile_layout[] = {100, 50, 110, -1};
        mu_layout_row(ctx, 4, tile_layout, 0);

        mu_text(ctx, "Tile coords");
        mu_text(ctx, "Views");
        mu_text(ctx, "Elevation range");
        mu_text(ctx, "Elev. source");

        auto layer_visibility = vt->_visible_in;

        for (const auto& tile : vt->_current_visibility_set->tiles.get_cdata())
        {
            if (tile.debug_info.has_value())
            {
                auto& debug_info = tile.debug_info.value();

                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", debug_info.coords));

                auto tile_visibility = tile.in_views & layer_visibility;
                // Present view visibility bits in left-to-right order.
                uint32_t bits = ((tile_visibility.is_set(0) ? 1 : 0) << 1)
                    | ((tile_visibility.is_set(1) ? 1 : 0) << 0);
                mu_text(ctx, hrz::format_to_buffer(buffer, "{:02b}", bits));

                mu_text(
                    ctx,
                    hrz::format_to_buffer(
                        buffer, "{:.02f}m - {:.02f}m", debug_info.min_elevation,
                        debug_info.max_elevation));
                mu_text(ctx, elevation_source_str(debug_info.elevation_source));
            }
            else
            {
                mu_text(ctx, "<No information>");
                mu_text(ctx, "");
                mu_text(ctx, "");
                mu_text(ctx, "");
            }
        }

        mu_end_treenode(ctx);
    }

    if (needs_debug_info && !vt->_request_debug_info_for_debug_draw
        && !vt->_request_debug_info_for_dev_ui && vt->_cullers.has_value())
    {
        vt->_actor_channel.send(to_actor::GenerateNewVisibilitySet{vt->_cullers.value(), true});
    }

    vt->_request_debug_info_for_dev_ui = needs_debug_info;
}

} // namespace hrz::vt
