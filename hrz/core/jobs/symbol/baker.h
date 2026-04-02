#pragma once

#include "hrz/common/blob_vector.h"
#include "hrz/core/jobs/context.h"
#include "hrz/core/jobs/feature_clamping.h"
#include "hrz/core/jobs/job_result.h"
#include "hrz/core/jobs/vector_tiles_jobs_params.h"
#include "hrz/fnd/class.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/fnd/log.h"

#include <hb.h>
#include <lin_maths.h>

#include <limits>
#include <optional>

namespace hrz_jobs::symbol
{

using PlaceholderInstance = hrz_jobs::BakedSymbols::PlaceholderInstance;
using AnchorGpu = hrz_jobs::BakedSymbols::AnchorGpu;
using AnchorCulling = hrz::vt::AnchorCullingInfo;
using AnchorSpan = hrz::vt::AnchorSpan;
using ImageInstance = hrz_jobs::BakedSymbols::ImageInstance;
using DecoratedShapeInstance = hrz_jobs::BakedSymbols::DecoratedShapeInstance;
using LeaderLineInstance = hrz_jobs::BakedSymbols::LeaderLineInstance;

using Size = lm::vec2;

struct ElementGeometry
{
    explicit ElementGeometry(Size size = {}, lm::bbox2 rect = lm::bbox2::invalid()) :
        layout_size{size}, visual_rect{rect}
    {
    }

    Size layout_size;
    lm::bbox2 visual_rect;
};

lm::bbox2 transform_rect_2d_no_rotation(lm::bbox2 rect, const lm::mat4& transform);
lm::bbox2 transform_rect_2d_offset(lm::bbox2 rect, lm::vec2 offset);
lm::bbox2 transform_rect_2d(lm::bbox2 rect, const lm::mat4& transform);
lm::bbox2 transform_rect_3d(lm::bbox2 rect, const lm::mat4& transform);

struct SizeConstraints
{
    Size min;
    Size max;
};

Size constrain_size_preserve_aspect_ratio(const SizeConstraints&, Size, float aspect_ratio = 0.0F);
Size constrain_box_fit(
    Size container_size,
    Size content_size,
    hrz_proto::BoxFit mode,
    hrz_proto::BoxFitAxes axes);

constexpr float infinity()
{
    return std::numeric_limits<float>::infinity();
}

static inline float max_finite(float a, float b)
{
    if (std::isinf(a)) return b;
    if (std::isinf(b)) return a;
    return std::max(a, b);
}

// Baking symbols consists in:
// * For all visual elements, prepare blob vectors for the GPU data.
// * For all features in the tile, visit all elements, in order.
//   * The first element has no size constraints.
//   * When visiting an element, using its baking parameters, the feature styling property
//     values, and the size constraints, compute the all the values that are relevant for
//     the element. This includes size constraints on the children.
//   * Visit children, and retrieve their size.
//   * Place children in the element's local space. (This is the symbol canvas space, not
//     the scene's 3D space.)
//   * Compute the element instance's size.
//   * For visual elements, create and push a GPU data instance.
//   * Return the element instance's size.
// * Compute the size of the whole symbol for the feature.
// * Compute the local transforms of the children of anchors, from the symbol size and the
//   anchor's alignment.
// * Turn all transforms from local transforms into global transforms (still in the symbol
//   canvas space).
// * Update the transforms in the GPU data buffers.
struct SymbolBaker
{
private:
    struct ElementInfo
    {
        ElementInfo() : transform{lm::mat4::identity()}, geometry{} {}

        // Transform given by the parent. This is transformed into a global transform after baking,
        // and finally must be written to GPU data for visual elements.
        lm::mat4 transform;

        // Index of the emitted instance in the visitor for this symbol element instance.
        std::optional<uint32_t> element_instance_index;

        // Geometry of this element computed by itself during baking. This is local.
        ElementGeometry geometry;
    };

    struct ElementVisitor
    {
        explicit ElementVisitor(SymbolBaker* baker) : baker(baker) {}

        HRZ_DELETE_COPY_MOVE(ElementVisitor);
        virtual ~ElementVisitor() = default;

        virtual hrz_jobs::JobResult init() { return hrz_jobs::JobResult::SUCCESS; }

        virtual void deinit() {}

        const JobContext& get_context() { return baker->context; }

