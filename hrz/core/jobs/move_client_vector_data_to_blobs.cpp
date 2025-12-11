#include "hrz/common/blob_array.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/profiling.h"
#include "hrz/common/vector_data/attribute_type_api.h" // IWYU pragma: keep
#include "hrz/common/vector_data/geometry_utils.h"
#include "hrz/common/vector_data/packed_attribute_values_builder.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_data_jobs_params.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/log.h"

// This job takes Protobuf data from a client request response, changes
// its format to the engines's internal structures for vector data, which
// includes allocating blobs and storing data in them.
//
// On top of this, this job also ensures there is a consistent number of
// geometries and attribute values, adding empty geometries and null values
// if needed.

namespace hrz_jobs::move_client_vector_data_to_blobs
{
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

    if (params.expects_geometry)
    {
        for (const auto& feature : client_response.features())
        {
            if (feature.has_geometry())
            {
                point_capacity += feature.geometry().coords_size() / 3;
                linestring_sizes_capacity += feature.geometry().linestring_sizes_size();
            }
        }
    }

    auto points = hrz::BlobVector<lm::dvec3>(context.get_blob_allocator(), point_capacity);
    auto linestring_sizes =
        hrz::BlobVector<uint32_t>(context.get_blob_allocator(), linestring_sizes_capacity);
    auto features = hrz::BlobVector<hrz::vector_data::VectorTileGeometry::Feature>(
        context.get_blob_allocator(), feature_capacity);

    points.register_blob_metadata("contents"_ss, "client vector geometry points"_ss);
    linestring_sizes.register_blob_metadata(
        "contents"_ss, "client vector geometry linestring sizes"_ss);
    features.register_blob_metadata("contents"_ss, "client vector geometry features"_ss);

    points.register_blob_owner(context.get_resource_owner());
    linestring_sizes.register_blob_owner(context.get_resource_owner());
    features.register_blob_owner(context.get_resource_owner());

    lm::dbbox2 bounds = {};

    for (const auto& proto_feature : client_response.features())
    {
        if (!params.expects_geometry)
        {
            if (proto_feature.geometry().coords_size() > 0)
            {
                HRZ_LOG_WARNING(
                    "Unexpected geometry in client-provided vector data, it will be ignored.");
                break;
            }
            continue;
        }

        hrz::vector_data::VectorTileGeometry::Feature feature{};
        feature.type = proto_feature.geometry().type();

        feature.first_point = points.size().value();
        feature.point_count = proto_feature.geometry().coords_size() / 3;

        for (size_t i = 0; i < feature.point_count; i++)
        {
            lm::dvec3 point = {
                proto_feature.geometry().coords(i * 3 + 0),
                proto_feature.geometry().coords(i * 3 + 1),
                proto_feature.geometry().coords(i * 3 + 2)};
            points.push_back(point);

            bounds = lm::expand(bounds, point.xy);
        }

        feature.first_linestring_size = linestring_sizes.size().value();
        feature.linestring_count = proto_feature.geometry().linestring_sizes_size();

        for (const auto size : proto_feature.geometry().linestring_sizes())
        {
            linestring_sizes.push_back(size);
        }

        if (feature.linestring_count == 0
            && (feature.type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY
                || feature.type == hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY))
        {
            linestring_sizes.push_back(feature.point_count);
            feature.linestring_count = 1;
        }

        feature.anchor = {};
        feature.anchor_angle = 0;

        auto read_points = points.data();
        auto read_linestring_sizes = linestring_sizes.data();
        if (read_points.has_value() && read_linestring_sizes.has_value() && feature.point_count > 0)
        {
            if (feature.type == hrz_proto::VectorGeometryType::POINT_GEOMETRY)
            {
                feature.anchor = (*read_points)[feature.first_point];
                feature.anchor_angle = 0;
            }
            else if (feature.type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY)
            {
                uint32_t linestring_size = (*read_linestring_sizes)[feature.first_linestring_size];
                auto feature_points = read_points->subspan(feature.first_point, linestring_size);
                feature.anchor = hrz::vector_data::compute_ring_average(feature_points);
                feature.anchor_angle = 0;
            }
            else if (feature.type == hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY)
            {
                auto linestring_sizes = read_linestring_sizes->subspan(
                    feature.first_linestring_size, feature.linestring_count);
                auto feature_points =
                    read_points->subspan(feature.first_point, feature.point_count);
                hrz::vector_data::compute_linestring_middle_and_angle(
                    feature_points, linestring_sizes, &feature.anchor, &feature.anchor_angle);
            }
        }

        features.push_back(feature);
    }

    if (!points.is_valid() || !linestring_sizes.is_valid() || !features.is_valid())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    auto feature_array_opt = features.to_blob_array();
    auto point_array_opt = points.to_blob_array();
    auto linestring_size_array_opt = linestring_sizes.to_blob_array();

    if (!feature_array_opt.has_value() || !point_array_opt.has_value()
        || !linestring_size_array_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    // Get the attributes and feature ids
    uint32_t feature_id_attribute_count = 0;

    // Why not use a map of MutableAttributeValues?
    // Because their order matter, as we will want to extract the first few of them
    // in a span later.
    hrz::flat_hash_map<uint32_t, uint32_t> attribute_id_to_index;
    std::vector<hrz::vector_data::PackedAttributeValuesBuilder> attribute_index_to_values;
    std::vector<uint32_t> attribute_id;

    auto emplace_back_attribute_values = [&](uint32_t id)
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

        auto index = attribute_id_to_index.at(model_attribute.id);
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

    std::span<hrz::vector_data::AttributeValues> values_finalized_span(
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
        auto index = attribute_id_to_index.at(attribute_model.id);
        response.attributes.push_back(std::move(attribute_index_to_values_finalized.at(index)));
    }

    response.geometry.bounds = bounds;
    response.geometry.features = feature_array_opt.value();
    response.geometry.points = point_array_opt.value();
    response.geometry.linestring_sizes = linestring_size_array_opt.value();

    return hrz_jobs::JobResult::SUCCESS;
}
} // namespace hrz_jobs::move_client_vector_data_to_blobs
