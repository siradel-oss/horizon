// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/jobs/styling.h"

#include "hrz/common/monitoring_defs.h"
#include "hrz/common/palette.h"
#include "hrz/common/style/flat_ast.h"
#include "hrz/common/three_d_tiles.h"
#include "hrz/common/vector_data/packed_attribute_values.h"
#include "hrz/core/job_scheduler.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/style/script.h"
#include "hrz/core/three_d_tiles/model_cache.h"
#include "hrz/core/three_d_tiles/system.h"
#include "hrz/core/three_d_tiles/tile_types.h"
#include "hrz/core/vector/data_loader/data_loader.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/meta.h"

#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace hrz::three_d_tiles
{

namespace
{

constexpr uint64_t COLOR_PRP = 0;
constexpr uint32_t TILE_REPR_ID = 0;
constexpr const char* TILE_REPR_NAME = "tile";

bool needs_attribute_values(const TilesetConfig& config, const ThreeDTile::Subtile& subtile)
{
    return config.has_vector_data_layer_attributes && !subtile.batches_to_feature_ids.empty();
}

hrz::RenderRequest apply_default_style(ThreeDTile::Subtile& subtile)
{
    subtile.set_styling_status(ThreeDTile::StylingStatus::WAITING_FOR_RESOURCE_UPDATE);
    return subtile.reset_style();
}

hrz::RenderRequest retrieve_style_data_from_job(ThreeDTile::Subtile& subtile, hrz::JobScheduler* js)
{
    const auto result = hrz_jobs::get_job_response(js, subtile.style_job_ticket);

    assert(subtile.has_pnts() || subtile.has_b3dm() || subtile.has_i3dm());

    if (subtile.per_batch_color.empty()) return {};

    // If a feature has been emitted, it will get
    // its colour down below. So by initialising
    // all the colours to 0, features that have not
    // been emitted are made invisible.
    std::ranges::fill(subtile.per_batch_color, lm::ubvec4{0});
    subtile.has_transparent_batch = false;

    const auto result_prps = result.features.prps.get_data();
    const auto result_values = result.features.get_values_reader();

    for (const auto& instance : result.features.instances.get_data())
    {
        if (instance.repr_id == TILE_REPR_ID)
        {
            const auto batch_id = instance.feature_index;

            lm::ubvec4 color = {0xff, 0xff, 0xff, 0xff};

            for (uint64_t prp_index = instance.first_prp;
                 prp_index < instance.first_prp + instance.prp_count; ++prp_index)
            {
                if (result_prps[prp_index] == COLOR_PRP)
                {
                    color = result_values.as_color(prp_index);
                }
            }

            if (color.a > 0 && color.a < 0xff)
            {
                subtile.has_transparent_batch = true;
            }

            subtile.per_batch_color[batch_id] = color;
        }
    }

    return hrz::RenderRequest::visual();
}

} // anonymous namespace

void ThreeDTilesSystem::set_vector_data_layer_id(TilesetH handle, uint32_t vector_data_layer_id)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    auto config = tileset->get_mutable_config();

    config->vector_data_layer_id = vector_data_layer_id;

    if (tileset->status == Tileset::Status::LOADED)
    {
        config->renew_vector_data_layer_request = true;
    }

    trigger_restyling(tileset, true);
}

