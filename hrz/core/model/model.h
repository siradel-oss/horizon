#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/picking_types.h"
#include "hrz/common/vector_data.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/attribution.h"
#include "hrz/core/base_url.h"
#include "hrz/core/model/ubo_defs.h"
#include "hrz/core/render.h"
#include "hrz/core/render_request.h"
#include "hrz/fnd/flat_hash_set.h"

#include <lin_maths.h>

#include <span>

/**
 * In Horizon, a 3D model comes from a glTF file and is made of multiple parts:
 *  - The model prototype is the in-memory representation of the 3D model.
 *    It is responsible for loading the buffers and textures and other resources
 *    when necessary.
 *  - The model geometry is a representation of the 3D model independent of materials.
 *    It contains all vertex data and sometimes some other things like feature colors
 *    for the batched representation.
 *    It is responsible for building and uploading those resources to the GPU.
 *  - The model material is what is applied on the geometry, mainly textures, gradients, etc.
 *    It contains the texture data and sometimes other things.
 *    It is responsible for building and uploading those resources to the GPU.
 *  - The baked model combines the geometry and material(s) to create a model that can
 *    finally be drawn.
 *  - Instance groups are used to instantiate a model on multiple points.
 *
 * All resources used by the geometries, materials, etc, are deduplicated by the prototype.
 *
 * There are multiple types of those resources:
 *  - Single: displays the model once, at a given location.
 *  - Instanced: instances the model multiple times, using an instance group.
 *  - Impostor baking: like single, but specific to building impostors.
 *  - Batched: used for 3D Tiles where feature IDs are associated per vertex.
 */

namespace hrz
{
struct ImageDecoder;
struct JobScheduler;
struct Render;

namespace model
{
struct SharedResources;
struct ModelPrototype;

SharedResources* create_shared_resources_single(Render*);
SharedResources* create_shared_resources_instanced(Render*);
SharedResources* create_shared_resources_batched(Render*);
void destroy_shared_resources(SharedResources*, Render*);

enum class ModelPrototypeStatus
{
    Loading,
    Ready,
    Error,
};

ModelPrototype* create_from_gltf_url(
    AssetsLoader*,
    BaseUrl url,
    const HttpHeaders&,
    AttributionHandle additional_attribution,
    assets_loader::Queue load_queue = assets_loader::Queue::MeshModels,
    uint32_t loading_priority = 0,
    monitoring::ResourceOwner resource_owner = {});

ModelPrototype* create_from_gltf_blob(
    BlobAllocator*,
    AttributionRegistry*,
    const blobs::BlobHandle& blob,
    std::string_view uri,
    BaseUrl base_url,
    size_t data_offset,
    const HttpHeaders&,
    AttributionHandle additional_attribution,
    assets_loader::Queue load_queue = assets_loader::Queue::MeshModels,
    uint32_t loading_priority = 0,
    monitoring::ResourceOwner resource_owner = {});

void destroy(
    ModelPrototype*,
    AssetsLoader* al,
    JobScheduler* js,
    BlobAllocator*,
    std::vector<my::ResourceHandle>& to_destroy);

void work(
    ModelPrototype*,
    AssetsLoader*,
    JobScheduler*,
    BlobAllocator*,
    ImageDecoder*,
    AttributionRegistry*);

void work_gpu(ModelPrototype*, BlobAllocator*, Render* render);
ModelPrototypeStatus get_status(ModelPrototype*);

// Returns true if the headers change could result in content negotiation differing from the
// previous headers.
bool update_http_headers(ModelPrototype*, const HttpHeaders&);

bool is_working(ModelPrototype*);

AttributionHandle get_attribution(ModelPrototype*);

struct ModelGeometryH
{
    uint64_t o;
};

struct SingleModelGeometryH
{
    uint64_t o;

    constexpr operator ModelGeometryH() const { return ModelGeometryH{o}; }
};

struct ImpostorBakingModelGeometryH
{
    uint64_t o;

    constexpr operator ModelGeometryH() const { return ModelGeometryH{o}; }
};

struct InstancedModelGeometryH
{
    uint64_t o;

    constexpr operator ModelGeometryH() const { return ModelGeometryH{o}; }
};

struct BatchedModelGeometryH
{
    uint64_t o;

    constexpr operator ModelGeometryH() const { return ModelGeometryH{o}; }
};

SingleModelGeometryH create_single_model_geometry(
    ModelPrototype*,
    const picking::ObjectReference& obj_ref,
    const picking::FeatureReference& feature_ref);
ImpostorBakingModelGeometryH create_impostor_baking_model_geometry(ModelPrototype*);
InstancedModelGeometryH create_instanced_model_geometry(ModelPrototype*);
BatchedModelGeometryH create_batched_model_geometry(
    ModelPrototype*,
    const picking::ObjectReference& obj_ref,
    const picking::FeatureReference& feature_ref,
    uint32_t batch_id_offset,
    size_t batch_length,
    std::span<const vector_data::FeatureIdHash> feature_id_hashes);
void destroy(ModelPrototype*, ModelGeometryH);

void set_batched_selection(
    ModelPrototype*,
    BatchedModelGeometryH,
    const hrz::flat_hash_set<vector_data::FeatureIdHash>& selected_objects);

void set_batched_colors(ModelPrototype*, BatchedModelGeometryH, std::span<const lm::ubvec4>);

struct ModelMaterialH
{
    uint64_t o;
};

ModelMaterialH create_model_material(ModelPrototype*, const hrz_proto::Material&);
void destroy(ModelPrototype*, ModelMaterialH);
void update_data_texture_palette(ModelPrototype*, ModelMaterialH, const hrz_proto::NumericPalette&);

struct InstanceGroupData
{
    bool use_enu_orientation;

