#include "model/hrz_core_model_blob_library.h"
#include "model/hrz_core_model_descriptor.h"

#include <hrz_common_profiling.h>
#include <hrz_fnd_json_utils.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_string_utils.h>

#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include <rapidjson/error/en.h>
#include <rapidjson/memorystream.h>

namespace
{

using namespace hrz;
using namespace hrz::model;

static constexpr const char* s_supported_extensions[] = {
    "KHR_draco_mesh_compression", "EXT_texture_webp",    "KHR_texture_basisu",
    "KHR_materials_variants",     "KHR_materials_unlit", "SIRADEL_templated_image_url",
    "SIRADEL_data_texture",       "CESIUM_RTC"};
static constexpr size_t MAX_MATERIAL_VARIANTS = 64;

enum GltfExtension
{
    KHR_draco_mesh_compression = 0,
    EXT_texture_webp,
    KHR_texture_basisu,
    KHR_materials_variants,
    KHR_materials_unlit,
    SIRADEL_templated_image_url,
    SIRADEL_data_texture,
    CESIUM_RTC,

    SupportedGltfExtensionCount
};

const rapidjson::Value& _get_gltf_extension(const rapidjson::Value& json, GltfExtension ext)
{
    const auto& extensions_json = json::get_member_or_null(json, "extensions");
    if (extensions_json.IsObject())
    {
        const auto& extension_json =
            json::get_member_or_null(extensions_json, s_supported_extensions[ext]);
        if (extension_json.IsObject())
        {
            return extension_json;
        }
    }
    return json::NullValue;
}

void _parse_gltf_node(
    const rapidjson::Value& nodes_json,
    int node_id,
    const lm::dmat4& parent_transform,
    ModelDescriptor* descriptor)
{
    const auto& node_json = json::get_nth_or_null(nodes_json, node_id);
    if (node_json.IsNull() || !node_json.IsObject())
    {
        return;
    }

    const auto& matrix_json = json::get_member_or_null(node_json, "matrix");
    const auto& rotation_json = json::get_member_or_null(node_json, "rotation");
    const auto& scale_json = json::get_member_or_null(node_json, "scale");
    const auto& translation_json = json::get_member_or_null(node_json, "translation");

    lm::dmat4 local_transform = lm::dmat4::identity();

    // Extract the transform
    if (matrix_json.IsArray())
    {
        json::copy_array_values(gsl::span<double>(local_transform.e), matrix_json);
    }
    else
    {
        lm::dvec3 scale(1, 1, 1);
        lm::dvec3 translation;
        lm::dquat rotation;

        if (scale_json.IsArray())
        {
            json::copy_array_values(gsl::span<double>(scale.m), scale_json);
        }

        if (translation_json.IsArray())
        {
            json::copy_array_values(gsl::span<double>(translation.m), translation_json);
        }

        if (rotation_json.IsArray())
        {
            json::copy_array_values(gsl::span<double>(rotation.m), rotation_json);
        }

        local_transform =
            lm::translation(translation) * lm::rotation_normalized(rotation) * lm::scaling(scale);
    }

    const lm::dmat4 transform = parent_transform * local_transform;

    // Parse the children
    const auto& children_json = json::get_member_or_null(node_json, "children");
    if (children_json.IsArray())
    {
        for (const auto& child_json : children_json.GetArray())
        {
            if (!child_json.IsNumber()) continue;
            auto child_node_id = child_json.GetInt();
            _parse_gltf_node(nodes_json, child_node_id, transform, descriptor);
        }
    }

    if (const int mesh_id = json::get_int_or(node_json, "mesh", -1); mesh_id >= 0)
    {
        descriptor->mesh_instances.push_back(ModelDescriptor::MeshInstance{transform, mesh_id});
    }
}

ModelDescriptor::Attribute* _get_primitive_attribute(
    ModelDescriptor::Primitive* prim,
    const char* name)
{
    auto get_or_insert_optional_attribute =
        [&](std::optional<ModelDescriptor::Attribute>* opt_attr) -> ModelDescriptor::Attribute*
    {
        if (!opt_attr->has_value())
        {
            opt_attr->emplace(ModelDescriptor::Attribute{});
        }
        return &opt_attr->value();
    };

    if (std::strcmp(name, "POSITION") == 0)
    {
        return get_or_insert_optional_attribute(&prim->position);
    }
    else if (std::strcmp(name, "NORMAL") == 0)
    {
        return get_or_insert_optional_attribute(&prim->normal);
    }
    else if (std::strcmp(name, "COLOR_0") == 0)
    {
        return get_or_insert_optional_attribute(&prim->color);
    }
    else if (std::strcmp(name, "TEXCOORD_0") == 0)
    {
        return get_or_insert_optional_attribute(&prim->uv[0]);
    }
    else if (std::strcmp(name, "TEXCOORD_1") == 0)
    {
        return get_or_insert_optional_attribute(&prim->uv[1]);
    }
    else if (std::strcmp(name, "TEXCOORD_2") == 0)
    {
        return get_or_insert_optional_attribute(&prim->uv[2]);
    }
    else if (std::strcmp(name, "TEXCOORD_3") == 0)
    {
        return get_or_insert_optional_attribute(&prim->uv[3]);
    }
    else
    {
        return &prim->extra_attributes[name];
    }
}

void _parse_gltf_primitive(const rapidjson::Value& prim_json, ModelDescriptor* descriptor)
{
    if (!prim_json.IsObject()) return;

    ModelDescriptor::Primitive prim;

    switch (int mode = json::get_int_or(prim_json, "mode", 4))
    {
        case 0: prim.mode = my::PrimitiveType::PointList; break;
        case 1: prim.mode = my::PrimitiveType::LineList; break;
        case 2: prim.mode = my::PrimitiveType::LineLoop; break;
        case 3: prim.mode = my::PrimitiveType::LineStrip; break;
        case 4: prim.mode = my::PrimitiveType::TriangleList; break;
        case 5: prim.mode = my::PrimitiveType::TriangleStrip; break;
        case 6: prim.mode = my::PrimitiveType::TriangleFan; break;
        default: HRZ_LOG_ERROR("Primitive mode {} unknown", mode); return;
    }

    prim.material = json::get_int(prim_json, "material");

    const auto& materials_variants_ext = _get_gltf_extension(prim_json, KHR_materials_variants);
    if (materials_variants_ext.IsObject())
    {
        const auto& mappings_json = json::get_member_or_null(materials_variants_ext, "mappings");
        if (mappings_json.IsArray())
        {
            for (const auto& mapping_json : mappings_json.GetArray())
            {
                if (!mapping_json.IsObject()) continue;

                ModelDescriptor::MaterialVariantsMapping mapping;
                mapping.material = json::get_int(mapping_json, "material");
                mapping.variants_bitset = 0;

                const auto& variants_json = json::get_member_or_null(mapping_json, "variants");
                if (!variants_json.IsArray()) continue;

                for (const auto& variant_json : variants_json.GetArray())
                {
                    if (!variant_json.IsInt()) continue;
                    const int variant_index = variant_json.GetInt();

                    if (variant_index >= 0 && (size_t)variant_index < MAX_MATERIAL_VARIANTS)
                    {
                        mapping.variants_bitset |= ((uint64_t)1 << variant_index);
                    }
                }

                if (mapping.variants_bitset != 0)
                {
                    prim.material_variants_mappings.push_back(mapping);
                }
            }
        }
    }

    if (auto indices_opt = json::get_int(prim_json, "indices"))
    {
        prim.indices = ModelDescriptor::Attribute{indices_opt.value(), std::nullopt};
    }

    const auto& attribs_json = json::get_member_or_null(prim_json, "attributes");
    if (attribs_json.IsObject())
    {
        for (const auto& attrib : attribs_json.GetObject())
        {
            const auto& name = attrib.name.GetString();

            if (!attrib.value.IsInt())
            {
                continue;
            }

            auto* attribute_ptr = _get_primitive_attribute(&prim, name);
            attribute_ptr->accessor = attrib.value.GetInt();
        }
    }

    const auto& draco_json = _get_gltf_extension(prim_json, KHR_draco_mesh_compression);
    if (draco_json.IsObject() && (prim.draco_buffer_view = json::get_int(draco_json, "bufferView")))
    {
        // This is a Draco-compressed mesh.
        // Get the mappings between attribute ids inside the Draco mesh and accessors.

        const auto& attribs_json = json::get_member_or_null(prim_json, "attributes");
        const auto& draco_attribs_json = json::get_member_or_null(draco_json, "attributes");

        if (attribs_json.IsObject() && draco_attribs_json.IsObject())
        {
            for (auto& attribute : draco_attribs_json.GetObject())
            {
                if (attribute.name.IsString() && attribute.value.IsNumber()
                    && attribs_json.HasMember(attribute.name.GetString()))
                {
                    auto attribute_name = attribute.name.GetString();
                    int draco_attribute_id = attribute.value.GetInt();

                    auto* attr_ptr = _get_primitive_attribute(&prim, attribute_name);
                    attr_ptr->draco_attribute = draco_attribute_id;
                }
            }
        }
    }

    descriptor->primitives.push_back(prim);
}

void _parse_gltf_mesh(const rapidjson::Value& mesh_json, ModelDescriptor* descriptor)
{
    const size_t prim_first = descriptor->primitives.size();

    const auto& primitives_json = json::get_member_or_null(mesh_json, "primitives");
    if (primitives_json.IsArray())
    {
        for (const auto& prim_json : primitives_json.GetArray())
        {
            _parse_gltf_primitive(prim_json, descriptor);
        }
    }

    const size_t prim_count = descriptor->primitives.size() - prim_first;
    descriptor->meshes.push_back(ModelDescriptor::Mesh{prim_first, prim_count});
}

my::VertexFormat _get_vertex_format(int type, const char* layout, bool normalized)
{
    constexpr uint32_t INT8 = 5120;
    constexpr uint32_t UINT8 = 5121;
    constexpr uint32_t INT16 = 5122;
    constexpr uint32_t UINT16 = 5123;
    constexpr uint32_t UINT32 = 5125;
    constexpr uint32_t FLOAT = 5126;

    if (strcmp(layout, "SCALAR") == 0)
    {
        switch (type)
        {
            case INT8: return normalized ? my::VertexFormat::Int8Norm : my::VertexFormat::Int8;
            case UINT8: return normalized ? my::VertexFormat::UInt8Norm : my::VertexFormat::UInt8;
            case INT16: return normalized ? my::VertexFormat::Int16Norm : my::VertexFormat::Int16;
            case UINT16:
                return normalized ? my::VertexFormat::UInt16Norm : my::VertexFormat::UInt16;
            case UINT32: return my::VertexFormat::UInt32;
            case FLOAT: return my::VertexFormat::Float32;
        }
    }
    else if (strcmp(layout, "VEC2") == 0)
    {
        switch (type)
        {
            case INT8: return normalized ? my::VertexFormat::Int8Norm_2 : my::VertexFormat::Int8_2;
            case UINT8:
                return normalized ? my::VertexFormat::UInt8Norm_2 : my::VertexFormat::UInt8_2;
            case INT16:
                return normalized ? my::VertexFormat::Int16Norm_2 : my::VertexFormat::Int16_2;
            case UINT16:
                return normalized ? my::VertexFormat::UInt16Norm_2 : my::VertexFormat::UInt16_2;
            case UINT32: return my::VertexFormat::UInt32_2;
            case FLOAT: return my::VertexFormat::Float32_2;
        }
    }
    else if (strcmp(layout, "VEC3") == 0)
    {
        switch (type)
        {
            case INT8: return normalized ? my::VertexFormat::Int8Norm_3 : my::VertexFormat::Int8_3;
            case UINT8:
                return normalized ? my::VertexFormat::UInt8Norm_3 : my::VertexFormat::UInt8_3;
            case INT16:
                return normalized ? my::VertexFormat::Int16Norm_3 : my::VertexFormat::Int16_3;
            case UINT16:
                return normalized ? my::VertexFormat::UInt16Norm_3 : my::VertexFormat::UInt16_3;
            case UINT32: return my::VertexFormat::UInt32_3;
            case FLOAT: return my::VertexFormat::Float32_3;
        }
    }
    else if (strcmp(layout, "VEC4") == 0)
    {
        switch (type)
        {
            case INT8: return normalized ? my::VertexFormat::Int8Norm_4 : my::VertexFormat::Int8_4;
            case UINT8:
                return normalized ? my::VertexFormat::UInt8Norm_4 : my::VertexFormat::UInt8_4;
            case INT16:
                return normalized ? my::VertexFormat::Int16Norm_4 : my::VertexFormat::Int16_4;
            case UINT16:
                return normalized ? my::VertexFormat::UInt16Norm_4 : my::VertexFormat::UInt16_4;
            case UINT32: return my::VertexFormat::UInt32_4;
            case FLOAT: return my::VertexFormat::Float32_4;
        }
    }
    // @Note We don't support Matrix attributes.
    //      -slerouzic, 24 jan 2019
    else
    {
        HRZ_LOG_WARNING("Layout {} not supported", layout);
        return my::VertexFormat::UInt8Norm;
    }

    HRZ_LOG_WARNING("Component type {} not supported", type);
    return my::VertexFormat::UInt8Norm;
}

void _parse_gltf_accessor(const rapidjson::Value& accessor_json, ModelDescriptor* descriptor)
{
    ModelDescriptor::Accessor accessor;

    if (accessor_json.IsObject())
    {
        accessor.buffer_view = json::get_int(accessor_json, "bufferView");
        accessor.byte_offset = json::get_int_or(accessor_json, "byteOffset", 0);
        accessor.count = json::get_int_or(accessor_json, "count", 0);

        accessor.type = _get_vertex_format(
            json::get_int_or(accessor_json, "componentType", 0),
            json::get_str_or(accessor_json, "type", ""),
            json::get_bool_or(accessor_json, "normalized", false));

        json::copy_array_values(
            gsl::span<double>(accessor.min.m), json::get_member_or_null(accessor_json, "min"));

        json::copy_array_values(
            gsl::span<double>(accessor.max.m), json::get_member_or_null(accessor_json, "max"));
    }

    descriptor->accessors.push_back(accessor);
}

void _parse_gltf_buffer_view(const rapidjson::Value& view_json, ModelDescriptor* descriptor)
{
    ModelDescriptor::BufferView view{};

    if (view_json.IsObject())
    {
        view.buffer = json::get_int_or(view_json, "buffer", 0);
        view.byte_stride = json::get_int_or(view_json, "byteStride", 0);
        view.byte_offset = json::get_int_or(view_json, "byteOffset", 0);
        view.byte_length = json::get_int_or(view_json, "byteLength", 0);
    }

    descriptor->buffer_views.push_back(view);
}

void _parse_gltf_buffer(
    const rapidjson::Value& buffer_json,
    BlobAllocator* ba,
    BlobLibrary* bl,
    uint32_t priority,
    ModelDescriptor* descriptor)
{
    ModelDescriptor::Buffer buffer;

    if (buffer_json.IsObject())
    {
        const std::string_view uri = json::get_str_or(buffer_json, "uri", "");
        if (uri.empty())
        {
            // We will use the embedded resources buffers here
            buffer.blob = std::nullopt;
        }
        else
        {
            buffer.blob = bl->add_blob_from_url(ba, uri, 0, priority);
        }

        buffer.byte_length = json::get_int_or(buffer_json, "byteLength", 0);
    }

    descriptor->buffers.push_back(buffer);
}

my::SamplerParams::Wrap _convert_sampler_wrap_mode(int mode)
{
    switch (mode)
    {
        case 33071: return my::SamplerParams::Wrap::Clamp;
        case 33648: return my::SamplerParams::Wrap::Mirror;
        case 10497:
        default: return my::SamplerParams::Wrap::Repeat;
    }
}

my::SamplerParams::Filter _convert_sampler_mag_filter(int filter)
{
    switch (filter)
    {
        case 9729: return my::SamplerParams::Filter::Linear;
        case 9728:
        default: return my::SamplerParams::Filter::Nearest;
    }
}

my::SamplerParams::Filter _convert_sampler_min_filter(int filter)
{
    switch (filter)
    {
        case 9729:
        case 9985:
        case 9987: return my::SamplerParams::Filter::Linear;
        case 9728:
        case 9984:
        case 9986:
        default: return my::SamplerParams::Filter::Nearest;
    }
}

std::optional<my::SamplerParams::Filter> _convert_sampler_mipmap_min_filter(int filter)
{
    switch (filter)
    {
        case 9987:
        case 9986: return my::SamplerParams::Filter::Linear;
        case 9984:
        case 9985: return my::SamplerParams::Filter::Nearest;
        case 9728:
        case 9729:
        default: return std::nullopt;
    }
}

bool _convert_sampler_use_mipmap(int filter)
{
    switch (filter)
    {
        case 9985:
        case 9987:
        case 9984:
        case 9986: return true;
        case 9729:
        case 9728:
        default: return false;
    }
}

void _parse_gltf_sampler(const rapidjson::Value& sampler_json, ModelDescriptor* descriptor)
{
    ModelDescriptor::Sampler sampler;

    if (sampler_json.IsObject())
    {
        sampler.wrap_s = _convert_sampler_wrap_mode(json::get_int_or(sampler_json, "wrapS", 10497));
        sampler.wrap_t = _convert_sampler_wrap_mode(json::get_int_or(sampler_json, "wrapT", 10497));
        sampler.mag_filter =
            _convert_sampler_mag_filter(json::get_int_or(sampler_json, "magFilter", 9729));

        const int min_filter = json::get_int_or(sampler_json, "minFilter", 9987);
        sampler.min_filter = _convert_sampler_min_filter(min_filter);
        sampler.mipmap_min_filter = _convert_sampler_mipmap_min_filter(min_filter);
        sampler.use_mipmap = _convert_sampler_use_mipmap(min_filter);
    }

    descriptor->samplers.push_back(sampler);
}

void _parse_gltf_image(
    const rapidjson::Value& image_json,
    BlobAllocator* ba,
    BlobLibrary* bl,
    uint32_t priority,
    ModelDescriptor* descriptor)
{
    ModelDescriptor::Image image;

    if (image_json.IsObject())
    {
        const auto& templated_json = _get_gltf_extension(image_json, SIRADEL_templated_image_url);
        if (templated_json.IsObject())
        {
            const std::string_view template_name =
                json::get_str_or(templated_json, "templateName", "");

            std::vector<std::pair<std::string_view, std::string_view>> params;
            const auto& params_json = json::get_member_or_null(templated_json, "parameters");
            if (params_json.IsObject())
            {
                for (const auto& member : params_json.GetObject())
                {
                    assert(member.name.IsString());
                    if (member.value.IsString())
                    {
                        params.emplace_back(member.name.GetString(), member.value.GetString());
                    }
                }
            }

            if (!template_name.empty())
            {
                image.blob =
                    bl->add_templated_blob_from_parameters(template_name, params, priority);
            }
            else
            {
                image.blob = std::nullopt;
            }
        }
        else
        {
            const std::string_view uri = json::get_str_or(image_json, "uri", "");
            if (!uri.empty())
            {
                image.blob = bl->add_blob_from_url(ba, uri, 0, priority);
            }
            else
            {
                image.blob = std::nullopt;
            }
            image.buffer_view = json::get_int(image_json, "bufferView");
        }
    }

    descriptor->images.push_back(image);
}

void _parse_gltf_texture(const rapidjson::Value& texture_json, ModelDescriptor* descriptor)
{
    ModelDescriptor::Texture texture;

    if (texture_json.IsObject())
    {
        texture.sampler = json::get_int(texture_json, "sampler");
        texture.source = json::get_int(texture_json, "source");

        const auto& ktx2_json = _get_gltf_extension(texture_json, KHR_texture_basisu);
        if (ktx2_json.IsObject())
        {
            auto ktx2_source = json::get_int(ktx2_json, "source");
            if (ktx2_source.has_value())
            {
                texture.source = ktx2_source;
            }
        }

        if (const auto& webp_json = _get_gltf_extension(texture_json, EXT_texture_webp);
            webp_json.IsObject())
        {
            auto webp_source = json::get_int(webp_json, "source");
            if (webp_source.has_value())
            {
                texture.source = webp_source;
            }
        }

        const auto& data_texture_json = _get_gltf_extension(texture_json, SIRADEL_data_texture);
        if (data_texture_json.IsObject())
        {
            auto data_interpretation = json::get_str(data_texture_json, "dataInterpretation");
            if (data_interpretation.has_value())
            {
                if (std::strcmp(data_interpretation.value(), "rgba8BitsToFloat") == 0)
                {
                    texture.data_intepretation = hrz_proto::ImageFormat::R_F32;
                }
                else if (std::strcmp(data_interpretation.value(), "silicium") == 0)
                {
                    // This is undocumented.
                    texture.data_intepretation = hrz_proto::ImageFormat::R_F32_SILICIUM;
                }
                else
                {
                    HRZ_LOG_WARNING("Unknown data interpretation: {}", data_interpretation.value());
                }
            }
        }
    }

    descriptor->textures.push_back(texture);
}

ModelDescriptor::AlphaMode _convert_alpha_mode(const char* str)
{
    if (std::strcmp(str, "MASK") == 0)
    {
        return ModelDescriptor::AlphaMode::Mask;
    }
    else if (std::strcmp(str, "BLEND") == 0)
    {
        return ModelDescriptor::AlphaMode::Blend;
    }
    else
    {
        return ModelDescriptor::AlphaMode::Opaque;
    }
}

void _parse_gltf_material(const rapidjson::Value& material_json, ModelDescriptor* descriptor)
{
    ModelDescriptor::Material material;

    if (material_json.IsObject())
    {
        material.alpha_mode =
            _convert_alpha_mode(json::get_str_or(material_json, "alphaMode", "OPAQUE"));
        material.alpha_cutoff = json::get_float_or(material_json, "alphaCutoff", 0.5F);
        material.double_sided = json::get_bool_or(material_json, "doubleSided", false);
        material.material = ModelDescriptor::NoMaterial{};

        const auto& pbr_json = json::get_member_or_null(material_json, "pbrMetallicRoughness");
        const auto& data_texture_json = _get_gltf_extension(material_json, SIRADEL_data_texture);

        if (pbr_json.IsObject())
        {
            ModelDescriptor::DiffuseMaterial diffuse_material;

            json::copy_array_values(
                gsl::span<float>(diffuse_material.color_factor.m),
                json::get_member_or_null(pbr_json, "baseColorFactor"), 1.0F);

            const auto& texture_info_json = json::get_member_or_null(pbr_json, "baseColorTexture");
            diffuse_material.color_texture = json::get_int(texture_info_json, "index");
            diffuse_material.uv_set = json::get_int_or(texture_info_json, "texCoord", 0);

            material.material = diffuse_material;
        }
        else if (data_texture_json.IsObject())
        {
            ModelDescriptor::DataMaterial data_material;

            const auto& texture_info_json =
                json::get_member_or_null(data_texture_json, "dataTexture");
            data_material.data_texture = json::get_int(texture_info_json, "index");
            data_material.uv_set = json::get_int_or(texture_info_json, "texCoord", 0);

            material.material = data_material;
            // @Todo(649) One day we'll want to get the alpha blend from the glTF model directly.
            // But right now it's always OPAQUE in the files, which is incompatible with the
            // requirements for MC3D.
            material.alpha_mode = ModelDescriptor::AlphaMode::Blend;
        }

        const auto& unlit_json = _get_gltf_extension(material_json, KHR_materials_unlit);
        if (!unlit_json.IsNull())
        {
            material.unlit = true;
        }
    }

    descriptor->materials.push_back(material);
}

bool _parse_gltf_json(
    AttributionHandle additional_attribution,
    const blobs::BlobHandle& json_blob,
    BlobAllocator* ba,
    BlobLibrary* bl,
    AttributionRegistry* attributions,
    uint32_t buffers_priority,
    uint32_t textures_priority,
    ModelDescriptor* descriptor)
{
    HRZ_SCOPED_SAMPLE("parse gltf json");

    rapidjson::Document root;
    {
        HRZ_SCOPED_SAMPLE("parse json document");
        auto json_data = json_blob.get_data();
        rapidjson::MemoryStream raw_stream((const char*)json_data.data(), json_data.size());
        rapidjson::EncodedInputStream<rapidjson::UTF8<char>, rapidjson::MemoryStream> stream(
            raw_stream);
        root.ParseStream(stream);
    }

    if (root.HasParseError())
    {
        HRZ_LOG_ERROR(
            "Error parsing JSON, Rapidjson error code {}",
            rapidjson::GetParseError_En(root.GetParseError()));
        return false;
    }

    hrz::InlinedVector<AttributionHandle, 8> all_attributions;
    if (additional_attribution)
    {
        all_attributions.push_back(additional_attribution);
    }

    const auto& copyright = json::get_nested_member_or_null(root, {"asset", "copyright"});
    if (copyright.IsString())
    {
        std::string_view copyright_str = hrz::str::trim_s(copyright.GetString());

        // Some providers (like Google) split attributions with semicolons and we're expected to
        // reassemble them later, because otherwise there is a lot of duplications. So we do that.
        while (!copyright_str.empty())
        {
            auto pair = hrz::str::split(copyright_str, ';');
            auto this_attribution = hrz::str::trim_s(pair.first);
            if (!this_attribution.empty())
            {
                all_attributions.push_back(
                    attribution::register_attribution(attributions, {this_attribution, ""}));
            }
            copyright_str = hrz::str::trim_s(pair.second);
        }
    }

    if (all_attributions.empty())
    {
        descriptor->attribution = {};
    }
    else if (all_attributions.size() == 1)
    {
        descriptor->attribution = all_attributions[0];
    }
    else
    {
        descriptor->attribution =
            attribution::register_attribution_group(attributions, all_attributions);
    }

    const auto& required_ext_json = json::get_member_or_null(root, "extensionsRequired");
    if (required_ext_json.IsArray())
    {
        for (const auto& ext_json : required_ext_json.GetArray())
        {
            if (!ext_json.IsString()) continue;

            const auto& ext_str = ext_json.GetString();

            auto found = std::find_if(
                std::begin(s_supported_extensions), std::end(s_supported_extensions),
                [&](const char* extension_str) -> bool
                { return std::strcmp(ext_str, extension_str) == 0; });

            if (found == std::end(s_supported_extensions))
            {
                HRZ_LOG_ERROR("Unsupported required glTF extension {}", ext_str);
                return false;
            }
        }
    }

    const auto& material_variants_ext = _get_gltf_extension(root, KHR_materials_variants);
    if (material_variants_ext.IsObject())
    {
        const auto& variants = json::get_member_or_null(material_variants_ext, "variants");
        if (variants.IsArray())
        {
            int variant_index = 0;
            for (const auto& variant : variants.GetArray())
            {
                std::optional<const char*> variant_name = json::get_str(variant, "name");
                if (variant_name.has_value())
                {
                    if ((size_t)variant_index < MAX_MATERIAL_VARIANTS)
                    {
                        descriptor->material_variants.insert(
                            std::make_pair(variant_name.value(), variant_index));
                    }
                    else
                    {
                        HRZ_LOG_WARNING(
                            "Too many material variants for model, variant {} ignored",
                            variant_name.value());
                    }
                }
                variant_index += 1;
            }
        }
    }

    lm::dmat4 root_transform = lm::dmat4::identity();
    const auto& cesium_rtc_ext = _get_gltf_extension(root, CESIUM_RTC);
    if (cesium_rtc_ext.IsObject())
    {
        const auto& center_json = json::get_member_or_null(cesium_rtc_ext, "center");
        if (center_json.IsArray())
        {
            lm::dvec3 rtc_center;
            json::copy_array_values(gsl::span<double>(rtc_center.m), center_json);
            root_transform = lm::translation(lm::dvec3{rtc_center.x, rtc_center.z, -rtc_center.y});
        }
    }

    const int scene_index = json::get_int_or(root, "scene", 0);
    const auto& scene_json = json::get_nth_member_or_null(root, "scenes", scene_index);
    const auto& nodes_json = json::get_member_or_null(root, "nodes");

    const auto& root_nodes_json = json::get_member_or_null(scene_json, "nodes");
    if (root_nodes_json.IsArray() && nodes_json.IsArray())
    {
        for (const auto& node_json : root_nodes_json.GetArray())
        {
            if (!node_json.IsNumber()) continue;
            const int node_id = node_json.GetInt();
            _parse_gltf_node(nodes_json, node_id, root_transform, descriptor);
        }
    }

    const auto& meshes_json = json::get_member_or_null(root, "meshes");
    if (meshes_json.IsArray())
    {
        for (const auto& mesh_json : meshes_json.GetArray())
        {
            _parse_gltf_mesh(mesh_json, descriptor);
        }
        assert(meshes_json.GetArray().Size() == descriptor->meshes.size());
    }

    const auto& accessors_json = json::get_member_or_null(root, "accessors");
    if (accessors_json.IsArray())
    {
        for (const auto& accessor_json : accessors_json.GetArray())
        {
            _parse_gltf_accessor(accessor_json, descriptor);
        }
        assert(accessors_json.GetArray().Size() == descriptor->accessors.size());
    }

    const auto& buffer_views_json = json::get_member_or_null(root, "bufferViews");
    if (buffer_views_json.IsArray())
    {
        for (const auto& buffer_view_json : buffer_views_json.GetArray())
        {
            _parse_gltf_buffer_view(buffer_view_json, descriptor);
        }
        assert(buffer_views_json.GetArray().Size() == descriptor->buffer_views.size());
    }

    const auto& buffers_json = json::get_member_or_null(root, "buffers");
    if (buffers_json.IsArray())
    {
        for (const auto& buffer_json : buffers_json.GetArray())
        {
            _parse_gltf_buffer(buffer_json, ba, bl, buffers_priority, descriptor);
        }
        assert(buffers_json.GetArray().Size() == descriptor->buffers.size());
    }

    const auto& samplers_json = json::get_member_or_null(root, "samplers");
    if (samplers_json.IsArray())
    {
        for (const auto& sampler_json : samplers_json.GetArray())
        {
            _parse_gltf_sampler(sampler_json, descriptor);
        }
        assert(samplers_json.GetArray().Size() == descriptor->samplers.size());
    }

    const auto& images_json = json::get_member_or_null(root, "images");
    if (images_json.IsArray())
    {
        for (const auto& image_json : images_json.GetArray())
        {
            _parse_gltf_image(image_json, ba, bl, textures_priority, descriptor);
        }
        assert(images_json.GetArray().Size() == descriptor->images.size());
    }

    const auto& textures_json = json::get_member_or_null(root, "textures");
    if (textures_json.IsArray())
    {
        for (const auto& texture_json : textures_json.GetArray())
        {
            _parse_gltf_texture(texture_json, descriptor);
        }
        assert(textures_json.GetArray().Size() == descriptor->textures.size());
    }

    const auto& materials_json = json::get_member_or_null(root, "materials");
    if (materials_json.IsArray())
    {
        for (const auto& material_json : materials_json.GetArray())
        {
            _parse_gltf_material(material_json, descriptor);
        }
        assert(materials_json.GetArray().Size() == descriptor->materials.size());
    }

    return true;
}

} // namespace