void ThreeDTilesSystem::set_style(
    TilesetH handle,
    std::string_view style_script,
    std::span<const hrz_proto::Palette* const> palettes_proto,
    uint64_t rng_seed)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    auto config = tileset->get_mutable_config();

    config->style_ast = std::make_shared<hrz::style::FlatAst>();

    config->style_palettes.clear();
    config->style_palettes.reserve(palettes_proto.size());
    for (auto proto : palettes_proto)
    {
        auto palette = hrz::palette::from_proto(*proto);
        config->style_palettes.push_back(std::move(palette));
    }

    config->rng_seed = rng_seed;

    bool style_is_valid = false;

    if (!style_script.empty())
    {
        style_is_valid = true;

        const auto lexer = hrz::style::Lexer::create(style_script);
        const auto parser = hrz::style::Parser::create();

        for (auto& attribute : config->attributes)
        {
            const auto style_id = parser->add_attribute(attribute.name);

            if (style_id == hrz::style::Parser::INSERT_ERROR)
            {
                HRZ_LOG_WARNING("Couldn't register attribute \"{}\".", attribute.name);
                style_is_valid = false;
            }

            attribute.style_id = style_id;
        }

        parser->add_property(COLOR_PRP, "color");

        _tile_depth_uniform_id = (uint32_t)parser->add_uniform("tile_depth");

        for (const auto& palette : config->style_palettes)
        {
            parser->add_palette(palette.name.c_str());
        }

        hrz::style::Ast full_ast;
        auto parse_result = parser->parse(*lexer, full_ast);
        if (!parse_result)
        {
            HRZ_LOG_ERROR(
                "Styling script parsing error {} ({}) at line {}", parse_result.description(),
                fmt::underlying(parse_result.type), parse_result.line);
            config->style_ast = nullptr;
            style_is_valid = false;
        }
        else
        {
            const auto optimizer = hrz::style::Optimizer::create(config->style_palettes);
            if (!optimizer->optimize(std::move(full_ast), config->style_ast.get()))
            {
                HRZ_LOG_ERROR("Styling script optimization error");
                config->style_ast = nullptr;
                style_is_valid = false;
            }
        }
    }

    config->has_style = style_is_valid;

    trigger_restyling(tileset, false);
}

void ThreeDTilesSystem::trigger_restyling(Tileset* tileset, bool reload_attributes)
{
    for (auto& tile : tileset->tiles)
    {
        for (auto& subtile : tile.subtiles)
        {
            subtile.needs_restyling = true;
            subtile.needs_attribute_reload |= reload_attributes;

            if (subtile.has_external_tileset())
            {
                const auto& content = std::get<ThreeDTile::TilesetContent>(subtile.content);
                if (content.tileset_handle != 0)
                {
                    auto external_tileset = _tilesets.get_object(content.tileset_handle);
                    trigger_restyling(external_tileset, reload_attributes);
                }
            }
        }
    }
}

hrz::RenderRequest ThreeDTilesSystem::work_waiting_for_attributes(
    const TilesetConfig& config,
    const ThreeDTile& tile,
    ThreeDTile::Subtile& subtile,
    hrz::JobScheduler* js)
{
    using AttributeStatus = ThreeDTile::Subtile::VectorDataAttributeStatus;

    switch (subtile.vector_data_attribute_status)
    {
        case AttributeStatus::UNREQUESTED:
        {
            if (config.vector_data_layer_status == TilesetConfig::VectorDataLayerStatus::LOADED)
            {
                _vector_data_channel.send(
                    hrz::vector_data::messages::RequestData{
                        subtile.vector_data_attribute_request_id,
                        config.vector_data_layer_request_id, subtile.batches_to_feature_ids,
                        hrz::vector_data::DataKind::AttributeValues
                    });
                subtile.vector_data_attribute_status = AttributeStatus::LOADING;
            }
            else if (config.vector_data_layer_status == TilesetConfig::VectorDataLayerStatus::ERROR)
            {
                subtile.clear_vector_data_attributes(config.attributes);
                return apply_default_style(subtile);
            }
            return {};
        }
        case AttributeStatus::LOADING:
        {
            return {};
        }
        case AttributeStatus::LOADED:
        {
            return start_style_job(config, tile, subtile, js);
        }
        case AttributeStatus::ERROR:
        {
            subtile.clear_vector_data_attributes(config.attributes);
            return apply_default_style(subtile);
        }
    }

    return {};
}

