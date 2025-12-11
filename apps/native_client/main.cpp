#include "hrz/core/backend.h"
#include "hrz/protocol/layer/defs.pb.h"
#include "hrz/protocol/mapbox/service.pb.h"
#include "hrz/protocol/scene_dump/service.pb.h"
#include "hrz/protocol/scene_model_version.h"
#include "hrz/scene_dump/migration.h"

#include <argparser.h>
#include <inttypes.h>
#include <ws_server.h>
#include <wsi.h>

#define HRZ_VERSION_STR3(X) #X
#define HRZ_VERSION_STR2(X) HRZ_VERSION_STR3(X)
#define HRZ_VERSION_STR HRZ_VERSION_STR2(HRZ_VERSION)

#define __STDC_FORMAT_MACROS

static std::shared_ptr<hrz_core::Backend> backend;
static std::shared_ptr<hrz_api::Api> api;

struct WsServerHandler : public ws::ServerHandler
{
    ws::Server* server = nullptr;
    bool has_client = false;
    std::vector<uint8_t> response_buffer;

    void on_client_connect() override
    {
        assert(!has_client);
        printf("Client connected\n");
        has_client = true;
    }

    void on_client_disconnect() override
    {
        assert(has_client);
        printf("Client disconnected\n");
        has_client = false;
    }

    void on_raw_message(const void* data, size_t size) override
    {
        assert(has_client);
        if (size >= 12)
        {
            uint32_t* u32_data = (uint32_t*)data;
            uint32_t rpc_index = u32_data[0];
            uint32_t service = u32_data[1];
            uint32_t method = u32_data[2];

            std::vector<uint8_t> response =
                backend->rpc(service, method, (const uint8_t*)data + 12, size - 12);

            response_buffer.resize(response.size() + 4);
            memcpy(response_buffer.data(), &rpc_index, 4);
            memcpy(response_buffer.data() + 4, response.data(), response.size());

            ws::send_raw(server, response_buffer.data(), response_buffer.size());
        }
    }
};

static WsServerHandler server_handler;

static bool frame()
{
    ws::poll(server_handler.server, &server_handler);
    return backend->frame();
}