namespace hrz::model
{

uint32_t fetch_glb_declared_size(gsl::span<const std::byte> gltf_data)
{
    // @Endianness UInt32 reads are little-endian only.

    if (gltf_data.size() >= 12) // Enough room for the glb header
    {
        const gsl::span<const std::byte> gltf_header = gltf_data.subspan(0, 12);
        const gsl::span<const std::byte> magic = gltf_header.first(4);

        uint32_t version = 0;
        memcpy(&version, gltf_header.data() + 4, 4);

        if (memcmp((const char*)magic.data(), "glTF", 4) == 0 && version == 2)
        {
            // This is a glb.

            uint32_t declared_size = 0;
            memcpy(&declared_size, gltf_header.data() + 8, 4);

            return declared_size;
        }
    }

    return 0;
}

bool parse_gltf_descriptor(
    std::string_view descriptor_url,
    AttributionHandle additional_attribution,
    size_t descriptor_offset,
    const blobs::BlobHandle& gltf_blob,
    BlobAllocator* ba,
    BlobLibrary* bl,
    AttributionRegistry* attributions,
    uint32_t buffers_priority,
    uint32_t textures_priority,
    ModelDescriptor* descriptor)
{
    // @Endianness UInt32 reads are little-endian only.

    auto gltf_size = gltf_blob.data_size();
    if (gltf_size >= 12) // Enough room for the glb header
    {
        auto gltf_data = gltf_blob.get_data();
        const gsl::span<const std::byte> gltf_header = gltf_data.subspan(0, 12);
        const gsl::span<const std::byte> magic = gltf_header.first(4);

        uint32_t version = 0;
        memcpy(&version, gltf_header.data() + 4, 4);

        if (memcmp((const char*)magic.data(), "glTF", 4) == 0 && version == 2)
        {
            // This is a glb.

            uint32_t declared_size = 0;
            memcpy(&declared_size, gltf_header.data() + 8, 4);

            if (declared_size > gltf_size)
            {
                return false;
            }

            size_t current_offset = 12;
            size_t json_data_offset = 0;
            size_t json_data_length = 0;
            bool malformed_file = false;

            // Iterate through chunks.

            while (current_offset < gltf_size && !malformed_file)
            {
                if (gltf_size - current_offset >= 8)
                {
                    auto chunk_header = gltf_data.subspan(current_offset, 8);
                    current_offset += 8;

                    uint32_t chunk_length = 0;
                    memcpy(&chunk_length, chunk_header.data(), 4);

                    if (current_offset + chunk_length > gltf_size)
                    {
                        malformed_file = true;
                        continue;
                    }

                    auto chunk_type = chunk_header.subspan(4, 4);
                    if (memcmp((const char*)chunk_type.data(), "JSON", 4) == 0)
                    {
                        // JSON chunk
                        json_data_offset = current_offset;
                        json_data_length = chunk_length;
                    }
                    else if (memcmp((const char*)chunk_type.data(), "BIN\0", 4) == 0)
                    {
                        // Binary buffer chunk
                        auto embedded_resources =
                            blobs::make_sub_blob(ba, gltf_blob, current_offset, chunk_length);
                        const size_t embedded_resources_offset = descriptor_offset + current_offset;

                        descriptor->embedded_resources = bl->add_blob_from_url(
                            ba, descriptor_url, embedded_resources_offset, buffers_priority,
                            embedded_resources);
                    }

                    current_offset += chunk_length;
                }
                else
                {
                    break;
                }
            }

            if (!malformed_file && json_data_length > 0)
            {
                auto json_data_blob =
                    blobs::make_sub_blob(ba, gltf_blob, json_data_offset, json_data_length);
                return _parse_gltf_json(
                    additional_attribution, json_data_blob, ba, bl, attributions, buffers_priority,
                    textures_priority, descriptor);
            }
            else
            {
                return false;
            }
        }
        else
        {
            // Not a glb, try and parse the resource as a glTF JSON.
            return _parse_gltf_json(
                additional_attribution, gltf_blob, ba, bl, attributions, buffers_priority,
                textures_priority, descriptor);
        }
    }
    return false;
}
} // namespace hrz::model
