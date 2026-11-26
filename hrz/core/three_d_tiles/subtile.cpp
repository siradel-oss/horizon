// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/job_scheduler.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/jobs/three_d_tiles_jobs_params.h"
#include "hrz/core/three_d_tiles/tile_types.h"
#include "hrz/fnd/meta.h"

#include <algorithm>
#include <cassert>
#include <variant>

namespace hrz::three_d_tiles
{

ThreeDTile::Subtile::BatchTablePollResult ThreeDTile::Subtile::poll_batch_table(
    hrz::JobScheduler* js)
{
    // Once decoded, stay decoded: callers check `batch_table_decoded` (or a DECODED result)
    // to run their one-time, post-decode logic only once.
    if (batch_table_decoded) return BatchTablePollResult::DECODED;

    if (!hrz_jobs::is_job_valid(js, decode_batch_table_ticket)
        || !hrz_jobs::is_job_finished(js, decode_batch_table_ticket))
    {
        return BatchTablePollResult::PENDING;
    }

    if (hrz_jobs::get_job_status(js, decode_batch_table_ticket)
        != hrz::job_scheduler::JobStatus::Finished_Success)
    {
        hrz_jobs::cancel_job(js, decode_batch_table_ticket);
        return BatchTablePollResult::ERROR;
    }

    auto response = hrz_jobs::get_job_response(js, decode_batch_table_ticket);

    attribute_values = std::move(response.attribute_values);
    batches_to_feature_ids = std::move(response.batches_to_feature_ids);
    batch_table_decoded = true;

    return BatchTablePollResult::DECODED;
}

hrz::RenderRequest ThreeDTile::Subtile::reset_style()
{
    std::ranges::fill(per_batch_color, lm::ubvec4{0xff});
    has_transparent_batch = false;

    return std::visit(
        hrz::overload{
            [](ContentSubtile auto& c)
            {
                c.reset_style();
                return hrz::RenderRequest::visual();
            },
            [](ThreeDTile::TilesetContent&) { return hrz::RenderRequest{}; },
            [](std::monostate&) { return hrz::RenderRequest{}; }
        },
        content);
}

void ThreeDTile::Subtile::work(const ContentWorkContext& ctx)
{
    std::visit(
        hrz::overload{
            [&](ContentSubtile auto& c) { c.work(ctx); }, [](ThreeDTile::TilesetContent&) {},
            [](std::monostate&) {}
        },
        content);
}

hrz::RenderRequest ThreeDTile::Subtile::work_gpu(
    const ContentWorkContext& ctx,
    const ContentRenderContext& render_ctx,
    const ThreeDTile& tile,
    const TilesetConfig* tileset_config)
{
    return std::visit(
        hrz::overload{
            [&](ContentSubtile auto& c)
            { return c.work_gpu(ctx, render_ctx, tile, *this, tileset_config); },
            [](ThreeDTile::TilesetContent&) { return hrz::RenderRequest{}; },
            [](std::monostate&) { return hrz::RenderRequest{}; }
        },
        content);
}

#if HRZ_DEBUG
static bool is_allowed_styling_status_transition(
    ThreeDTile::StylingStatus from,
    ThreeDTile::StylingStatus to)
{
    using Status = ThreeDTile::StylingStatus;

    if (from == to) return true;

    switch (to)
    {
        case Status::IDLE: return from == Status::WAITING_FOR_RESOURCE_UPDATE;
        case Status::WAITING_FOR_ATTRIBUTES: return from == Status::IDLE;
        case Status::WAITING_FOR_JOB:
            return from == Status::IDLE || from == Status::WAITING_FOR_ATTRIBUTES;
        case Status::WAITING_FOR_RESOURCE_UPDATE:
            return from == Status::IDLE || from == Status::WAITING_FOR_ATTRIBUTES
                || from == Status::WAITING_FOR_JOB;
    }

    return false;
}
#endif

void ThreeDTile::Subtile::set_styling_status(StylingStatus status)
{
#if HRZ_DEBUG
    assert(is_allowed_styling_status_transition(_styling_status, status));
#endif

    _styling_status = status;
}

void ThreeDTile::Subtile::reset_styling_status(hrz::JobScheduler* js)
{
    if (_styling_status == StylingStatus::WAITING_FOR_JOB)
    {
        assert(hrz_jobs::is_job_valid(js, style_job_ticket));
    }

    hrz_jobs::cancel_job(js, style_job_ticket);
    _styling_status = StylingStatus::IDLE;
}

hrz::RenderRequest ThreeDTile::Subtile::upload_style_data()
{
    return std::visit(
        hrz::overload{
            [&](ContentSubtile auto& c)
            {
                if (_styling_status != StylingStatus::WAITING_FOR_RESOURCE_UPDATE)
                {
                    return hrz::RenderRequest{};
                }

                c.update_feature_colors(per_batch_color, has_transparent_batch);
                has_been_styled_once = true;
                set_styling_status(StylingStatus::IDLE);
                return hrz::RenderRequest::visual();
            },
            [](ThreeDTile::TilesetContent&)
            {
                assert(false && "Only styleable content can leave the IDLE styling status");
                return hrz::RenderRequest{};
            },
            [](std::monostate&)
            {
                assert(false && "Only styleable content can leave the IDLE styling status");
                return hrz::RenderRequest{};
            }
        },
        content);
}

bool ThreeDTile::Subtile::store_vector_data_attributes(
    std::span<const hrz::three_d_tiles::AttributeConfig> attributes,
    std::span<hrz::vector_data::AttributeValues> values,
    hrz::AttributionHandle attribution)
{
    for (unsigned int attribute_index = 0; attribute_index < attributes.size(); ++attribute_index)
    {
        const auto& attribute = attributes[attribute_index];

        if (!attribute.has_vector_data_layer_source() || attribute_values.size() <= attribute_index)
        {
            continue;
        }

        bool values_found = false;

        for (auto& value : values)
        {
            if (value.attribute_id == attribute.attribute_id)
            {
                attribute_values.at(attribute_index) = std::move(value);
                values_found = true;
                break;
            }
        }

        attribute_attributions = attribution;

        if (!values_found) return false;
    }

    return true;
}

void ThreeDTile::Subtile::clear_vector_data_attributes(
    std::span<const hrz::three_d_tiles::AttributeConfig> attributes)
{
    for (unsigned int attribute_index = 0; attribute_index < attributes.size(); ++attribute_index)
    {
        if (attributes[attribute_index].has_vector_data_layer_source()
            && attribute_values.size() > attribute_index)
        {
            attribute_values.at(attribute_index) = std::nullopt;
            attribute_attributions = {};
        }
    }
}

} // namespace hrz::three_d_tiles