    lm::dmat4 transform;
    my::CullModifier cull_modifier;
    std::span<const lm::vec3> positions;
    std::span<const lm::usvec3> compressed_positions;
    std::span<const lm::vec3> normals;
    std::span<const lm::usvec4> compressed_normals;
    std::span<const lm::vec3> scales;
    std::span<const lm::ubvec4> colors;

    std::span<const uint32_t> object_ids; // Used in the objet reference for picking.

    // Fill only one of them. If per object, we'll do the indirection with object_ids
    // during baking. If per instance, we just copy everything, no need for
    // any processing.
    // Those two methods are available so that upstream systems don't have to
    // allocate memory for the indirection if they don't have per instance IDs.
    std::span<const vector_data::FeatureIdHash> feature_id_per_object;
    std::span<const vector_data::FeatureIdHash> feature_id_per_instance;

    VertexCompressionParamsUniformData position_compression;
    VertexCompressionParamsUniformData normal_compression;
};

struct InstanceGroupH
{
    uint64_t o;
};

enum class InstanceGroupStatus
{
    Loading,
    Ready,
    Error,
};

InstanceGroupH create_instance_group(
    ModelPrototype*,
    const picking::ObjectReference& obj_ref,
    const picking::FeatureReference& feature_ref,
    uint32_t object_id_offset,
    const InstanceGroupData&);
void destroy(ModelPrototype*, InstanceGroupH);

void set_instance_group_colors(ModelPrototype*, InstanceGroupH, std::span<const lm::ubvec4>);
void set_instance_group_selection(
    ModelPrototype*,
    InstanceGroupH,
    const hrz::flat_hash_set<uint64_t>& selected_objects);

InstanceGroupStatus get_instance_group_status(ModelPrototype*, InstanceGroupH);

struct BakedModelH
{
    uint64_t o;
};

struct SingleBakedModelH
{
    uint64_t o;

    constexpr operator BakedModelH() const { return BakedModelH{o}; }
};

struct ImpostorBakingBakedModelH
{
    uint64_t o;

    constexpr operator BakedModelH() const { return BakedModelH{o}; }
};

struct InstancedBakedModelH
{
    uint64_t o;

    constexpr operator BakedModelH() const { return BakedModelH{o}; }
};

struct BatchedBakedModelH
{
    uint64_t o;

    constexpr operator BakedModelH() const { return BakedModelH{o}; }
};

SingleBakedModelH create_baked_model(
    ModelPrototype*,
    SingleModelGeometryH,
    std::optional<ModelMaterialH> base_material,
    std::optional<ModelMaterialH> overlay_material);

ImpostorBakingBakedModelH create_baked_model(
    ModelPrototype*,
    ImpostorBakingModelGeometryH,
    std::optional<ModelMaterialH>);

InstancedBakedModelH create_baked_model(
    ModelPrototype*,
    InstancedModelGeometryH,
    std::optional<ModelMaterialH>);

BatchedBakedModelH create_baked_model(
    ModelPrototype*,
    BatchedModelGeometryH,
    std::optional<ModelMaterialH> base_material,
    std::optional<ModelMaterialH> overlay_material);

void destroy(ModelPrototype*, BakedModelH);

void work(ModelPrototype*, BakedModelH);

void set_animations(ModelPrototype*, BakedModelH, std::span<const std::string>);

[[nodiscard]] RenderRequest work_gpu(ModelPrototype*, BakedModelH, SharedResources*, Render*);

// Expensive!
BSphere<double> compute_model_bsphere(ModelPrototype*, BakedModelH, const lm::dmat4& transform);

enum class BakedModelStatus
{
    Loading,
    Displayable,
    Ready,
    ReadyWithErrors,
    Error,
};

BakedModelStatus get_status(ModelPrototype*, BakedModelH);
bool is_working(ModelPrototype*, BakedModelH);

void work_gpu(ModelPrototype*, InstanceGroupH, Render*);

struct DrawProperties
{
    lm::dmat4 transform;
    float animation_speed;
    float animation_phase;
    render::LightingSettings lighting;
    int clip_id;
    lm::vec4 color;
    uint32_t feature_color_blend_mode;
    float feature_color_blend_strength;
    bool overlay_material_enabled;
    bool apply_feature_color_to_overlay;
    float overlay_material_opacity;
    bool draw_under_flat_overlays;
};

void draw(
    ModelPrototype*,
    SingleBakedModelH,
    const DrawProperties&,
    bool selected,
    uint32_t scene_views,
    SharedResources*,
    Render*,
    AttributionRegistry*);

void draw(
    ModelPrototype*,
    ImpostorBakingBakedModelH,
    const DrawProperties&,
    const lm::mat4& proj,
    const lm::mat4& view,
    SharedResources*,
    Render*,
    AttributionRegistry*);

void draw(
    ModelPrototype*,
    InstancedBakedModelH,
    InstanceGroupH,
    const DrawProperties&,
    uint32_t scene_views,
    SharedResources*,
    Render*,
    AttributionRegistry*);

void draw(
    ModelPrototype*,
    BatchedBakedModelH,
    const DrawProperties&,
    uint32_t scene_views,
    SharedResources*,
    Render*,
    AttributionRegistry*);

} // namespace model
} // namespace hrz