int main(int argc, char* argv[])
{
    argparser::ArgParser* arg_parser = argparser::create();
    argparser::ArgDef arg;

    arg.name = "port";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = true;
    arg.default_uint = 8484;
    argparser::add_argument(arg_parser, arg);

    arg.name = "worker-count";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = true;
    arg.default_uint = 0;
    argparser::add_argument(arg_parser, arg);

    arg.name = "run-actors-on-main-thread";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = true;
    arg.default_bool = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "user-agent";
    arg.type = argparser::ArgType::String;
    arg.required = false;
    arg.has_default = true;
    arg.default_string = "Horizon/" HRZ_VERSION_STR;
    argparser::add_argument(arg_parser, arg);

    arg.name = "log-filter-level";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = true;
    arg.default_uint = 0;
    argparser::add_argument(arg_parser, arg);

    arg.name = "graphics-level";
    arg.type = argparser::ArgType::Int;
    arg.required = false;
    arg.has_default = true;
    arg.default_int = (int)hrz_proto::GRAPHICS_LEVEL_AUTO;
    argparser::add_argument(arg_parser, arg);

    arg.name = "force-render";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = true;
    arg.default_bool = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "force-flat-overlay-render";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.default_bool = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "disable-events-capture";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = true;
    arg.default_bool = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "internal-integration";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = true;
    arg.default_bool = true;
    argparser::add_argument(arg_parser, arg);

    arg.name = "raster-provider-tile-cache-size";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = true;
    arg.default_uint = 150;
    argparser::add_argument(arg_parser, arg);

    arg.name = "blob-memory-pool-size";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = true;
    arg.default_uint = 400;
    argparser::add_argument(arg_parser, arg);

    arg.name = "use-system-allocator-for-blobs";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = true;
    arg.default_bool = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "max-video-memory-size";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = true;
    arg.default_uint = 0;
    argparser::add_argument(arg_parser, arg);

    arg.name = "monitoring-server";
    arg.type = argparser::ArgType::String;
    arg.required = false;
    arg.has_default = true;
    arg.default_string = "";
    argparser::add_argument(arg_parser, arg);

    arg.name = "monitoring-auto-connect";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = true;
    arg.default_bool = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "http-cache-size";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = true;
    arg.default_uint = 128;
    argparser::add_argument(arg_parser, arg);

    arg.name = "disable-terrain";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = true;
    arg.default_bool = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "show-loading-screen";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = true;
    arg.default_bool = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "force-shader-compilation";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = true;
    arg.default_bool = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "scene-dump";
    arg.type = argparser::ArgType::String;
    arg.required = false;
    arg.has_default = true;
    arg.default_string = "";
    argparser::add_argument(arg_parser, arg);

    arg.name = "http-referrer";
    arg.type = argparser::ArgType::String;
    arg.required = false;
    arg.has_default = true;
    arg.default_string = "";
    argparser::add_argument(arg_parser, arg);

    arg.name = "enable-shadows";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "shadows-cascade-count";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "enable-atmosphere";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "flat-overlay-resolution";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "flat-overlay-cascade-count";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "enable-ui-elements-depth-peeling";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "imagery-merge-group-count";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "raster-atlas-size";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "compress-raster-atlas-texture";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "help";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = true;
    arg.default_bool = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "w";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = true;
    arg.default_uint = 1280;
    argparser::add_argument(arg_parser, arg);

    arg.name = "h";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = true;
    arg.default_uint = 720;
    argparser::add_argument(arg_parser, arg);

    arg.name = "mapbox-style";
    arg.type = argparser::ArgType::String;
    arg.required = false;
    arg.default_string = "";
    argparser::add_argument(arg_parser, arg);

    if (!argparser::parse(arg_parser, argc, argv))
    {
        printf("Invalid, or missing, command line arguments.\n");
        return 1;
    }

    if (argparser::get_value_bool(arg_parser, "help").value_or(false))
    {
        argparser::show_help(arg_parser);
        return 0;
    }

    hrz_proto::ViewerOptions options;
    options.set_worker_count(argparser::get_value_uint(arg_parser, "worker-count").value());
    options.set_run_actors_on_main_thread(
        argparser::get_value_bool(arg_parser, "run-actors-on-main-thread").value());
    options.set_user_agent(argparser::get_value_string(arg_parser, "user-agent").value());
    options.set_log_filter_level(argparser::get_value_uint(arg_parser, "log-filter-level").value());
    options.set_graphics_level(
        (hrz_proto::GraphicsLevel)argparser::get_value_int(arg_parser, "graphics-level").value());
    options.set_force_render(argparser::get_value_bool(arg_parser, "force-render").value());
    options.set_force_flat_overlay_render(
        argparser::get_value_bool(arg_parser, "force-flat-overlay-render").value());
    options.set_disable_events_capture(
        argparser::get_value_bool(arg_parser, "disable-events-capture").value());
    options.set_internal_integration(
        argparser::get_value_bool(arg_parser, "internal-integration").value());
    options.set_raster_provider_tile_cache_size(
        argparser::get_value_uint(arg_parser, "raster-provider-tile-cache-size").value());
    options.set_blob_memory_pool_size(
        argparser::get_value_uint(arg_parser, "blob-memory-pool-size").value() * 1024 * 1024);
    options.set_use_system_allocator_for_blobs(
        argparser::get_value_bool(arg_parser, "use-system-allocator-for-blobs").value());
    options.set_max_video_memory_size(
        argparser::get_value_uint(arg_parser, "max-video-memory-size").value() * 1024 * 1024);
    options.set_native_http_cache_size(
        argparser::get_value_uint(arg_parser, "http-cache-size").value() * 1024 * 1024);
    options.set_native_http_referrer(
        argparser::get_value_string(arg_parser, "http-referrer").value());
    options.set_disable_terrain(argparser::get_value_bool(arg_parser, "disable-terrain").value());
    options.set_show_loading_screen(
        argparser::get_value_bool(arg_parser, "show-loading-screen").value());
    options.set_force_shader_compilation(
        argparser::get_value_bool(arg_parser, "force-shader-compilation").value());

#define OVERRIDE_SETTING_WITH_OPT_ARG(PRP, ARG, ARGTYPE)                             \
    auto PRP##_opt = argparser::get_value_##ARGTYPE(arg_parser, ARG);                \
    if (PRP##_opt.has_value())                                                       \
    {                                                                                \
        options.mutable_graphics_settings_overrides()->set_##PRP(PRP##_opt.value()); \
    }

    OVERRIDE_SETTING_WITH_OPT_ARG(shadows_enabled, "enable-shadows", bool);
    OVERRIDE_SETTING_WITH_OPT_ARG(shadows_cascade_count, "shadows-cascade-count", uint);
    OVERRIDE_SETTING_WITH_OPT_ARG(atmosphere_enabled, "enable-atmosphere", bool);
    OVERRIDE_SETTING_WITH_OPT_ARG(flat_overlay_resolution, "flat-overlay-resolution", uint);
    OVERRIDE_SETTING_WITH_OPT_ARG(flat_overlay_cascade_count, "flat-overlay-cascade-count", uint);
    OVERRIDE_SETTING_WITH_OPT_ARG(
        ui_elements_depth_peeling_enabled, "enable-ui-elements-depth-peeling", bool);
    OVERRIDE_SETTING_WITH_OPT_ARG(imagery_merge_group_count, "imagery-merge-group-count", uint);
    OVERRIDE_SETTING_WITH_OPT_ARG(raster_atlas_size, "raster-atlas-size", uint);
    OVERRIDE_SETTING_WITH_OPT_ARG(
        raster_atlas_texture_compression_enabled, "compress-raster-atlas-texture", bool);

#undef OVERRIDE_SETTING_WITH_OPT_ARG

    auto add_key_binding = [&options](hrz_proto::Key key, hrz_proto::KeyAction action)
    {
        auto* binding = options.mutable_key_bindings()->add_bindings();
        binding->set_key(key);
        binding->set_action(action);
    };

    add_key_binding(hrz_proto::Key::K_R, hrz_proto::KeyAction::RESET_NORTH);
    add_key_binding(hrz_proto::Key::K_P, hrz_proto::KeyAction::TOGGLE_DEV_UI);
    add_key_binding(hrz_proto::Key::K_O, hrz_proto::KeyAction::MOVE_DEV_UI);
    add_key_binding(hrz_proto::Key::K_D, hrz_proto::KeyAction::DESELECT_ALL);
    add_key_binding(hrz_proto::Key::K_A, hrz_proto::KeyAction::EDITOR_APPEND);
    add_key_binding(hrz_proto::Key::K_S, hrz_proto::KeyAction::EDITOR_SELECT);
    add_key_binding(hrz_proto::Key::K_DELETE, hrz_proto::KeyAction::EDITOR_DELETE_SELECTED_POINT);
    add_key_binding(hrz_proto::Key::K_M, hrz_proto::KeyAction::TOGGLE_MONITORING);
    add_key_binding(hrz_proto::Key::K_CTRL, hrz_proto::KeyAction::MOD_KEY);

    int width = argparser::get_value_uint(arg_parser, "w").value();
    int height = argparser::get_value_uint(arg_parser, "h").value();

    std::optional<WsiInstance> wsi = wsi_init(width, height);
    if (!wsi.has_value())
    {
        printf("Could not create window");
        return hrz_proto::ViewerInitStatus::FRAMEBUFFER_CREATION_ERROR;
    }

    backend = hrz_core::Backend::create(wsi->instance, wsi->window, options);
    if (backend->init_status() != hrz_proto::ViewerInitStatus::INIT_SUCCESS)
    {
        printf(
            "Error initializing Horizon: %s\n",
            hrz_proto::ViewerInitStatus_Name(backend->init_status()).c_str());
        backend->cleanup();
        wsi_cleanup();
        return backend->init_status();
    }

    api = hrz_api::Api::create(backend);

    uint16_t port = argparser::get_value_uint(arg_parser, "port").value();

    std::pair<ws::Server*, ws::Status> server_status = ws::create_server(port);
    if (server_status.second != ws::Status::Ok)
    {
        printf("WS error: %s\n", ws::to_string(server_status.second));
        return 1;
    }

    server_handler.server = server_status.first;
    if (!server_handler.server)
    {
        printf("Error creating websockets server on port %" PRIu16 "\n", port);
        backend->cleanup();
        wsi_cleanup();
        return 1;
    }

    printf("Listening websockets on port %" PRIu16 "\n", port);

    auto opt_monitoring_server = argparser::get_value_string(arg_parser, "monitoring-server");
    if (opt_monitoring_server.has_value() && *opt_monitoring_server.value() != '\0')
    {
        HrzProtocol::StringValue input;
        input.set_value(opt_monitoring_server.value());
        HrzProtocol::Void output;

        api->monitoring_service.set_monitoring_server_address(input, output);
    }

    auto opt_monitoring_connect = argparser::get_value_bool(arg_parser, "monitoring-auto-connect");
    if (opt_monitoring_connect.has_value())
    {
        HrzProtocol::BoolValue input;
        input.set_value(opt_monitoring_connect.value());
        HrzProtocol::Void output;

        api->monitoring_service.enable_profiling(input, output);
        api->monitoring_service.enable_metrics(input, output);
        api->monitoring_service.connect_to_monitoring_server(input, output);
    }

    std::string dump_filename = argparser::get_value_string(arg_parser, "scene-dump").value_or("");
    if (!dump_filename.empty())
    {
        FILE* fp = fopen(dump_filename.c_str(), "rb");
        if (fp)
        {
            fseek(fp, 0, SEEK_END);
            size_t size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            std::vector<std::byte> raw_dump;
            raw_dump.resize(size);
            size = fread(raw_dump.data(), 1, size, fp);
            raw_dump.resize(size);
            fclose(fp);

            hrz_proto::SceneLoadRequest req;
            req.mutable_dump()->ParseFromArray(raw_dump.data(), raw_dump.size());
            req.set_clear_layers(true);

            if (req.dump().version() != hrz::scene_dump::SceneModelVersion)
            {
                raw_dump = hrz::migration::migrate(raw_dump);
                if (!raw_dump.empty())
                {
                    req.mutable_dump()->ParseFromArray(raw_dump.data(), raw_dump.size());
                }
                else
                {
                    printf("Couldn't migrate scene dump\n");
                }
            }

            hrz_proto::LayerArray layers;
            api->scene_dump_service.load_dump(req, layers);
        }
    }

    std::string mapbox_style_filename =
        argparser::get_value_string(arg_parser, "mapbox-style").value_or("");
    if (!mapbox_style_filename.empty())
    {
        FILE* fp = fopen(mapbox_style_filename.c_str(), "rb");
        if (fp)
        {
            fseek(fp, 0, SEEK_END);
            size_t size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            std::vector<char> raw_style;
            raw_style.resize(size);
            size = fread(raw_style.data(), 1, size, fp);
            raw_style.resize(size);
            fclose(fp);

            hrz_proto::MapboxTranslationParams params;
            params.set_style_json(raw_style.data(), raw_style.size());

            hrz_proto::MapboxTranslationTicket ticket;
            api->mapbox_service.translate_scene(params, ticket);
        }
        else
        {
            printf("Could not open Mapbox style at \"%s\"\n", mapbox_style_filename.c_str());
        }
    }

    wsi_run(frame);

    ws::destroy_server(server_handler.server);
    backend->cleanup();
    wsi_cleanup();
    argparser::destroy(arg_parser);
    return 0;
}