void ThreeDTilesSystem::release_attribute_request(ThreeDTile::Subtile& subtile)
{
    if (subtile.vector_data_attribute_status
        == ThreeDTile::Subtile::VectorDataAttributeStatus::UNREQUESTED)
    {
        return;
    }

    _vector_data_channel.send(
        hrz::vector_data::messages::ReleaseDataRequest{subtile.vector_data_attribute_request_id});
    subtile.vector_data_attribute_status =
        ThreeDTile::Subtile::VectorDataAttributeStatus::UNREQUESTED;
}

void ThreeDTilesSystem::cancel_attribute_request(ThreeDTile::Subtile& subtile)
{
    if (subtile.styling_status() != ThreeDTile::StylingStatus::WAITING_FOR_ATTRIBUTES) return;

    release_attribute_request(subtile);
}

hrz::RenderRequest ThreeDTilesSystem::start_style_job(
    const TilesetConfig& config,
    const ThreeDTile& tile,
    ThreeDTile::Subtile& subtile,
    hrz::JobScheduler* js)
{
    assert(!hrz_jobs::is_job_valid(js, subtile.style_job_ticket));

    // Missing attributes can happen for example if not all of
    // them have been loaded from the tile data.
    if (subtile.attribute_values.size() != config.attributes.size())
    {
        return apply_default_style(subtile);
    }

    hrz_jobs::FeaturesStylingData job_data;

    job_data.ast = config.style_ast;
    job_data.representations.emplace_back(TILE_REPR_ID, TILE_REPR_NAME);
    job_data.attributes.reserve(config.attributes.size());

    // Tiles without a batch table are stylable, but they have
    // no feature ids and attribute values.
    // In order to execute the styling script and have a computed
    // style for the tile, we pretend that there is one feature,
    // encompassing the whole tile.
    // Dummy attribute values have been generated in the batch
    // table decoding job.
    job_data.feature_count = std::max(subtile.batch_length, (uint32_t)1);

    for (unsigned int i = 0; i < config.attributes.size(); ++i)
    {
        if (!subtile.attribute_values.at(i).has_value()) return apply_default_style(subtile);

        const auto& attribute = config.attributes.at(i);
        const auto& attribute_values = subtile.attribute_values.at(i).value();

        job_data.attributes.emplace(attribute.style_id, attribute_values);
        if (!attribute_values.out_of_line_data.empty())
        {
            job_data.attributes[attribute.style_id].out_of_line_data =
                attribute_values.out_of_line_data;
        }
    }

    const bool has_feature_ids = subtile.batches_to_feature_ids.has_any_attribute();
    if (!has_feature_ids)
    {
        if (tile.range)
        {
            job_data.rng_seed = hrz::hash_values_static(
                tile.uri, tile.range->offset, tile.range->length, config.rng_seed);
        }
        else
        {
            job_data.rng_seed = hrz::hash_values_static(tile.uri, config.rng_seed);
        }
    }
    else
    {
        job_data.rng_seed = config.rng_seed;
        job_data.feature_ids_hashes = subtile.batches_to_feature_ids.hashes();
    }

    job_data.uniforms.emplace(
        _tile_depth_uniform_id,
        hrz::vector_data::attr_from<hrz::vector_data::OwnedAttributeValue>(tile.depth));

    for (const auto& palette : config.style_palettes)
    {
        job_data.palettes.push_back(palette);
    }

    subtile.style_job_ticket = hrz_jobs::add_job_style_features(
        js, std::move(job_data),
        {hrz::monitoring::systems::ThreeDTilesLayers, config.global_layer_id});

    subtile.set_styling_status(ThreeDTile::StylingStatus::WAITING_FOR_JOB);
    return {};
}

