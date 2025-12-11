#include "hrz/common/geo.h"
#include "hrz/common/proto_maths.h"
#include "hrz/common/vertex_utils.h"
#include "hrz/core/jobs/symbol/baker.h"

namespace hrz_jobs::symbol
{
ElementGeometry SymbolBaker::AnchorVisitor::visit_element(
    const hrz_jobs::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElementType::ANCHOR_SYMBOL_ELEMENT);
    const auto& params = element.anchor();

    auto position_offset = params.default_position_offset;
    load_vec3f_property(params.position_offset_prp, &position_offset);

    auto rotation = params.default_rotation;
    load_vec3f_property(params.rotation_prp, &rotation);

    auto culling_priority = params.default_culling_priority;
    load_float_property(params.culling_priority_prp, &culling_priority);

    lm::dvec3 position = get_feature_position();
    lm::vec3 in_tile_position = lm::vec3(position - get_tile_center());

    hrz::GeoPosition2 ll = hrz::ecef_to_geo2(position);
    lm::dmat4 enu_matrix = hrz::enu_to_ecef_rotation_matrix_for_geo(ll);
    uint32_t local_east_oct = hrz::octahedral_compress_normal(lm::vec3(enu_matrix.x.xyz));
    uint32_t local_up_oct = hrz::octahedral_compress_normal(lm::vec3(enu_matrix.z.xyz));

    lm::mat4 local_rotation_matrix = hrz::euler_rotation(rotation, params.rotation_order);
    lm::vec3 euler_angles = hrz::extract_euler_angles_xyz(local_rotation_matrix);

    AnchorGpu anchor_gpu;
    anchor_gpu.in_tile_position = in_tile_position;
    anchor_gpu.position_offset = lm::vec3((enu_matrix * lm::dvec4(position_offset, 0)).xyz);
    anchor_gpu.local_east_axis = local_east_oct;
    anchor_gpu.local_up_axis = local_up_oct;
    anchor_gpu.rotation = euler_angles;
    anchor_gpu.feature_index = get_feature_index();
    anchor_gpu.feature_id = get_feature_id();

    AnchorCulling anchor_culling;
    // The rectangle will be filled later, in the baker's instance finalization code
    anchor_culling.in_tile_position = in_tile_position;
    anchor_culling.position_offset = lm::vec3((enu_matrix * lm::dvec4(position_offset, 0)).xyz);
    anchor_culling.local_east_axis = lm::vec3(enu_matrix.x.xyz);
    anchor_culling.local_up_axis = lm::vec3(enu_matrix.z.xyz);
    anchor_culling.rotation = euler_angles;
    anchor_culling.anchor_prototype_index = element.anchor_index.value();
    anchor_culling.priority = culling_priority;

    assert(element.anchor_index.has_value());
    push_anchor(anchor_gpu, anchor_culling, position, element.anchor_index.value());

    auto geometry = visit_child(params.child_index, constraints);
    if (params.reset_layout)
    {
        // If we reset layout, we'll use the child's info during post-processing to align the child
        // to the anchor, and we don't want to transmit any info to the parent.
        return ElementGeometry{Size{}, lm::bbox2{}};
    }
    else
    {
        return geometry;
    }
}
} // namespace hrz_jobs::symbol
