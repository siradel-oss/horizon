#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/core/attribution.h"
#include "hrz/core/model/animation.h"
#include "hrz/core/model/blob_library.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/meta.h"
#include "hrz/protocol/image.pb.h"

#include <mycelium/backend.h>

#include <optional>

namespace hrz::model
{
/**
 * The model descriptor is the full description of a 3D model.
 * It's the in-memory representation of a model prototype.
 * All its resources are referenced by the blob library.
 */
struct ModelDescriptor
{
    static constexpr size_t MaxUvCount = 4;

    struct Transform
    {
        struct TRS
        {
            lm::dvec3 translation{};
            lm::dquat rotation{};
            lm::dvec3 scale{1.0, 1.0, 1.0};

            lm::dmat4 to_matrix() const
            {
                return lm::translation(translation) * lm::rotation_normalized(rotation)
                    * lm::scaling(scale);
            }
        };

        std::variant<lm::dmat4, TRS> data;

        lm::dmat4 to_matrix() const
        {
            return std::visit(
                hrz::overload{
                    [](const lm::dmat4& m) { return m; },
                    [](const TRS& trs)
                    {
                        return lm::translation(trs.translation)
                            * lm::rotation_normalized(trs.rotation) * lm::scaling(trs.scale);
                    },
                },
                data);
        }
    };

    struct Node
    {
        Transform transform;
    };

    // Nodes can be instanced by multiple roots.
    // This records the hierarchy.
    struct NodeInstance
    {
        int node_id{};
        std::optional<int> parent_node_instance_id; // Nullopt for root nodes
    };

    struct MeshInstance
    {
        int node_instance_id;
        int mesh_id;
    };

    struct Mesh
    {
        size_t prim_first;
        size_t prim_count;
    };

    struct Attribute
    {
        int accessor;
        std::optional<int> draco_attribute;
    };

    struct MaterialVariantsMapping
    {
        uint64_t variants_bitset{};
        std::optional<int> material;
    };

    struct Primitive
    {
        my::PrimitiveType mode;
        std::optional<int> material;
        std::optional<int> draco_buffer_view;
        std::vector<MaterialVariantsMapping> material_variants_mappings;

        std::optional<Attribute> indices;
        std::optional<Attribute> position;
        std::optional<Attribute> normal;
        std::optional<Attribute> color;
        std::optional<Attribute> uv[MaxUvCount];
        hrz::flat_hash_map<std::string, Attribute> extra_attributes;
    };

    struct Accessor
    {
        std::optional<int> buffer_view;
        size_t byte_offset{};
        size_t count{};
        my::VertexFormat type{};
        lm::dvec4 min;
        lm::dvec4 max;
    };

    struct BufferView
    {
        int buffer;
        size_t byte_stride;
        size_t byte_offset;
        size_t byte_length;
    };

    struct Buffer
    {
        std::optional<BlobLibrary::Handle> blob;
        size_t byte_length{};
    };

    struct Sampler
    {
        bool use_mipmap{};
        my::SamplerParams::Wrap wrap_s{};
        my::SamplerParams::Wrap wrap_t{};
        my::SamplerParams::Filter min_filter{};
        my::SamplerParams::Filter mag_filter{};
        std::optional<my::SamplerParams::Filter> mipmap_min_filter;
    };

    struct Image
    {
        std::optional<int> buffer_view;
        std::optional<BlobLibrary::Handle> blob;
    };

    struct Texture
    {
        std::optional<int> source;
        std::optional<int> sampler;
        std::optional<hrz_proto::ImageFormat> data_intepretation;
    };

    enum class AlphaMode
    {
        Opaque = 0,
        Mask = 1,
        Blend = 2
    };

    struct NoMaterial
    {
    };

    struct DiffuseMaterial
    {
        lm::vec4 color_factor;
        std::optional<int> color_texture;
        int uv_set;
    };

    struct DataMaterial
    {
        std::optional<int> data_texture;
        int uv_set;
    };

    struct Material
    {
        AlphaMode alpha_mode = AlphaMode::Opaque;
        float alpha_cutoff = 0.5;
        bool double_sided = false;
        bool unlit = false;

        std::variant<NoMaterial, DiffuseMaterial, DataMaterial> material;
    };

    struct AnimationChannel
    {
        int sampler;
        int target_node;
        AnimationTargetProperty target_property;
    };

    struct AnimationSampler
    {
        int timestamp_accessor;
        int value_accessor;
        AnimationInterpolation interpolation;
    };

    struct Animation
    {
        std::string name;
        std::vector<AnimationChannel> channels;
        std::vector<AnimationSampler> samplers;
    };

    AttributionHandle attribution;
    lm::dmat4 root_transform;
    std::optional<BlobLibrary::Handle> embedded_resources;
    std::vector<Node> nodes;
    // Node instances always have parent appearing before children.
    std::vector<NodeInstance> node_instances;
    std::vector<MeshInstance> mesh_instances;
    std::vector<Mesh> meshes;
    std::vector<Primitive> primitives;
    std::vector<Accessor> accessors;
    std::vector<BufferView> buffer_views;
    std::vector<Buffer> buffers;
    std::vector<Sampler> samplers;
    std::vector<Image> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Animation> animations;
    hrz::flat_hash_map<std::string, int> material_variants; // Name -> index
};

class BlobLibrary;

uint32_t fetch_glb_declared_size(std::span<const std::byte> gltf_data);

bool parse_gltf_descriptor(
    std::string_view descriptor_url,
    AttributionHandle additional_attribution,
    size_t descriptor_offset,
    const blobs::BlobHandle& json_blob,
    BlobAllocator*,
    BlobLibrary*,
    AttributionRegistry*,
    uint32_t buffers_priority,
    uint32_t textures_priority,
    ModelDescriptor* parse_into);

} // namespace hrz::model
