#include "hrz/common/blob_vector.h"
#include "hrz/common/profiling.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_data_jobs_params.h"

#include <lin_maths.h>

#include <vector>

namespace hrz_jobs::build_aabb_tree
{

using Node = hrz_jobs::AabbTree::Node;

namespace
{

void insert_node(const Node& node_to_insert, std::vector<Node>& nodes)
{
    if (nodes.empty())
    {
        nodes.push_back(node_to_insert);
        return;
    }

    uint32_t current_node_index = 0;
    bool node_is_inserted = false;

    while (!node_is_inserted)
    {
        nodes[current_node_index].bbox =
            lm::merge(nodes[current_node_index].bbox, node_to_insert.bbox);

        if (nodes[current_node_index].has_children)
        {
            auto child0_index = nodes[current_node_index].child_indices[0];
            auto child1_index = nodes[current_node_index].child_indices[1];

            auto& child0 = nodes[child0_index];
            auto& child1 = nodes[child1_index];

            auto bbox0 = lm::merge(node_to_insert.bbox, child0.bbox);
            auto bbox1 = lm::merge(node_to_insert.bbox, child1.bbox);

            auto area0 = lm::area(bbox0);
            auto area1 = lm::area(bbox1);

            if (area0 <= area1)
            {
                current_node_index = child0_index;
            }
            else
            {
                current_node_index = child1_index;
            }
        }
        else
        {
            uint32_t child0_index = nodes.size();
            nodes.push_back(nodes[current_node_index]);

            uint32_t child1_index = nodes.size();
            nodes.push_back(node_to_insert);

            nodes[current_node_index].has_children = true;
            nodes[current_node_index].child_indices[0] = child0_index;
            nodes[current_node_index].child_indices[1] = child1_index;

            node_is_inserted = true;
        }
    }
}

} // namespace

hrz_jobs::JobResult run(
    const hrz::vector_data::VectorTileGeometry& geometry,
    hrz_jobs::AabbTree& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("build aabb job");

    auto features = geometry.features.get_cdata();
    auto points = geometry.points.get_cdata();

    std::vector<Node> nodes;

    for (uint32_t feature_index = 0; feature_index < features.size(); ++feature_index)
    {
        const auto& feature = features[feature_index];

        uint32_t first_point = feature.first_point;
        uint32_t last_point = feature.first_point + feature.point_count;

        lm::dbbox2 bbox = lm::dbbox2::invalid();

        for (uint32_t i = first_point; i < last_point; ++i)
        {
            bbox = lm::expand(bbox, points[i].xy);
        }

        Node node;
        node.bbox = bbox;
        node.feature_index = feature_index;
        node.has_children = false;

        insert_node(node, nodes);
    }

    if (!nodes.empty())
    {
        response.bounds = hrz::web_mercator_bounds_to_geo(nodes[0].bbox);
    }
    else
    {
        response.bounds = hrz::GeoBounds::empty();
    }

    auto ba = context.get_blob_allocator();

    auto aabb_tree = hrz::BlobVector<Node>(ba, nodes.size());
    aabb_tree.register_blob_metadata("contents"_ss, "vector features AABB"_ss);
    aabb_tree.register_blob_owner(context.get_resource_owner());

    for (const auto& node : nodes)
    {
        aabb_tree.push_back(node);
    }

    auto aabb_tree_array_opt = aabb_tree.to_blob_array();

    if (!aabb_tree_array_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    response.aabb_tree = std::move(aabb_tree_array_opt.value());

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace hrz_jobs::build_aabb_tree
