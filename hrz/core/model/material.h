// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/palette.h"
#include "hrz/core/model/common.h"
#include "hrz/core/model/descriptor.h"
#include "hrz/core/model/model.h"
#include "hrz/core/model/resources/resource.h"
#include "hrz/protocol/3d_model/material.pb.h"

namespace hrz::model
{

struct ModelPrototype;

class ModelMaterial
{
public:
    enum class Status
    {
        Loading,
        Displayable,
        Ready,
        ReadyWithErrors,
    };

    struct Primitive
    {
        my::VertexInputStream uv_stream;
        my::VertexInputStream compressed_uv_stream;

        VertexCompressionParamsUniformData uv_compression;

        std::optional<int> texture_to_load;
        bool is_data_texture = false;
        std::optional<int> sampler_to_load;
        bool use_mipmaps = true;

        lm::vec4 color = lm::vec4(1, 1, 1, 1);
        bool double_sided = false;
        ModelDescriptor::AlphaMode alpha_mode = ModelDescriptor::AlphaMode::Opaque;
        float alpha_cutoff = 0.5F;
        bool unlit = false;

        my::ResourceHandle texture;
        my::ResourceHandle sampler;
    };

private:
    enum class LoadStatus
    {
        Loading,
        Loaded,
        LoadedWithErrors,
    };

    bool _initialized = false;
    LoadStatus _textures_status = LoadStatus::Loading;
    LoadStatus _geometries_status = LoadStatus::Loading;
    size_t _primitive_revision = 1; // Used to detect progressive loading advances
    size_t _mesh_revision = 1;      // Used to detect palette changes

    UsedResources<int> _used_vertex_buffers;
    UsedResources<int> _used_draco_meshes;
    UsedResources<SamplerWithParams> _used_samplers;
    UsedResources<TextureWithCfg> _used_textures;

    std::vector<Primitive> _primitives;

    hrz_proto::Material _material;
    Palette _palette;

    std::optional<int> _variant_index;
    BlobLibrary::ConfigH _cfg;

    void initialize(ModelPrototype*);

    const ModelDescriptor::Attribute* get_uv_attribute(
        ModelPrototype*,
        const ModelDescriptor::Primitive&);

    RenderRequest build(ModelPrototype*, SharedResources*, Render*);

    LoadStatus build_primitive_uv(
        ModelPrototype* proto,
        SharedResources* sr,
        const ModelDescriptor::Primitive& desc_primitive,
        Primitive& primitive);

public:
    explicit ModelMaterial(const hrz_proto::Material&);
    void destroy(ModelPrototype*);

    void update_data_texture_palette(const hrz_proto::NumericPalette& palette);

    constexpr Status status() const
    {
        if (!_initialized) return Status::Loading;

        if (_geometries_status == LoadStatus::Loaded)
        {
            if (_textures_status == LoadStatus::Loaded)
            {
                return Status::Ready;
            }
            else if (_textures_status == LoadStatus::LoadedWithErrors)
            {
                return Status::ReadyWithErrors;
            }
            else
            {
                return Status::Displayable;
            }
        }
        else if (_geometries_status == LoadStatus::LoadedWithErrors)
        {
            if (_textures_status == LoadStatus::Loaded
                || _textures_status == LoadStatus::LoadedWithErrors)
            {
                return Status::ReadyWithErrors;
            }
            else
            {
                return Status::Displayable;
            }
        }

        return Status::Loading;
    }

    constexpr size_t primitive_revision() const { return _primitive_revision; }

    constexpr size_t mesh_revision() const { return _mesh_revision; }

    Primitive& get_primitive(size_t i) { return _primitives[i]; }

    const Palette& get_palette() { return _palette; }

    std::array<my::VertexInputStream, 2> get_streams(const Primitive&, int material_index);

    static std::array<my::VertexInputStream, 2> get_default_streams(
        const SharedResources* sr,
        int material_index);

    void work(ModelPrototype*);
    RenderRequest work_gpu(ModelPrototype*, SharedResources*, Render*);
};

} // namespace hrz::model
