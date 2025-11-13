#pragma once

#include "hrz/core/vector/symbol/element.h"
#include "hrz/fnd/gen_object_pool.h"

namespace hrz::vt::symbol
{
// Specialisation of ElementSystem for layout-only elements,
// i.e. those that don't have graphical representations.
template<typename PrototypeT, hrz_proto::SymbolElementType ELEMENT_TYPE>
struct LayoutElementSystem : public ElementSystem
{
protected:
    using Prototype = PrototypeT;
    static constexpr hrz_proto::SymbolElementType ElementType = ELEMENT_TYPE;

    using PrototypeIndexPool = hrz::GenIndexPool<RawPrototypeH, 32, 32>;
    using PrototypePool = hrz::GenObjectPool<Prototype, PrototypeIndexPool, 64>;

    PrototypePool _prototypes;

    void deinit(ReprSystem::WorkCtx&, Render*) override {}

    void delete_prototype(PrototypeH) override {}

    PrototypeStatus get_prototype_status(PrototypeH prototype_handle) const override
    {
        if (prototype_handle.type == ElementType
            && _prototypes.get_object(prototype_handle.handle) != nullptr)
        {
            return PrototypeStatus::Ready;
        }

        return PrototypeStatus::Error;
    }

    SymbolBakingData::ElementBakingParams get_prototype_baking_params(
        PrototypeH prototype_handle) const override
    {
        if (prototype_handle.type != ElementType)
        {
            assert(false);
            return {};
        }

        auto prototype = _prototypes.get_object(prototype_handle.handle);
        if (prototype == nullptr)
        {
            assert(false);
            return {};
        }

        return {*prototype};
    }

public:
    bool is_visual() const override { return false; }

    void init_render(Render*) override {}

    void deinit_render(Render*) override {}

    std::optional<RenderableH> make_renderable(
        PrototypeH,
        uint64_t layer_id,
        TileCoords tile_coords,
        BakedSymbols::ElementInstances&& baked_instances,
        double bsphere_radius,
        lm::dvec3 bsphere_center,
        my::ResourceHandle tile_ubo,
        my::ResourceHandle anchor_ubo,
        my::ResourceHandle anchor_data_texture,
        my::ResourceHandle selection_texture,
        const std::array<my::ResourceHandle, SCENE_VIEW_COUNT> culling_visibility_textures,
        my::ResourceHandle data_texture_sampler,
        uint32_t z_index,
        Render*) override
    {
        return std::nullopt;
    }

    void delete_renderable(RenderableH) override {}

    void draw_renderable(
        RenderableH,
        uint32_t scene_views,
        bool has_any_selected,
        bool ignore_occlusions,
        Render*) override
    {
    }

    void work(ReprSystem::WorkCtx&) override {}

    void work_gpu(Render*) override {}
};

// The stack element allow superimposing multiple elements on top of
// each other.
// How the smaller elements are placed with respect to the largest
// element is controlled by the alignment. (0,0) centers all the
// elements. -1 or 1 in one axis aligns the elements on the border.
struct StackElementSystem :
    public LayoutElementSystem<SymbolBakingData::Stack, hrz_proto::STACK_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

struct StackExpandElementSystem :
    public LayoutElementSystem<
        SymbolBakingData::StackExpand,
        hrz_proto::STACK_EXPAND_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

struct PaddingElementSystem :
    public LayoutElementSystem<SymbolBakingData::Padding, hrz_proto::PADDING_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

struct SizedBoxElementSystem :
    public LayoutElementSystem<SymbolBakingData::SizedBox, hrz_proto::SIZED_BOX_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

struct FlexElementSystem :
    public LayoutElementSystem<SymbolBakingData::Flex, hrz_proto::FLEX_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

struct FlexibleElementSystem :
    public LayoutElementSystem<SymbolBakingData::Flexible, hrz_proto::FLEXIBLE_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

struct AlignElementSystem :
    public LayoutElementSystem<SymbolBakingData::FittedBox, hrz_proto::ALIGN_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

struct ConstrainedBoxElementSystem :
    public LayoutElementSystem<
        SymbolBakingData::ConstrainedBox,
        hrz_proto::CONSTRAINED_BOX_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

struct RotatedBoxElementSystem :
    public LayoutElementSystem<SymbolBakingData::RotatedBox, hrz_proto::ROTATED_BOX_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

struct AspectRatioElementSystem :
    public LayoutElementSystem<
        SymbolBakingData::AspectRatio,
        hrz_proto::ASPECT_RATIO_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

struct FittedBoxElementSystem :
    public LayoutElementSystem<SymbolBakingData::FittedBox, hrz_proto::FITTED_BOX_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

struct TransformElementSystem :
    public LayoutElementSystem<SymbolBakingData::Transform, hrz_proto::TRANSFORM_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

struct OptionalElementSystem :
    public LayoutElementSystem<SymbolBakingData::Optional, hrz_proto::OPTIONAL_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

struct VariantElementSystem :
    public LayoutElementSystem<SymbolBakingData::Variant, hrz_proto::VARIANT_SYMBOL_ELEMENT>
{
private:
    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;
};

} // namespace hrz::vt::symbol
