// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/monitoring_defs.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/model/model.h"
#include "hrz/fnd/flat_hash_map.h"

#include <cassert>
#include <optional>
#include <string>
#include <vector>

namespace hrz
{

struct JobScheduler;
struct BlobAllocator;

} // namespace hrz

namespace hrz::three_d_tiles
{

// Cached model prototypes for i3dm external models.
class ModelCache
{
public:
    enum class ModelStatus
    {
        NEW,
        LOADING,
        LOADED,
        ERROR
    };

    struct ModelParts
    {
        hrz::model::ModelPrototype* prototype = nullptr;
        hrz::model::InstancedBakedModelH model;
        hrz::model::InstancedModelGeometryH geometry;
        hrz::model::ModelMaterialH material;
    };

private:
    struct ModelPrototypeRef
    {
        ModelStatus status = ModelStatus::ERROR;

        std::string uri;
        hrz::HttpHeaders headers;
        hrz::BaseUrl base_url;
        size_t data_offset;
        hrz::AttributionHandle additional_attribution;
        hrz::assets_loader::Queue load_queue;
        uint32_t loading_priority;
        hrz::monitoring::ResourceOwner resource_owner;

        hrz::assets_loader::Ticket load_external_gltf_ticket;
        ModelParts parts;

        uint32_t ref_count = 0;
    };

    hrz::flat_hash_map<uint64_t, ModelPrototypeRef> _model_uris_to_prototypes;

public:
    uint64_t ref(
        std::string_view uri,
        const hrz::BaseUrl& base_url,
        const hrz::HttpHeaders& headers,
        hrz::AttributionHandle additional_attribution,
        hrz::assets_loader::Queue request_queue,
        uint32_t request_priority,
        hrz::monitoring::ResourceOwner resource_owner);

    void unref(uint64_t hash);

    std::optional<ModelCache::ModelStatus> get_model_status(uint64_t hash) const;

    std::optional<ModelParts> get_model_parts(uint64_t hash) const;

    void work(
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba,
        hrz::ImageDecoder* imgdec,
        hrz::AttributionRegistry* attributions,
        std::vector<my::ResourceHandle>& to_destroy);

    void destroy(
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba,
        std::vector<my::ResourceHandle>& to_destroy);
};

} // namespace hrz::three_d_tiles