        lm::dvec3 get_tile_center() { return baker->tile_center; }

        lm::dvec3 get_feature_position() { return baker->feature_position; }

        uint32_t get_feature_index() { return baker->feature_index; }

        hrz::vector_data::FeatureIdHash get_feature_id() { return baker->feature_id; }

        void load_bool_property(uint64_t prp, bool* dst) const
        {
            baker->load_bool_property(prp, dst);
        }

        void load_uint_property(uint64_t prp, uint64_t* dst) const
        {
            baker->load_uint_property(prp, dst);
        }

        void load_int_property(uint64_t prp, int64_t* dst) const
        {
            baker->load_int_property(prp, dst);
        }

        void load_float_property(uint64_t prp, float* dst) const
        {
            baker->load_float_property(prp, dst);
        }

        void load_vec2f_property(const lm::ulvec2& prps, lm::vec2* dst) const
        {
            baker->load_vec2f_property(prps, dst);
        }

        void load_vec3f_property(const lm::ulvec3& prps, lm::vec3* dst) const
        {
            baker->load_vec3f_property(prps, dst);
        }

        void load_rgba_color_property(uint64_t prp, lm::ubvec4* dst) const
        {
            baker->load_rgba_color_property(prp, dst);
        }

        void load_string_property(uint64_t prp, std::string_view* dst) const
        {
            baker->load_string_property(prp, dst);
        }

        template<typename TEnum>
        void load_enum_property(uint64_t prp, TEnum* dst)
        {
            baker->load_enum_property<TEnum>(prp, dst);
        }

        // Index is index among anchors in the symbol representation.
        void push_anchor(
            const AnchorGpu& anchor_gpu,
            const AnchorCulling& anchor_culling,
            lm::dvec3 position,
            uint32_t index)
        {
            baker->anchor_gpu_data.push_back(anchor_gpu);
            baker->anchor_culling_data.push_back(anchor_culling);
            baker->anchor_positions.push_back(position);

            if (baker->anchor_indices_to_baked_anchor_indices.size() < index + 1)
            {
                baker->anchor_indices_to_baked_anchor_indices.resize(index + 1, std::nullopt);
            }
            baker->anchor_indices_to_baked_anchor_indices[index] = {baker->anchor_count};

            baker->anchor_count += 1;
        }

        // Only called for elements that have a z-index, i.e. visual elements.
        virtual hrz_jobs::JobResult init_element_instances(
            const hrz_jobs::SymbolBakingData::Element& element)
        {
            return hrz_jobs::JobResult::SUCCESS;
        }

        virtual ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) = 0;

        // Call this function when visiting an element that has children, on each child.
        ElementGeometry visit_child(
            uint32_t child_element_index,
            const SizeConstraints& constraints)
        {
            assert(child_element_index < baker->params.elements.size());

            uint32_t parent_element_index = baker->current_element_index;
            auto child_geometry = baker->visit_element(child_element_index, constraints);
            baker->current_element_index = parent_element_index;

            return child_geometry;
        }

        void register_element_instance_index(uint32_t instance_index)
        {
            baker->element_instance_info.at(baker->current_element_index).element_instance_index = {
                instance_index
            };
        }

        // Every parent must set the local transform of its children.
        void set_child_local_transform(uint32_t child_element_index, const lm::mat4& transform)
        {
            assert(child_element_index < baker->params.elements.size());

            baker->element_instance_info.at(child_element_index).transform = transform;
        }

        const Size& get_child_size(uint32_t child_element_index)
        {
            assert(child_element_index < baker->params.elements.size());
            return baker->element_instance_info.at(child_element_index).geometry.layout_size;
        }

        const lm::bbox2& get_child_visual_rect(uint32_t child_element_index)
        {
            assert(child_element_index < baker->params.elements.size());
            return baker->element_instance_info.at(child_element_index).geometry.visual_rect;
        }

        const hrz_jobs::SymbolBakingData::Element& get_child(uint32_t index) const
        {
            return baker->params.elements[index];
        }

        hrz_proto::SymbolElementType get_child_type(uint32_t index) const
        {
            return get_child(index).type;
        }

