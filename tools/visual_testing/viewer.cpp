#include "hrz/core/backend.h"
#include "hrz/fnd/string_utils.h"
#include "hrz/protocol/path_builder.h"
#include "hrz/protocol/scene_model_version.h"
#include "hrz/scene_dump/migration.h"

#include <argparser.h>
#include <wsi.h>

#include <span>
#include <thread>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

using namespace std::chrono;

namespace
{
constexpr double DEFAULT_TIMEOUT_SEC = 300; // 5 min
constexpr size_t INACTIVE_FRAMES_BEFORE_CAPTURE = 2;

#define EXIT_CODES                            \
    DEFINE_EXIT_CODE(Ok, 0)                   \
    DEFINE_EXIT_CODE(MissingArguments, 1)     \
    DEFINE_EXIT_CODE(InvalidArguments, 2)     \
    DEFINE_EXIT_CODE(FailedInitialization, 3) \
    DEFINE_EXIT_CODE(MissingInput, 4)         \
    DEFINE_EXIT_CODE(InvalidInput, 5)         \
    DEFINE_EXIT_CODE(FailedMigration, 6)      \
    DEFINE_EXIT_CODE(Timeout, 7)

enum ExitCode
{

#define DEFINE_EXIT_CODE(NAME, VALUE) NAME = VALUE,
    EXIT_CODES
#undef DEFINE_EXIT_CODE
};

class Viewer
{
    enum class Status
    {
        Init = 0,
        LoadingScene = 1,
        TakeCapture = 2,
        Timeout = 3,
    };

    Status status = Status::Init;

    std::shared_ptr<hrz_api::Api> api = nullptr;
    std::shared_ptr<hrz_core::Backend> backend = nullptr;

    hrz_proto::Image capture;

    ExitCode load_scene_dump(const char* scene_dump_filename)
    {
        assert(api);

        // Load the file
        FILE* f = fopen(scene_dump_filename, "rb");
        if (!f)
        {
            printf("Couldn't find scene dump: '%s'\n", scene_dump_filename);
            return ExitCode::MissingInput;
        }

        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);

        std::vector<std::byte> raw_message_buffer(size);
        size = fread(raw_message_buffer.data(), 1, size, f);
        raw_message_buffer.resize(size);
        fclose(f);

        // Parse the dump
        hrz_proto::SceneDump dump;
        if (!dump.ParseFromArray(raw_message_buffer.data(), raw_message_buffer.size()))
        {
            printf(
                "Error: failed to parse scene dump data from input file '%s'\n",
                scene_dump_filename);
            return ExitCode::InvalidInput;
        }

        // Migrate the dump
        if (dump.version() != hrz::scene_dump::SceneModelVersion)
        {
            raw_message_buffer = hrz::migration::migrate(raw_message_buffer);
            if (raw_message_buffer.empty())
            {
                printf(
                    "Error: failed to migrate '%s' from dump version %d to %d.\n",
                    scene_dump_filename, dump.version(), hrz::scene_dump::SceneModelVersion);
                return ExitCode::FailedMigration;
            }
            dump.ParseFromArray(raw_message_buffer.data(), raw_message_buffer.size());
        }

        // Apply the dump
        hrz_proto::LayerArray output;
        hrz_proto::SceneLoadRequest input;
        *input.mutable_dump() = std::move(dump);
        input.set_clear_layers(true);

        api->scene_dump_service.load_dump(input, output);

        return ExitCode::Ok;
    }

    ExitCode load_mapbox_style(const char* mapbox_style_filename)
    {
        assert(api);

        // Load the file
        FILE* f = fopen(mapbox_style_filename, "rb");
        if (!f)
        {
            printf("Couldn't find mapbox style: '%s'\n", mapbox_style_filename);
            return ExitCode::MissingInput;
        }

        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);

        std::vector<char> buffer(size);
        size = fread(buffer.data(), sizeof(char), size, f);
        buffer.resize(size);
        fclose(f);