hrz::RenderRequest ThreeDTilesSystem::start_styling_process(
    const TilesetConfig& config,
    ThreeDTile& tile,
    ThreeDTile::Subtile& subtile,
    bool reload_attribute_values,
    const TilesetWorkContext& ctx)
{
    subtile.needs_restyling = false;
    subtile.needs_attribute_reload = false;

    subtile.reset_styling_status(ctx.js);

    if (!config.has_style) return apply_default_style(subtile);

    if (!needs_attribute_values(config, subtile))
    {
        return start_style_job(config, tile, subtile, ctx.js);
    }

    if (reload_attribute_values) release_attribute_request(subtile);

    subtile.set_styling_status(ThreeDTile::StylingStatus::WAITING_FOR_ATTRIBUTES);
    return work_waiting_for_attributes(config, tile, subtile, ctx.js);
}

hrz::RenderRequest ThreeDTilesSystem::work_subtile_styling(
    const Tileset* tileset,
    ThreeDTile& tile,
    ThreeDTile::Subtile& subtile,
    const TilesetWorkContext& ctx)
{
    hrz::RenderRequest render_request;

    assert(subtile.can_be_styled());

    if ((subtile.styling_status() == ThreeDTile::StylingStatus::IDLE && !subtile.is_styled())
        || subtile.needs_restyling)
    {
        // The tile fulfills the prerequisites for styling, and either:
        //  * has no style and is not currently being styled, or,
        //  * a restyling has been requested.
        // So let's start or restart the styling process.

        render_request |= start_styling_process(
            *tileset->config, tile, subtile, subtile.needs_attribute_reload, ctx);
    }
    else if (subtile.styling_status() == ThreeDTile::StylingStatus::WAITING_FOR_ATTRIBUTES)
    {
        render_request |= work_waiting_for_attributes(*tileset->config, tile, subtile, ctx.js);
    }
    else if (subtile.styling_status() == ThreeDTile::StylingStatus::WAITING_FOR_JOB)
    {
        assert(hrz_jobs::is_job_valid(ctx.js, subtile.style_job_ticket));

        if (hrz_jobs::is_job_finished(ctx.js, subtile.style_job_ticket))
        {
            if (hrz_jobs::get_job_status(ctx.js, subtile.style_job_ticket)
                == hrz::job_scheduler::JobStatus::Finished_Success)
            {
                retrieve_style_data_from_job(subtile, ctx.js);
            }
            else
            {
                HRZ_LOG_ERROR("Styling job error for tile \"{}\"", tile.uri);
                subtile.reset_style();
                hrz_jobs::cancel_job(ctx.js, subtile.style_job_ticket);
            }

            subtile.set_styling_status(ThreeDTile::StylingStatus::WAITING_FOR_RESOURCE_UPDATE);
        }
    }

    return render_request;
}

Tileset* ThreeDTilesSystem::find_tileset_for_layer_request(uint64_t vector_data_layer_request_id)
{
    return _tilesets.get_object(
        VectorDataRequestId::tileset_handle_of(vector_data_layer_request_id));
}

ThreeDTilesSystem::AttributeRequestTarget ThreeDTilesSystem::find_subtile_for_attribute_request(
    uint64_t attribute_request_id)
{
    const auto id = VectorDataRequestId::decode(attribute_request_id);
    auto tileset = _tilesets.get_object(id.tileset_handle);

    if (tileset == nullptr) return {};

    if (id.tile_index >= tileset->tiles.size()) return {};

    auto& tile = tileset->tiles.at(id.tile_index);

    if (id.subtile_index >= tile.subtiles.size()) return {};

    return {tileset, &tile, &tile.subtiles.at(id.subtile_index)};
}