        // Only call this function during element instances finalization.
        uint32_t get_baked_anchor_index(uint32_t anchor_index) const
        {
            if (baker->anchor_indices_to_baked_anchor_indices.size() > anchor_index)
            {
                const auto& baked_index =
                    baker->anchor_indices_to_baked_anchor_indices.at(anchor_index);
                if (baked_index.has_value())
                {
                    return baked_index.value();
                }
            }

            HRZ_LOG_ERROR("Anchor at index {} not baked", anchor_index);
            return 0;
        }

        // Set global transform and baked anchor index (by calling
        // `get_baked_anchor_index()`).
        virtual void finalize_element_instance(
            uint32_t z_index,
            uint32_t element_instance_index,
            const lm::mat4& global_transform)
        {
        }

        // Outer optional is for success or failure.
        // Inner optional is for when no instances have been baked. (Distinguishing this case
        // allows not generating empty renderables.)
        // Visitors for visual elements must override this method, and make it return a value.
        // (If they don't fail.)
        virtual std::optional<std::optional<hrz_jobs::BakedSymbols::ElementInstances>>
        get_element_instances_at_z_index(uint32_t z_index)
        {
            return std::nullopt;
        }

    private:
        SymbolBaker* baker;
    };

    struct PlaceholderVisitor : public ElementVisitor
    {
        explicit PlaceholderVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        hrz::flat_hash_map<uint32_t, hrz::BlobVector<PlaceholderInstance>> instances_by_z_index;

        hrz_jobs::JobResult init_element_instances(
            const hrz_jobs::SymbolBakingData::Element& element) override;
        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
        void finalize_element_instance(
            uint32_t z_index,
            uint32_t element_instance_index,
            const lm::mat4& global_transform) override;
        std::optional<std::optional<hrz_jobs::BakedSymbols::ElementInstances>>
        get_element_instances_at_z_index(uint32_t z_index) override;
    };

    struct AnchorVisitor : public ElementVisitor
    {
        explicit AnchorVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    struct LeaderLineVisitor : public ElementVisitor
    {
        explicit LeaderLineVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        hrz::flat_hash_map<uint32_t, hrz::BlobVector<LeaderLineInstance>> instances_by_z_index;

        hrz_jobs::JobResult init_element_instances(
            const hrz_jobs::SymbolBakingData::Element& element) override;
        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
        void finalize_element_instance(
            uint32_t z_index,
            uint32_t element_instance_index,
            const lm::mat4& global_transform) override;
        std::optional<std::optional<hrz_jobs::BakedSymbols::ElementInstances>>
        get_element_instances_at_z_index(uint32_t z_index) override;
    };