        // Apply the style
        hrz_proto::MapboxTranslationTicket output;
        hrz_proto::MapboxTranslationParams input;
        input.set_style_json(buffer.data(), buffer.size());

        api->mapbox_service.translate_scene(input, output);

        return ExitCode::Ok;
    }

public:
    void wait_for_message(
        hrz_proto::MessageType msg_type,
        uint64_t timeout_ms,
        hrz_proto::TypedMessage* out_msg)
    {
        auto start_ms = high_resolution_clock::now();

        bool message_found = false;
        while (!message_found)
        {
            backend->frame();

            hrz_proto::DequeueParams params;
            params.set_max_message_count(10);

            hrz_proto::DequeuedMessages messages;

            while (true)
            {
                api->message_queue_service.dequeue_messages(params, messages);

                for (const auto& msg : messages.messages())
                {
                    if (msg.type() == msg_type)
                    {
                        out_msg->CopyFrom(msg);
                        message_found = true;
                    }
                }

                if (messages.queue_size().message_count() == 0) break;
            }

            uint64_t elapsed_ms = duration_cast<duration<uint64_t, std::milli>>(
                                      high_resolution_clock::now() - start_ms)
                                      .count();
            if (timeout_ms && elapsed_ms > timeout_ms)
            {
                status = Status::Timeout;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    ExitCode run(
        const char* input_file,
        const char* output_capture_file,
        int width,
        int height,
        bool show_window,
        uint64_t timeout_ms,
        uint32_t log_filter_level)
    {
        // Initialize Horizon.
        std::optional<WsiInstance> wsi = wsi_init(width, height, show_window);
        if (!wsi.has_value())
        {
            printf("Could not create window\n");
            return ExitCode::FailedInitialization;
        }

        hrz_proto::ViewerOptions options;
        options.set_log_filter_level(log_filter_level);
        options.set_graphics_level(hrz_proto::GraphicsLevelHigh);

        backend = hrz_core::Backend::create(wsi->instance, wsi->window, options);
        if (backend->init_status() != hrz_proto::ViewerInitStatus::INIT_SUCCESS)
        {
            printf(
                "Couldn't initialize Horizon backend: %s\n",
                hrz_proto::ViewerInitStatus_Name(backend->init_status()).c_str());
            backend->cleanup();
            wsi_cleanup();
            return ExitCode::FailedInitialization;
        }

        api = hrz_api::Api::create(backend);

        {
            hrz_proto::TypedMessage msg;
            wait_for_message(hrz_proto::MessageType::VIEWER_READY_MESSAGE, 0, &msg);
            assert(msg.has_viewer_ready());
        }

        // Load input.
        ExitCode load_exit_code{};
        if (std::string_view(input_file).ends_with(".hrz_scene.pbf"))
        {
            load_exit_code = load_scene_dump(input_file);
        }
        else if (std::string_view(input_file).ends_with(".json"))
        {
            load_exit_code = load_mapbox_style(input_file);
        }
        else
        {
            printf("Unrecognized file extension for input \"%s\"\n", input_file);
            load_exit_code = ExitCode::InvalidInput;
        }

        if (load_exit_code != ExitCode::Ok)
        {
            backend->cleanup();
            wsi_cleanup();
            return load_exit_code;
        }

        status = Status::LoadingScene;

        auto start_ms = high_resolution_clock::now();
        size_t consecutive_inactive_frames = 0;
        while (status == Status::LoadingScene)
        {
            backend->frame();

            hrz_proto::Void input;
            hrz_proto::BoolValue output;
            api->viewer_service.is_working(input, output);

            if (!output.value())
            {
                consecutive_inactive_frames++;
                if (consecutive_inactive_frames >= INACTIVE_FRAMES_BEFORE_CAPTURE)
                {
                    status = Status::TakeCapture;
                }
            }
            else
            {
                consecutive_inactive_frames = 0;
            }

            uint64_t elapsed_ms = duration_cast<duration<uint64_t, std::milli>>(
                                      high_resolution_clock::now() - start_ms)
                                      .count();
            if (elapsed_ms > timeout_ms)
            {
                status = Status::Timeout;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        ExitCode exit_code = ExitCode::Ok;

        // Take screenshot.
        if (status == Status::TakeCapture)
        {
            hrz_proto::Void output_void;
            api->viewer_service.schedule_frame_capture(hrz_proto::Void{}, output_void);

            {
                hrz_proto::TypedMessage msg;
                // The scene should already be loaded. Set a 30 seconds timeout to take the capture.
                wait_for_message(hrz_proto::MessageType::FRAME_CAPTURE_MESSAGE, 30000, &msg);
                assert(msg.has_frame_capture());
                capture = msg.frame_capture();
            }

            if (capture.width() > 0)
            {
                // Save capture.
                stbi_write_png(
                    output_capture_file, capture.width(), capture.height(), 4,
                    capture.data().c_str(), capture.width() * 4 * sizeof(uint8_t));
            }
        }

        if (status == Status::Timeout)
        {
            exit_code = ExitCode::Timeout;
        }

        backend->cleanup();
        wsi_cleanup();
        return exit_code;
    }
};
} // namespace

int main(int argc, char* argv[])
{
    argparser::ArgParser* arg_parser = argparser::create();
    argparser::ArgDef arg;

    arg.name = "input";
    arg.type = argparser::ArgType::String;
    arg.required = true;
    arg.has_default = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "output";
    arg.type = argparser::ArgType::String;
    arg.required = true;
    arg.has_default = false;
    argparser::add_argument(arg_parser, arg);

    arg.name = "width";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = true;
    arg.default_uint = 640;
    argparser::add_argument(arg_parser, arg);

    arg.name = "height";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = true;
    arg.default_uint = 480;
    argparser::add_argument(arg_parser, arg);

    arg.name = "show-window";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = true;
    arg.default_bool = true;
    argparser::add_argument(arg_parser, arg);

    arg.name = "timeout";
    arg.type = argparser::ArgType::Double;
    arg.required = false;
    arg.has_default = true;
    arg.default_double = DEFAULT_TIMEOUT_SEC;
    argparser::add_argument(arg_parser, arg);

    arg.name = "log-filter-level";
    arg.type = argparser::ArgType::Uint;
    arg.required = false;
    arg.has_default = true;
    arg.default_uint = 0;
    argparser::add_argument(arg_parser, arg);

    arg.name = "help";
    arg.type = argparser::ArgType::Bool;
    arg.required = false;
    arg.has_default = true;
    arg.default_bool = false;
    argparser::add_argument(arg_parser, arg);

    auto print_help = [&]()
    {
        argparser::show_help(arg_parser);
        printf("Return codes:\n");
#define DEFINE_EXIT_CODE(NAME, VALUE) printf("  %d = %s\n", VALUE, #NAME);
        EXIT_CODES
#undef DEFINE_EXIT_CODE
    };

    if (!argparser::parse(arg_parser, argc, argv))
    {
        print_help();
        return ExitCode::MissingArguments;
    }

    if (argparser::get_value_bool(arg_parser, "help").value_or(false))
    {
        print_help();
        return ExitCode::Ok;
    }

    const char* input_file = argparser::get_value_string(arg_parser, "input").value();
    const char* output_capture_file = argparser::get_value_string(arg_parser, "output").value();
    uint32_t width = argparser::get_value_uint(arg_parser, "width").value();
    uint32_t height = argparser::get_value_uint(arg_parser, "height").value();
    bool show_window = argparser::get_value_bool(arg_parser, "show-window").value();
    double timeout_sec = argparser::get_value_double(arg_parser, "timeout").value();
    uint32_t log_filter_level = argparser::get_value_uint(arg_parser, "log-filter-level").value();

    if (width < 1 || height < 1)
    {
        printf("Invalid width or height.\n");
        return ExitCode::InvalidArguments;
    }

    Viewer v;
    return v.run(
        input_file, output_capture_file, (int)width, (int)height, show_window, timeout_sec * 1000,
        log_filter_level);
}
