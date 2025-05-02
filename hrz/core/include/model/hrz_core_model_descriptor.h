#pragma once

#include "hrz_core_attribution.h"
#include "model/hrz_core_model_blob_library.h"

#include <hrz_common_blob_allocator.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_inlined_vector.h>
#include <hrz_fnd_variant.h>

#include <mycelium_backend.h>

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

    struct MeshInstance
    {
        lm::dmat4 transform;
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
        uint64_t variants_bitset;
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
        size_t byte_offset;
        size_t count;
        my::VertexFormat type;
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
        size_t byte_length;
    };

    struct Sampler
    {
        bool use_mipmap;
        my::SamplerParams::Wrap wrap_s;
        my::SamplerParams::Wrap wrap_t;
        my::SamplerParams::Filter min_filter;
        my::SamplerParams::Filter mag_filter;
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

    AttributionHandle attribution;
    std::optional<BlobLibrary::Handle> embedded_resources;
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
    hrz::flat_hash_map<std::string, int> material_variants; // Name -> index
};

class BlobLibrary;

uint32_t fetch_glb_declared_size(gsl::span<const std::byte> gltf_data);

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
