// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/three_d_tiles/model_cache.h"

#include "hrz/common/blob_allocator.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/base_url.h"
#include "hrz/core/job_scheduler.h"
#include "hrz/core/model/model.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/log.h"
#include "hrz/protocol/3d_model/material.pb.h"

#include <array>
#include <cassert>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{

static uint64_t make_model_uri_hash(std::string_view uri, const hrz::HttpHeaders& headers)
{
    return hrz::hash_mix(hrz::murmur3_x64_64(uri), headers.hash_content());
}

} // anonymous namespace

namespace hrz::three_d_tiles
{

uint64_t ModelCache::ref(
    std::string_view uri,
    const hrz::BaseUrl& base_url,
    const hrz::HttpHeaders& headers,
    hrz::AttributionHandle additional_attribution,
    hrz::assets_loader::Queue request_queue,
    uint32_t request_priority,
    hrz::monitoring::ResourceOwner resource_owner)
{
    const auto hash = make_model_uri_hash(uri, headers);

    const auto it = _model_uris_to_prototypes.find(hash);
    if (it == std::end(_model_uris_to_prototypes))
    {
        ModelPrototypeRef proto_ref;
        proto_ref.status = ModelStatus::NEW;

        proto_ref.uri = uri;
        proto_ref.headers = headers;
        proto_ref.base_url = base_url;
        proto_ref.data_offset = 0;
        proto_ref.additional_attribution = additional_attribution;
        proto_ref.load_queue = request_queue;
        proto_ref.loading_priority = request_priority;
        proto_ref.resource_owner = resource_owner;

        proto_ref.ref_count = 1;

        _model_uris_to_prototypes.emplace(hash, std::move(proto_ref));
    }
    else
    {
        auto& proto_ref = it->second;

        if (request_priority > proto_ref.loading_priority)
        {
            proto_ref.loading_priority = request_priority;
            proto_ref.load_queue = request_queue;
        }

        proto_ref.ref_count += 1;
    }

    return hash;
}

void ModelCache::unref(uint64_t hash)
{
    const auto it = _model_uris_to_prototypes.find(hash);
    if (it != std::end(_model_uris_to_prototypes))
    {
        auto& proto_ref = it->second;

        assert(proto_ref.ref_count > 0);
        proto_ref.ref_count -= 1;
    }
    else
    {
        assert(false && "Could not find shared model in the cache");
    }
}

std::optional<ModelCache::ModelStatus> ModelCache::get_model_status(uint64_t hash) const
{
    const auto it = _model_uris_to_prototypes.find(hash);
    if (it == std::end(_model_uris_to_prototypes))
    {
        return std::nullopt;
    }
    return it->second.status;
}

std::optional<ModelCache::ModelParts> ModelCache::get_model_parts(uint64_t hash) const
{
    const auto it = _model_uris_to_prototypes.find(hash);
    if (it == std::end(_model_uris_to_prototypes))
    {
        return std::nullopt;
    }
    return it->second.parts;
}

void ModelCache::work(
    hrz::AssetsLoader* al,
    hrz::JobScheduler* js,
    hrz::BlobAllocator* ba,
    hrz::ImageDecoder* imgdec,
    hrz::AttributionRegistry* attributions,
    std::vector<my::ResourceHandle>& to_destroy)
{
    for (auto it = std::begin(_model_uris_to_prototypes);
         it != std::end(_model_uris_to_prototypes);)
    {
        auto& proto_ref = it->second;

        if (proto_ref.ref_count == 0)
        {
            if (proto_ref.status == ModelStatus::LOADING)
            {
                hrz::assets_loader::end(al, proto_ref.load_external_gltf_ticket);
            }
            else if (proto_ref.status == ModelStatus::LOADED)
            {
                hrz::model::destroy(proto_ref.parts.prototype, proto_ref.parts.geometry);
                hrz::model::destroy(proto_ref.parts.prototype, proto_ref.parts.model);
                hrz::model::destroy(proto_ref.parts.prototype, proto_ref.parts.material);
                hrz::model::destroy(proto_ref.parts.prototype, al, js, ba, to_destroy);
            }

            _model_uris_to_prototypes.erase(it++);
            continue;
        }

        if (proto_ref.status == ModelStatus::NEW)
        {
            proto_ref.load_external_gltf_ticket = hrz::assets_loader::begin(
                al, proto_ref.uri, proto_ref.headers, proto_ref.load_queue,
                proto_ref.loading_priority, proto_ref.resource_owner);
            proto_ref.status = ModelStatus::LOADING;
        }
        else if (proto_ref.status == ModelStatus::LOADING)
        {
            if (!hrz::assets_loader::is_valid(al, proto_ref.load_external_gltf_ticket))
            {
                HRZ_LOG_ERROR("Error: Could not download external glTF.");
                proto_ref.status = ModelStatus::ERROR;
            }
            else if (hrz::assets_loader::is_finished(al, proto_ref.load_external_gltf_ticket))
            {
                if (hrz::assets_loader::get_status(al, proto_ref.load_external_gltf_ticket)
                    == hrz::assets_loader::RequestStatus::Loaded)
                {
                    const auto gltf_blob =
                        hrz::assets_loader::get_blob(al, ba, proto_ref.load_external_gltf_ticket);
                    proto_ref.parts.prototype = hrz::model::create_from_gltf_blob(
                        ba, attributions, gltf_blob, proto_ref.uri, proto_ref.base_url,
                        proto_ref.data_offset, proto_ref.headers, proto_ref.additional_attribution,
                        proto_ref.load_queue, proto_ref.loading_priority, proto_ref.resource_owner);
                    proto_ref.parts.geometry =
                        create_instanced_model_geometry(proto_ref.parts.prototype);
                    proto_ref.parts.material =
                        hrz::model::create_model_material(proto_ref.parts.prototype, {});
                    proto_ref.parts.model = hrz::model::create_baked_model(
                        proto_ref.parts.prototype, proto_ref.parts.geometry,
                        proto_ref.parts.material);

                    proto_ref.status = ModelStatus::LOADED;
                }
                else
                {
                    HRZ_LOG_ERROR("Error: Could not load external glTF \"{}\"", proto_ref.uri);

                    proto_ref.status = ModelStatus::ERROR;
                }

                hrz::assets_loader::end(al, proto_ref.load_external_gltf_ticket);
            }
            else if (proto_ref.status == ModelStatus::LOADED)
            {
                hrz::model::work(proto_ref.parts.prototype, al, js, ba, imgdec, attributions);
            }
        }

        it++;
    }
}

void ModelCache::destroy(
    hrz::AssetsLoader* al,
    hrz::JobScheduler* js,
    hrz::BlobAllocator* ba,
    std::vector<my::ResourceHandle>& to_destroy)
{
    for (const auto& [_, proto] : _model_uris_to_prototypes)
    {
        if (proto.parts.prototype)
        {
            hrz::model::destroy(proto.parts.prototype, al, js, ba, to_destroy);
        }
    }
}

} // namespace hrz::three_d_tiles