void ThreeDTilesSystem::apply_attribute_data(
    Tileset& tileset,
    ThreeDTile& tile,
    ThreeDTile::Subtile& subtile,
    hrz::vector_data::messages::DataUpdate& message,
    hrz::JobScheduler* js)
{
    const auto& attributes = tileset.config->attributes;

    auto* all_values =
        std::get_if<hrz::InlinedVector<hrz::vector_data::AttributeValues, 16>>(&message.data);
    assert(all_values != nullptr);
    const auto values = all_values != nullptr
        ? std::span<hrz::vector_data::AttributeValues>(all_values->data(), all_values->size())
        : std::span<hrz::vector_data::AttributeValues>();

    if (subtile.store_vector_data_attributes(attributes, values, message.attribution))
    {
        subtile.vector_data_attribute_status =
            ThreeDTile::Subtile::VectorDataAttributeStatus::LOADED;
    }
    else
    {
        HRZ_LOG_ERROR("Attribute request error for tile \"{}\"", tile.uri);
        subtile.vector_data_attribute_status =
            ThreeDTile::Subtile::VectorDataAttributeStatus::ERROR;
    }

    on_attribute_reply(tileset, tile, subtile, js);
}

void ThreeDTilesSystem::on_attribute_reply(
    Tileset& tileset,
    ThreeDTile& tile,
    ThreeDTile::Subtile& subtile,
    hrz::JobScheduler* js)
{
    // A reply for a subtile that was not waiting for it (e.g. after a vector
    // data layer update) must be applied by restyling it.
    if (subtile.styling_status() != ThreeDTile::StylingStatus::WAITING_FOR_ATTRIBUTES)
    {
        subtile.needs_restyling = true;
        return;
    }

    work_waiting_for_attributes(*tileset.config, tile, subtile, js);
}

void ThreeDTilesSystem::work_attribute_messages(const WorkContext& ctx)
{
    for (auto& generic_message : _vector_data_channel.receive())
    {
        namespace messages = hrz::vector_data::messages;
        std::visit(
            hrz::overload{
                [&](messages::LayerModelUpdate& message)
                {
                    auto tileset = find_tileset_for_layer_request(message.request_id);
                    if (tileset != nullptr)
                    {
                        tileset->get_mutable_config()->vector_data_layer_status =
                            TilesetConfig::VectorDataLayerStatus::LOADED;
                        trigger_restyling(tileset, true);
                    }
                },
                [&](messages::LayerModelError& message)
                {
                    auto tileset = find_tileset_for_layer_request(message.request_id);
                    if (tileset != nullptr)
                    {
                        tileset->get_mutable_config()->vector_data_layer_status =
                            TilesetConfig::VectorDataLayerStatus::ERROR;
                        trigger_restyling(tileset, true);
                    }
                },
                [&](const messages::LayerNewData&)
                {
                    // No-op
                },
                [&](messages::DataUpdate& message)
                {
                    const auto target = find_subtile_for_attribute_request(message.request_id);
                    if (target.subtile != nullptr)
                    {
                        apply_attribute_data(
                            *target.tileset, *target.tile, *target.subtile, message, ctx.js);
                    }
                },
                [&](messages::DataError& message)
                {
                    const auto target = find_subtile_for_attribute_request(message.request_id);
                    if (target.subtile != nullptr)
                    {
                        target.subtile->vector_data_attribute_status =
                            ThreeDTile::Subtile::VectorDataAttributeStatus::ERROR;
                        on_attribute_reply(*target.tileset, *target.tile, *target.subtile, ctx.js);
                    }
                }
            },
            generic_message);
    }
}

hrz::RenderRequest ThreeDTilesSystem::work_tile_styling(
    const Tileset* tileset,
    ThreeDTile& tile,
    const TilesetWorkContext& ctx)
{
    hrz::RenderRequest render_request;

    if (tile.load_status() == ThreeDTile::LoadStatus::LOADED
        || tile.load_status() == ThreeDTile::LoadStatus::RENDERABLE)
    {
        for (auto& subtile : tile.subtiles)
        {
            if (!subtile.can_be_styled()) continue;
            render_request |= work_subtile_styling(tileset, tile, subtile, ctx);
        }
    }

    return render_request;
}

} // namespace hrz::three_d_tiles