    struct StackVisitor : public ElementVisitor
    {
        explicit StackVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    struct StackExpandVisitor : public ElementVisitor
    {
        explicit StackExpandVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    struct ImageVisitor : public ElementVisitor
    {
        struct BakingData
        {
            // This points to the job baking data. Used in finalize.
            // The baking data is retained during the whole job, so it has a longer lifetime than
            // the baker. So this is fine. If this becomes untrue someday, sorry for the headache.
            std::span<const hrz_jobs::SymbolBakingData::Image::SpriteGeometry> sprite_geometries;
            std::vector<std::pair<uint32_t, int>> instance_index_geometry_index;
            hrz::BlobVector<ImageInstance> gpu_instances;
        };

        hrz::flat_hash_map<uint32_t, BakingData> instances_by_z_index;

        explicit ImageVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        hrz_jobs::JobResult init_element_instances(
            const hrz_jobs::SymbolBakingData::Element& element) override;
        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
        void finalize_element_instance(
            uint32_t z_index,
            uint32_t element_instance_index,
            const lm::mat4& global_transform) override;
        std::optional<std::optional<hrz_jobs::BakedSymbols::ElementInstances>>
        get_element_instances_at_z_index(uint32_t z_index) override;
    };

    struct PaddingVisitor : public ElementVisitor
    {
        explicit PaddingVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    struct SizedBoxVisitor : public ElementVisitor
    {
        explicit SizedBoxVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    struct FlexVisitor : public ElementVisitor
    {
        explicit FlexVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    struct FlexibleVisitor : public ElementVisitor
    {
        explicit FlexibleVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    struct ConstrainedBoxVisitor : public ElementVisitor
    {
        explicit ConstrainedBoxVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    struct RotatedBoxVisitor : public ElementVisitor
    {
        explicit RotatedBoxVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    struct DecoratedShapeVisitor : public ElementVisitor
    {
        hrz::flat_hash_map<uint32_t, hrz::BlobVector<DecoratedShapeInstance>> instances_by_z_index;

        explicit DecoratedShapeVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        hrz_jobs::JobResult init_element_instances(
            const hrz_jobs::SymbolBakingData::Element& element) override;
        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
        void finalize_element_instance(
            uint32_t z_index,
            uint32_t element_instance_index,
            const lm::mat4& global_transform) override;
        std::optional<std::optional<hrz_jobs::BakedSymbols::ElementInstances>>
        get_element_instances_at_z_index(uint32_t z_index) override;
    };

    struct AspectRatioVisitor : public ElementVisitor
    {
        explicit AspectRatioVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    struct FittedBoxVisitor : public ElementVisitor
    {
        explicit FittedBoxVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    struct TransformVisitor : public ElementVisitor
    {
        explicit TransformVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    struct TextVisitor : public ElementVisitor
    {
        struct Instances
        {
            uint32_t text_count;
            hrz::BlobVector<lm::mat4> transforms;
            hrz::BlobVector<uint32_t> anchor_indices;
            hrz::BlobVector<float> outline_widths;
            hrz::BlobVector<lm::ubvec4> fill_colors;
            hrz::BlobVector<lm::ubvec4> outline_colors;
            hrz::BlobVector<hrz_jobs::BakedSymbols::TextInstances::GlyphPositionUv>
                glyph_positions_uvs;
            hrz::BlobVector<uint16_t> text_indices;
            bool has_non_zero_outline_width;
        };

        hb_buffer_t* hb_buffer{};
        hrz::flat_hash_map<hrz::font_rasterizer::FontHandle, hrz::font_rasterizer::Font> fonts;
        hrz::flat_hash_map<uint32_t, Instances> instances_by_z_index;

        explicit TextVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        hrz_jobs::JobResult init() override;
        void deinit() override;
        hrz_jobs::JobResult init_element_instances(
            const hrz_jobs::SymbolBakingData::Element& element) override;
        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
        void finalize_element_instance(
            uint32_t z_index,
            uint32_t element_instance_index,
            const lm::mat4& global_transform) override;
        std::optional<std::optional<hrz_jobs::BakedSymbols::ElementInstances>>
        get_element_instances_at_z_index(uint32_t z_index) override;
    };

    struct OptionalVisitor : public ElementVisitor
    {
        explicit OptionalVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    struct VariantVisitor : public ElementVisitor
    {
        explicit VariantVisitor(SymbolBaker* baker) : ElementVisitor(baker) {}

        ElementGeometry visit_element(
            const hrz_jobs::SymbolBakingData::Element& element,
            const SizeConstraints& constraints) override;
    };

    // Job context
    const JobContext& context;

    // Visitors
    PlaceholderVisitor placeholder_visitor;
    AnchorVisitor anchor_visitor;
    StackVisitor stack_visitor;
    StackExpandVisitor stack_expand_visitor;
    ImageVisitor image_visitor;
    PaddingVisitor padding_visitor;
    SizedBoxVisitor sized_box_visitor;
    FlexVisitor flex_visitor;
    FlexibleVisitor flexible_visitor;
    ConstrainedBoxVisitor constrained_box_visitor;
    RotatedBoxVisitor rotated_box_visitor;
    DecoratedShapeVisitor decorated_shape_visitor;
    AspectRatioVisitor aspect_ratio_visitor;
    FittedBoxVisitor fitted_box_visitor;
    TransformVisitor transform_visitor;
    TextVisitor text_visitor;
    OptionalVisitor optional_visitor;
    VariantVisitor variant_visitor;
    LeaderLineVisitor leader_line_visitor;

    hrz::flat_hash_map<hrz_proto::SymbolElementType, ElementVisitor*> element_visitors;

    // Input data
    const hrz_jobs::SymbolBakingData& params;

    hrz::BlobArray<hrz::vector_data::VectorTileGeometry::Feature>::Data input_features;
    hrz::BlobArray<lm::dvec3>::Data input_points;
    hrz::BlobArray<uint64_t>::Data input_feature_ids;
    hrz::BlobArray<float>::Data input_clamps;

    const hrz::style::StyledFeatures& style;
    hrz::BlobArray<uint64_t>::Data style_prps;
    hrz::vector_data::PackedAttributeValuesReader style_values;

    hrz::FeatureClampingGenerator clamps_gen;

    // Output data
    uint32_t anchor_count;
    hrz::BlobVector<AnchorGpu> anchor_gpu_data;
    hrz::BlobVector<AnchorCulling> anchor_culling_data;
    hrz::BlobVector<AnchorSpan> anchor_spans_data;
    hrz::BlobVector<lm::dvec3> anchor_positions;

    double tile_radius{};
    lm::dvec3 tile_center;

    uint32_t max_feature_index{};

    // Feature visit parameters
    lm::dvec3 feature_position;
    uint32_t feature_index{};
    hrz::vector_data::FeatureIdHash feature_id{};
    uint32_t feature_prp_start{};
    uint32_t feature_prp_end{};
    uint32_t current_element_index{};
    hrz::InlinedVector<ElementInfo, 32> element_instance_info;
    hrz::InlinedVector<std::optional<uint32_t>, 32> anchor_indices_to_baked_anchor_indices;

public:
    SymbolBaker(const hrz_jobs::SymbolBakingData& params, const JobContext& context);

    hrz_jobs::JobResult bake(hrz_jobs::BakedSymbols& baked_symbols);

private:
    ElementGeometry visit_element(uint32_t element_index, const SizeConstraints& constraints);

    // The property loading methods only write a value to the destination
    // if there is one in the styling data.

    void load_bool_property(uint64_t prp, bool* dst) const
    {
        for (uint32_t i = feature_prp_start; i < feature_prp_end; ++i)
        {
            if (style_prps[i] == prp)
            {
                *dst = style_values.as_bool(i);
                break;
            }
        }
    }

    void load_uint_property(uint64_t prp, uint64_t* dst) const
    {
        for (uint32_t i = feature_prp_start; i < feature_prp_end; ++i)
        {
            if (style_prps[i] == prp)
            {
                *dst = style_values.as_uint64(i);
                break;
            }
        }
    }

    void load_int_property(uint64_t prp, int64_t* dst) const
    {
        for (uint32_t i = feature_prp_start; i < feature_prp_end; ++i)
        {
            if (style_prps[i] == prp)
            {
                *dst = style_values.as_int64(i);
                break;
            }
        }
    }

    void load_float_property(uint64_t prp, float* dst) const
    {
        for (uint32_t i = feature_prp_start; i < feature_prp_end; ++i)
        {
            if (style_prps[i] == prp)
            {
                *dst = (float)style_values.as_number(i);
                break;
            }
        }
    }

    void load_vec2f_property(const lm::ulvec2& prps, lm::vec2* dst) const
    {
        load_float_property(prps.x, &dst->x);
        load_float_property(prps.y, &dst->y);
    }

    void load_vec3f_property(const lm::ulvec3& prps, lm::vec3* dst) const
    {
        load_float_property(prps.x, &dst->x);
        load_float_property(prps.y, &dst->y);
        load_float_property(prps.z, &dst->z);
    }

    void load_rgba_color_property(uint64_t prp, lm::ubvec4* dst) const
    {
        for (uint32_t i = feature_prp_start; i < feature_prp_end; ++i)
        {
            if (style_prps[i] == prp)
            {
                *dst = style_values.as_color(i);
                break;
            }
        }
    }

    void load_string_property(uint64_t prp, std::string_view* dst) const
    {
        for (uint32_t i = feature_prp_start; i < feature_prp_end; ++i)
        {
            if (style_prps[i] == prp)
            {
                *dst = style_values.as_string(i);
                break;
            }
        }
    }

    template<typename TEnum>
    void load_enum_property(uint64_t prp, TEnum* dst) const
    {
        for (uint32_t i = feature_prp_start; i < feature_prp_end; ++i)
        {
            if (style_prps[i] == prp)
            {
                *dst = (TEnum)style_values.as_uint64(i);
                break;
            }
        }
    }

    ElementVisitor* get_element_visitor_for_type(hrz_proto::SymbolElementType element_type)
    {
        auto it = element_visitors.find(element_type);
        if (it != element_visitors.end())
        {
            return it->second;
        }
        else
        {
            assert(!"Unknown element visitor");
            return nullptr;
        }
    }
};

} // namespace hrz_jobs::symbol
