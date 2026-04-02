#include "hrz/common/blob_array.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/profiling.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_data_jobs_params.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/log.h"

#include <limits>

// Vector data (both geometry and attributes) loaded from a single
// source is consistent in terms of the order of the features,
// including missing and repeated ones. This is the case whether
// the data is for a tile or an arbitrary list of feature IDs.
// (This is the contract the system that loaded it must respect.)
//
// However the vector data loader can load data from multiple sources
// and combine them together before passing the resulting combination
// to other systems. In this case, because the sources are indepen-
// dent, they may not have their feature data in the same order, or
// with the same missing or duplicated features. The first data source
// in the source list is the primary source and establishes the cano-
// nical feature order.
//
// The role of this job is to take vector data from a secondary source,
// the feature IDs from the canonical order, and sort (as well as
// filter or duplicate if necessary) the vector data so that it con-
// forms to the given order if the join is by feature ID; or ensure
// that the number of features in the vector data matches the
// number of features from the primary source if the join is by
// feature count.

using namespace hrz::vector_data;

namespace hrz_jobs::join_vector_data
{
namespace
{

hrz::vector_data::VectorTileGeometry::Feature make_empty_feature()
{
    hrz::vector_data::VectorTileGeometry::Feature feature;
    feature.type = hrz_proto::VectorGeometryType::POLYGON_GEOMETRY;
    feature.first_point = 0;
    feature.point_count = 0;
    feature.first_linestring_size = 0;
    feature.linestring_count = 0;
    feature.anchor = lm::dvec3(std::numeric_limits<double>::quiet_NaN());
    feature.anchor_angle = 0;
    return feature;
}

hrz_jobs::JobResult join_by_feature_ids(
    const hrz_jobs::UnjoinedVectorData& params,
    hrz_jobs::JoinedVectorData& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("join by feature id");

    assert(params.join_by_feature_ids);

    if (!params.feature_ids.has_same_attributes(params.reference_feature_ids))
    {
        HRZ_LOG_WARNING("Feature IDs do not contain the same attributes");
        return hrz_jobs::JobResult::FAILURE;
    }

    auto hashes_data = params.feature_ids.hashes().get_data();
    auto reference_hashes_data = params.reference_feature_ids.hashes().get_data();

    bool already_sorted = params.feature_ids.size() == params.reference_feature_ids.size();

    if (already_sorted)
    {
        for (size_t i = 0; i < params.feature_ids.size(); ++i)
        {
            if (hashes_data.at(i) != reference_hashes_data.at(i))
            {
                already_sorted = false;
                break;
            }
        }
    }

    if (already_sorted && params.geometry.has_value()
        && params.geometry->features.size() != params.reference_feature_ids.size())
    {
        already_sorted = false;
    }

    for (size_t i = 0; i < params.attributes.size(); ++i)
    {
        const auto& attribute = params.attributes.at(i);

        if (attribute.values.size() != params.reference_feature_ids.size())
        {
            already_sorted = false;
            break;
        }
    }

    if (already_sorted)
    {
        response.geometry = params.geometry;
        response.attributes = params.attributes;

        return hrz_jobs::JobResult::SUCCESS;
    }

    hrz::flat_hash_map<hrz::vector_data::FeatureIdHash, size_t> feature_ids_to_indices;
    for (size_t i = 0; i < params.feature_ids.size(); ++i)
    {
        feature_ids_to_indices.insert_or_assign(hashes_data.at(i), i);
    }

    if (params.geometry.has_value())
    {
        using Feature = hrz::vector_data::VectorTileGeometry::Feature;

        auto unsorted_features = params.geometry.value().features.get_data();
        hrz::BlobVector<Feature> sorted_features(
            context.get_blob_allocator(), unsorted_features.size());
        sorted_features.register_blob_metadata("contents"_ss, "joined vector features"_ss);
        sorted_features.register_blob_owner(context.get_resource_owner());

        for (auto feature_id_hash : reference_hashes_data)
        {
            Feature feature;

            auto it = feature_ids_to_indices.find(feature_id_hash);
            if (it != feature_ids_to_indices.end())
            {
                feature = unsorted_features.at(it->second);
            }
            else
            {
                feature = make_empty_feature();
            }

            sorted_features.push_back(feature);
        }

        auto features_opt = sorted_features.to_blob_array();
        if (!features_opt.has_value()) return hrz_jobs::JobResult::FAILURE;

        hrz::vector_data::VectorTileGeometry geometry;
        geometry.bounds = params.geometry.value().bounds;
        geometry.features = std::move(features_opt.value());
        geometry.points = params.geometry.value().points;
        geometry.linestring_sizes = params.geometry.value().linestring_sizes;
        response.geometry = {std::move(geometry)};
    }

    for (size_t i = 0; i < params.attributes.size(); ++i)
    {
        const auto& attribute = params.attributes.at(i);

        auto unsorted_values = attribute.values.get_data();
        hrz::BlobVector<hrz::vector_data::PackedAttributeValue> sorted_values(
            context.get_blob_allocator(), unsorted_values.size());
        sorted_values.register_blob_metadata("contents"_ss, "joined attribute values"_ss);
        sorted_values.register_blob_owner(context.get_resource_owner());

        for (auto feature_id_hash : reference_hashes_data)
        {
            auto value = hrz::vector_data::attr_null<hrz::vector_data::PackedAttributeValue>();

            auto it = feature_ids_to_indices.find(feature_id_hash);
            if (it != feature_ids_to_indices.end())
            {
                value = unsorted_values.at(it->second);
            }

            sorted_values.push_back(value);
        }

        auto values_opt = sorted_values.to_blob_array();
        if (!values_opt.has_value()) return hrz_jobs::JobResult::FAILURE;

        hrz::vector_data::AttributeValues sorted_attribute;
        sorted_attribute.attribute_id = attribute.attribute_id;
        sorted_attribute.values = std::move(values_opt.value());
        sorted_attribute.out_of_line_data = attribute.out_of_line_data;
        response.attributes.push_back(std::move(sorted_attribute));
    }

    return hrz_jobs::JobResult::SUCCESS;
}

hrz_jobs::JobResult match_feature_counts(
    const hrz_jobs::UnjoinedVectorData& params,
    hrz_jobs::JoinedVectorData& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("match feature count");

    assert(!params.join_by_feature_ids);

    const size_t expected_feature_count = params.reference_feature_ids.size();

    if (params.geometry.has_value())
    {
        response.geometry = params.geometry;

        if (params.geometry->features.size() > expected_feature_count)
        {
            response.geometry->features = params.geometry->features.make_sub_array(
                hrz::unsafe("Size is checked above"), 0, expected_feature_count);
        }
        else if (params.geometry->features.size() < expected_feature_count)
        {
            using Feature = hrz::vector_data::VectorTileGeometry::Feature;

            auto params_features = params.geometry.value().features.get_data();
            hrz::BlobVector<Feature> response_features(
                context.get_blob_allocator(), params_features.size());
            response_features.register_blob_metadata("contents"_ss, "joined vector features"_ss);
            response_features.register_blob_owner(context.get_resource_owner());

            for (size_t i = 0; i < params.geometry->features.size(); ++i)
            {
                response_features.push_back(params_features.at(i));
            }

            for (size_t i = params.geometry->features.size(); i < expected_feature_count; ++i)
            {
                response_features.push_back(make_empty_feature());
            }

            auto features_opt = response_features.to_blob_array();
            if (!features_opt.has_value()) return hrz_jobs::JobResult::FAILURE;

            hrz::vector_data::VectorTileGeometry geometry;
            geometry.bounds = params.geometry.value().bounds;
            geometry.features = std::move(features_opt.value());
            geometry.points = params.geometry.value().points;
            geometry.linestring_sizes = params.geometry.value().linestring_sizes;
            response.geometry = {std::move(geometry)};
        }
    }

    for (size_t i = 0; i < params.attributes.size(); ++i)
    {
        response.attributes.push_back(params.attributes.at(i));
        auto& attribute = response.attributes.at(i);

        if (attribute.values.size() > expected_feature_count)
        {
            attribute.values = attribute.values.make_sub_array(
                hrz::unsafe("Size is checked above"), 0, expected_feature_count);
        }
        else if (attribute.values.size() < expected_feature_count)
        {
            hrz::BlobVector<hrz::vector_data::PackedAttributeValue> joined_values(
                context.get_blob_allocator(), expected_feature_count);
            joined_values.register_blob_metadata("contents"_ss, "joined attribute values"_ss);
            joined_values.register_blob_owner(context.get_resource_owner());

            auto param_values = attribute.values.get_data();
            for (size_t j = 0; j < param_values.size(); ++j)
            {
                joined_values.push_back(param_values.at(j));
            }

            for (size_t j = param_values.size(); j < expected_feature_count; ++j)
            {
                joined_values.push_back(
                    hrz::vector_data::attr_null<hrz::vector_data::PackedAttributeValue>());
            }

            auto values_opt = joined_values.to_blob_array();
            if (!values_opt.has_value()) return hrz_jobs::JobResult::FAILURE;

            attribute.values = std::move(values_opt.value());
        }
    }

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace

hrz_jobs::JobResult run(
    const hrz_jobs::UnjoinedVectorData& params,
    hrz_jobs::JoinedVectorData& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("join vector data job");

#ifndef NDEBUG
    assert(
        !params.geometry.has_value()
        || params.geometry->features.size() == params.feature_ids.size());

    for (const auto& attribute : params.attributes)
    {
        assert(attribute.values.size() == params.feature_ids.size());
    }
#endif

    auto res = params.join_by_feature_ids ? join_by_feature_ids(params, response, context)
                                          : match_feature_counts(params, response, context);

#ifndef NDEBUG
    assert(
        !response.geometry.has_value()
        || response.geometry->features.size() == params.reference_feature_ids.size());

    for (const auto& attribute : response.attributes)
    {
        assert(attribute.values.size() == params.reference_feature_ids.size());
    }
#endif

    return res;
}

} // namespace hrz_jobs::join_vector_data
