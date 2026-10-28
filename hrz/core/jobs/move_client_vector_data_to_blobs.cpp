// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/blob_array.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/profiling.h"
#include "hrz/common/vector_data/attribute_type_api.h" // IWYU pragma: keep
#include "hrz/common/vector_data/geometry_utils.h"
#include "hrz/common/vector_data/packed_attribute_values_builder.h"
#include "hrz/common/vector_data/tile_geometry.h"
#include "hrz/common/vector_data/tile_geometry_builder.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_data_jobs_params.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/log.h"
#include "hrz/protocol/services.h"
#include "hrz/protocol/vector/geometry.pb.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

#include <span>

// This job takes Protobuf data from a client request response, changes
// its format to the engines's internal structures for vector data, which
// includes allocating blobs and storing data in them.
//
// On top of this, this job also ensures there is a consistent number of
// geometries and attribute values, adding empty geometries and null values
// if needed.

namespace hrz_jobs::move_client_vector_data_to_blobs
{

namespace
{

[[nodiscard]]
bool move_geometry_to_blobs(
    const hrz_proto::VectorFeatureGeometry& geometry,
    hrz::vector_data::VectorTileGeometryBuilder& builder)
{
    hrz::vector_data::LinestringSizes linestring_sizes;
    if (!hrz::vector_data::normalize_linestring_sizes(
            geometry.type(), (uint32_t)(geometry.coords_size() / 3),
            std::span<const uint32_t>(
                geometry.linestring_sizes().data(), (size_t)geometry.linestring_sizes_size()),
            &linestring_sizes))
    {
        HRZ_LOG_WARNING(
            "Client-provided vector data has fewer points than expected by the linestring "
            "sizes. ");
        return builder.add_geometry_collection([&](auto&) { return true; });
    }

    if (geometry.type() == hrz_proto::POINT_GEOMETRY)
    {
        return builder.add_points_feature(
            [&](hrz::vector_data::VectorTileGeometryBuilder::PointsBuilder& points_builder) -> bool
            {
                auto point_count = (size_t)geometry.coords_size() / 3;
                for (size_t i = 0; i < point_count; i++)
                {
                    lm::dvec3 point = {
                        geometry.coords(i * 3 + 0), geometry.coords(i * 3 + 1),
                        geometry.coords(i * 3 + 2)
                    };
                    points_builder.add_point(point);
                }
                return true;
            });
    }
    else if (geometry.type() == hrz_proto::POLYLINE_GEOMETRY)
    {
        return builder.add_polylines_feature(
            [&](hrz::vector_data::VectorTileGeometryBuilder::PolylinesBuilder& polylines_builder)
                -> bool
            {
                size_t first_linestring_point = 0;
                for (const auto linestring_size : linestring_sizes)
                {
                    for (size_t i = first_linestring_point;
                         i < first_linestring_point + linestring_size; i++)
                    {
                        lm::dvec3 point = {
                            geometry.coords(i * 3 + 0), geometry.coords(i * 3 + 1),
                            geometry.coords(i * 3 + 2)
                        };
                        polylines_builder.add_point(point);
                    }
                    if (!polylines_builder.end_linestring()) return false;
                    first_linestring_point += linestring_size;
                }
                return true;
            });
    }
    else if (geometry.type() == hrz_proto::POLYGON_GEOMETRY)
    {
        return builder.add_polygons_feature(
            [&](hrz::vector_data::VectorTileGeometryBuilder::PolygonsBuilder& polygons_builder)
                -> bool
            {
                size_t first_linestring_point = 0;
                for (const auto linestring_size : linestring_sizes)
                {
                    for (size_t i = first_linestring_point;
                         i < first_linestring_point + linestring_size; i++)
                    {
                        lm::dvec3 point = {
                            geometry.coords(i * 3 + 0), geometry.coords(i * 3 + 1),
                            geometry.coords(i * 3 + 2)
                        };
                        polygons_builder.add_point(point);
                    }
                    if (!polygons_builder.end_ring()) return false;
                    first_linestring_point += linestring_size;
                }
                return polygons_builder.end_polygon();
            });
    }
    else
    {
        HRZ_LOG_WARNING(
            "Client-provided vector data has an unknown geometry type: {}", (int)geometry.type());
        return builder.add_geometry_collection([&](auto&) { return true; });
    }
}

[[nodiscard]]
bool move_geometry_to_blobs(
    const hrz_proto::VectorDataRequestResponse& client_response,
    hrz::vector_data::VectorTileGeometryBuilder& builder)
{
    for (const auto& proto_feature : client_response.features())
    {
        if (proto_feature.geometry_size() == 0)
        {
            if (!builder.add_geometry_collection([&](auto&) { return true; }))
            {
                return false;
            }
        }
        else if (proto_feature.geometry_size() == 1)
        {
            if (!move_geometry_to_blobs(proto_feature.geometry(0), builder))
            {
                return false;
            }
        }
        else
        {
            if (!builder.add_geometry_collection(
                    [&](hrz::vector_data::VectorTileGeometryBuilder& collection_builder)
                    {
                        for (const auto& proto_geometry : proto_feature.geometry())
                        {
                            if (!move_geometry_to_blobs(proto_geometry, collection_builder))
                            {
                                return false;
                            }
                        }
                        return true;
                    }))
            {
                return false;
            }
        }
    }

    return true;
}

} // anonymous namespace

hrz_jobs::JobResult run(
    const hrz_jobs::RawClientVectorData& params,
    hrz::vector_data::DecodedVectorTile& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("move client tile to blobs job");

    const auto& client_response = params.client_data;

    size_t point_capacity = 0;
    size_t linestring_sizes_capacity = 0;
    size_t feature_capacity = params.expects_geometry ? client_response.features_size() : 0;

    for (const auto& feature : client_response.features())
    {
        if (params.expects_geometry)
        {
            for (const auto& geometry : feature.geometry())
            {
                point_capacity += geometry.coords_size() / 3;
                linestring_sizes_capacity += geometry.linestring_sizes_size();
            }
        }
        else if (feature.geometry_size() > 0)
        {
            HRZ_LOG_WARNING(
                "Unexpected geometry in client-provided vector data, it will be ignored.");
            break;
        }
    }

    auto builder = hrz::vector_data::VectorTileGeometryBuilder(
        context.get_blob_allocator(), feature_capacity, point_capacity, linestring_sizes_capacity);
    builder.register_blobs_owner(context.get_resource_owner());

    if (params.expects_geometry)
    {
        if (!move_geometry_to_blobs(client_response, builder))
        {
            HRZ_LOG_ERROR("Failed to move client-provided vector data geometry to blobs.");
            return hrz_jobs::JobResult::FAILURE;
        }
    }

    auto tile_geometry_opt = std::move(builder).finalize();
    if (!tile_geometry_opt.has_value())
    {
        HRZ_LOG_ERROR("Failed to finalize vector tile geometry builder.");
        return hrz_jobs::JobResult::FAILURE;
    }
    response.geometry = std::move(tile_geometry_opt.value());

    // Get the attributes and feature ids
    uint32_t feature_id_attribute_count = 0;

    // Why not use a map of MutableAttributeValues?
    // Because their order matter, as we will want to extract the first few of them
    // in a span later.
    hrz::flat_hash_map<uint32_t, uint32_t> attribute_id_to_index;
    std::vector<hrz::vector_data::PackedAttributeValuesBuilder> attribute_index_to_values;
    std::vector<uint32_t> attribute_id;

    const auto emplace_back_attribute_values = [&](uint32_t id)
    {
        attribute_id_to_index.insert({id, (uint32_t)attribute_index_to_values.size()});
        attribute_index_to_values.emplace_back(
            client_response.features_size(), context.get_blob_allocator(),
            context.get_resource_owner());
        attribute_id.push_back(id);
    };

    for (const auto& attribute : params.attributes)
    {
        // First pass: all of the feature id attributes go to the front,
        // so that we can extract them in a span later
        if (attribute.is_feature_id)
        {
            feature_id_attribute_count++;
            emplace_back_attribute_values(attribute.id);
        }
    }

    for (const auto& attribute : params.attributes)
    {
        // All non feature id attributes go to the back
        if (!attribute.is_feature_id)
        {
            emplace_back_attribute_values(attribute.id);
        }
    }

    for (size_t i = 0; i < params.attributes.size(); i++)
    {
        const auto& model_attribute = params.attributes[i];

        const auto index = attribute_id_to_index.at(model_attribute.id);
        auto& values = attribute_index_to_values.at(index);

        bool has_warned_about_missing_attribute = false;

        for (const auto& feature : client_response.features())
        {
            if (i >= (size_t)feature.attribute_values_size())
            {
                if (!has_warned_about_missing_attribute)
                {
                    HRZ_LOG_WARNING(
                        "Missing attribute {} value for client-provided feature: got {} values, "
                        "expected {}. A default value will be used instead.",
                        model_attribute.id, feature.attribute_values_size(),
                        params.attributes.size());
                    has_warned_about_missing_attribute = true;
                }

                values.push_null();
            }
            else
            {
                const auto& feature_attribute_value = feature.attribute_values(i);
                values.push_transform(
                    model_attribute.transform,
                    hrz::vector_data::attr_as_ref(feature_attribute_value));
            }
        }
    }

    std::vector<hrz::vector_data::AttributeValues> attribute_index_to_values_finalized;
    attribute_index_to_values_finalized.reserve(attribute_index_to_values.size());
    for (size_t i = 0; i < attribute_id.size(); ++i)
    {
        auto& values = attribute_index_to_values[i];
        auto values_finalized = values.finalize(attribute_id[i]);
        if (values_finalized.has_value())
        {
            attribute_index_to_values_finalized.push_back(values_finalized.value());
        }
        else
        {
            attribute_index_to_values_finalized.push_back({});
        }
    }

    const std::span<hrz::vector_data::AttributeValues> values_finalized_span(
        attribute_index_to_values_finalized);

    hrz::BlobVector<hrz::vector_data::FeatureIdHash> hashes_vector(
        context.get_blob_allocator(), client_response.features_size());
    hashes_vector.resize(client_response.features_size());
    hashes_vector.register_blob_metadata("contents"_ss, "client feature ID hashes"_ss);
    hashes_vector.register_blob_owner(context.get_resource_owner());

    auto hashes_array_opt = hashes_vector.to_blob_array();
    if (!hashes_array_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    auto feature_ids_opt = hrz::vector_data::FeatureIds::make(
        values_finalized_span.subspan(0, feature_id_attribute_count), hashes_array_opt.value());

    if (!feature_ids_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    response.feature_ids = feature_ids_opt.value();
    response.attributes.reserve(attribute_id_to_index.size());

    // We want to return the attribute values in order in which their definition was provided to us.
    for (const auto& attribute_model : params.attributes)
    {
        const auto index = attribute_id_to_index.at(attribute_model.id);
        response.attributes.push_back(std::move(attribute_index_to_values_finalized.at(index)));
    }

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace hrz_jobs::move_client_vector_data_to_blobs
