#include "hrz/core/mapbox_translation.h"

#include "hrz/common/blob_allocator.h"
#include "hrz/common/tickets.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/client_message_queue.h"
#include "hrz/core/client_messages.h"
#include "hrz/core/scene.h"
#include "hrz/core/scene_model.h"
#include "hrz/fnd/json_utils.h"
#include "hrz/fnd/log.h"
#include "hrz/mapbox/translate.h"
#include "hrz/protocol/path_builder.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <optional>
#include <vector>

namespace hrz
{
namespace
{
struct Translation
{
    mapbox::TranslationTicket ticket;
    hrz_proto::MapboxTranslationParams params;

    assets_loader::Ticket sprite_load_ticket;
};

} // namespace

struct MapboxTranslationSystem
{
    TicketGenerator<mapbox::TranslationTicket> ticket_generator;
    std::vector<Translation> translations;
};

namespace mapbox
{
MapboxTranslationSystem* create_translation_system()
{
    return new MapboxTranslationSystem();
}

void destroy_translation_system(MapboxTranslationSystem* sys)
{
    delete sys;
}

TranslationTicket begin_translation(
    MapboxTranslationSystem* sys,
    AssetsLoader* al,
    ClientMessageQueue* mq,
    const hrz_proto::MapboxTranslationParams& params)
{
    if (params.destroy_existing_layers())
    {
        for (auto& translation : sys->translations)
        {
            if (assets_loader::is_valid(al, translation.sprite_load_ticket))
            {
                assets_loader::end(al, translation.sprite_load_ticket);
            }
        }
        sys->translations.clear();
    }

    TranslationTicket ticket = sys->ticket_generator.generate();

    rapidjson::Document root;
    root.Parse(params.style_json().data(), params.style_json().length());

    if (root.HasParseError())
    {
        HRZ_LOG_ERROR(
            "Invalid Mapbox style JSON: {}", rapidjson::GetParseError_En(root.GetParseError()));

        hrz_proto::MapboxTranslationMessage message;
        message.mutable_ticket()->set_opaque(ticket);
        message.mutable_result()->set_status(
            hrz_proto::MapboxTranslationStatus::MAPBOX_TRANSLATION_FAILURE);
        client_message_queue::enqueue_mapbox_translation_message(mq, std::move(message));

        return ticket;
    }

    sys->translations.push_back({});
    auto& translation = sys->translations.back();

    translation.ticket = ticket;
    translation.params = params;

    auto sprite_url_opt = hrz::json::get_str(root, "sprite");
    if (sprite_url_opt.has_value())
    {
        std::string sprite_index_url = sprite_url_opt.value() + std::string(".json");
        translation.sprite_load_ticket = assets_loader::begin(
            al, sprite_index_url, assets_loader::from_proto(params.http_headers()));
    }

    return ticket;
}

void work(
    MapboxTranslationSystem* sys,
    AssetsLoader* al,
    BlobAllocator* ba,
    ClientMessageQueue* mq,
    Scene* scene,
    ActorRunner* ar)
{
    for (auto it = sys->translations.begin(); it != sys->translations.end();)
    {
        auto& translation = *it;

        bool ticket_is_valid = assets_loader::is_valid(al, translation.sprite_load_ticket);
        if (ticket_is_valid && !assets_loader::is_finished(al, translation.sprite_load_ticket))
        {
            ++it;
            continue;
        }

        hrz_proto::MapboxTranslationMessage message;
        message.mutable_ticket()->set_opaque(translation.ticket);

        std::string sprite_json = "";

        if (ticket_is_valid)
        {
            auto status = assets_loader::get_status(al, translation.sprite_load_ticket);
            if (status == assets_loader::RequestStatus::Loaded)
            {
                auto handle = assets_loader::get_blob(al, ba, translation.sprite_load_ticket);
                auto sprite_data = handle.get_data();

                sprite_json = std::string((const char*)sprite_data.data(), sprite_data.size());
            }
            else
            {
                HRZ_LOG_ERROR("Could not load Mapbox sprites");
                message.mutable_result()->set_status(
                    hrz_proto::MapboxTranslationStatus::MAPBOX_DOWNLOAD_FAILURE);
                it = sys->translations.erase(it);
                continue;
            }
        }

        hrz_mapbox::TranslationSettings settings;
        settings.raster_group = translation.params.raster_group();
        settings.first_raster_slot = translation.params.first_raster_slot();
        settings.first_vector_data_layer_id = translation.params.first_vector_data_layer_id();
        settings.first_flat_overlay_z_index = translation.params.first_flat_overlay_z_index();
        settings.first_symbol_z_index = translation.params.first_symbol_z_index();

        hrz::CameraViewInfo view_info = hrz::scene::get_main_camera_view_info(scene);
        settings.camera_fovy = view_info.cam.fovy;
        settings.viewport_size = lm::dvec2(view_info.viewport.size);

        hrz_proto::SceneLoadRequest load_request;
        load_request.set_clear_layers(translation.params.destroy_existing_layers());
        load_request.mutable_camera_animation()->CopyFrom(translation.params.camera_animation());

        auto result = hrz_mapbox::translate_scene(
            translation.params.style_json(), sprite_json, settings, load_request.mutable_dump());

        if (translation.params.ignore_camera_params())
        {
            load_request.mutable_dump()->clear_cameras();
        }

        bool ignore_ambient = translation.params.ignore_ambient_params();
        bool ignore_terrain = translation.params.ignore_terrain_params();

        if (ignore_ambient || ignore_terrain)
        {
            ConstSceneModelAccessor accessor(scene::get_model(scene));

            for (auto& params : *load_request.mutable_dump()->mutable_scene_view_settings())
            {
                hrz_proto::SceneViewSettingsPathBuilder<ConstSceneModelAccessor> builder(
                    accessor, params.index());

                if (ignore_ambient)
                {
                    params.mutable_settings()->mutable_ambient()->CopyFrom(
                        builder.clone().ambient().get());
                }

                if (ignore_terrain)
                {
                    params.mutable_settings()->mutable_terrain()->CopyFrom(
                        builder.clone().terrain().get());
                }
            }
        }

        if (result.success)
        {
            hrz::scene::load_scene_dump(
                scene, load_request, *message.mutable_result()->mutable_created_layers(), ar);

            message.mutable_result()->set_status(
                hrz_proto::MapboxTranslationStatus::MAPBOX_TRANSLATION_SUCCESS);
            message.mutable_result()->set_raster_slots_used(result.raster_slots_used);
            message.mutable_result()->set_vector_data_layer_ids_used(
                result.vector_data_layer_ids_used);
            message.mutable_result()->set_flat_overlay_z_indices_used(
                result.flat_overlay_z_indices_used);
            message.mutable_result()->set_symbol_z_indices_used(result.symbol_z_indices_used);
        }
        else
        {
            message.mutable_result()->set_status(
                hrz_proto::MapboxTranslationStatus::MAPBOX_TRANSLATION_FAILURE);
        }

        client_message_queue::enqueue_mapbox_translation_message(mq, std::move(message));

        it = sys->translations.erase(it);
        continue;
    }
}

bool is_working(const MapboxTranslationSystem* system)
{
    return !system->translations.empty();
}

} // namespace mapbox

} // namespace hrz
