#include "hrz_jobs_declarations.h"

#include <hrz_common_blob_allocator.h>
#include <hrz_common_blob_array.h>
#include <hrz_common_blob_vector.h>
#include <hrz_common_profiling.h>
#include <hrz_common_vector_data.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_log.h>

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
// with the same missing or duplicated features. But the data source,
// from which the geometry is loaded and used, establishes the cano-
// nical feature order.
//
// The role of this job is to take vector data from a secondary source,
// the feature IDs from the canonical order, and sort (as well as
// filter or duplicate if necessary) the vector data so that it con-
// forms to the given order.

using namespace hrz::vector_data;

namespace hrz_jobs::sort_vector_data
{
hrz::JobResult run(
    const hrz::vector_data::UnsortedVectorData& params,
    hrz::vector_data::SortedVectorData& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("sort vector data job");

#ifndef NDEBUG
    for (const auto& attribute : params.attributes)
    {
        assert(attribute.values.size() == params.feature_ids.size());
    }
#endif

    if (!params.feature_ids.has_same_attributes(params.reference_feature_ids))
    {
        HRZ_LOG_WARNING("Feature IDs do not contain the same attributes");
        return hrz::JobResult::FAILURE;
    }

    auto hashes_data = params.feature_ids.hashes().get_data();
    auto reference_hashes_data = params.reference_feature_ids.hashes().get_data();

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
        sorted_features.register_blob_metadata("contents"_ss, "sorted vector features"_ss);
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
                feature.type = hrz_proto::VectorGeometryType::POLYGON_GEOMETRY;
                feature.anchor = lm::dvec3(0, 0, 0);
                feature.anchor_angle = 0;
                feature.first_point = 0;
                feature.point_count = 0;
                feature.first_linestring_size = 0;
                feature.linestring_count = 0;
            }

            sorted_features.push_back(feature);
        }

        auto features_opt = sorted_features.to_blob_array();
        if (!features_opt.has_value()) return hrz::JobResult::FAILURE;

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
        sorted_values.register_blob_metadata("contents"_ss, "sorted attribute values"_ss);
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
        if (!values_opt.has_value()) return hrz::JobResult::FAILURE;

        hrz::vector_data::AttributeValues sorted_attribute;
        sorted_attribute.attribute_id = attribute.attribute_id;
        sorted_attribute.values = std::move(values_opt.value());
        sorted_attribute.out_of_line_data = attribute.out_of_line_data;
        response.attributes.push_back(std::move(sorted_attribute));
    }

#ifndef NDEBUG
    for (const auto& attribute : response.attributes)
    {
        assert(attribute.values.size() == params.reference_feature_ids.size());
    }
#endif

    return hrz::JobResult::SUCCESS;
}
} // namespace hrz_jobs::sort_vector_data
