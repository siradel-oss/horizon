#include "hrz/core/dev_ui.h"

extern "C"
{
#include <microui/microui.h>
}

#include "hrz/common/blob_allocator.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/common/ui_utils.h"
#include "hrz/core/blob_image.h"
#include "hrz/core/debug_draw.h"
#include "hrz/core/events.h"
#include "hrz/core/gestures.h"
#include "hrz/core/image_decoder.h"
#include "hrz/core/job_scheduler.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/platform/platform.h"
#include "hrz/core/render.h"
#include "hrz/core/resources/resources.h"
#include "hrz/core/scene.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/core/version.h"
#include "hrz/fnd/defines.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/static_vector.h"

#include <mycelium/backend.h>

#include <optional>

namespace
{
enum
{
    ATLAS_WHITE = MU_ICON_MAX,
    ATLAS_FONT,
    INSTANCES_WIDTH = HRZ_S_DEV_UI_INSTANCES_WIDTH,
    INSTANCES_HEIGHT = 128,
    MAX_INSTANCES = INSTANCES_WIDTH * INSTANCES_HEIGHT,
};

enum DevUiWindow
{
    DevUiWindow_Monitoring,
    DevUiWindow_Viewport,
    DevUiWindow_Cameras,
    DevUiWindow_Logs,
    DevUiWindow_AssetsLoader,
    DevUiWindow_BlobAllocator,
    DevUiWindow_Jobs,
    DevUiWindow_Rasters,
    DevUiWindow_ElevationQueries,
    DevUiWindow_VectorLoader,
    DevUiWindow_VectorTilesLayers,
    DevUiWindow_VectorFlatOverlays,
    DevUiWindow_SceneModel,
    DevUiWindow_DebugDraw,
    DevUiWindow_Count,
};

static const int DEV_UI_WINDOW_COUNT = (size_t)DevUiWindow_Count;

const char* _window_name(DevUiWindow w)
{
    switch (w)
    {
        case DevUiWindow_Monitoring: return "Monitoring";
        case DevUiWindow_Viewport: return "Viewport";
        case DevUiWindow_Cameras: return "Cameras";
        case DevUiWindow_AssetsLoader: return "Asset loader";
        case DevUiWindow_BlobAllocator: return "Blobs";
        case DevUiWindow_Jobs: return "Jobs";
        case DevUiWindow_Logs: return "Logs";
        case DevUiWindow_Rasters: return "Rasters";
        case DevUiWindow_ElevationQueries: return "Elevation queries";
        case DevUiWindow_VectorLoader: return "Vector data loader";
        case DevUiWindow_VectorTilesLayers: return "Vector tiles";
        case DevUiWindow_VectorFlatOverlays: return "Flat overlays";
        case DevUiWindow_SceneModel: return "Scene model";
        case DevUiWindow_DebugDraw: return "Debug draw";
        default: return "???";
    }
}

static const mu_Rect atlas[] = {
    {},
    {88, 68, 16, 16},
    {0, 0, 18, 18},
    {113, 68, 5, 7},
    {118, 68, 7, 5},
    {125, 68, 2, 2},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {84, 68, 2, 17},
    {39, 68, 3, 17},
    {114, 51, 5, 17},
    {34, 17, 7, 17},
    {28, 34, 6, 17},
    {58, 0, 9, 17},
    {103, 0, 8, 17},
    {86, 68, 2, 17},
    {42, 68, 3, 17},
    {45, 68, 3, 17},
    {34, 34, 6, 17},
    {40, 34, 6, 17},
    {48, 68, 3, 17},
    {51, 68, 3, 17},
    {54, 68, 3, 17},
    {124, 34, 4, 17},
    {46, 34, 6, 17},
    {52, 34, 6, 17},
    {58, 34, 6, 17},
    {64, 34, 6, 17},
    {70, 34, 6, 17},
    {76, 34, 6, 17},
    {82, 34, 6, 17},
    {88, 34, 6, 17},
    {94, 34, 6, 17},
    {100, 34, 6, 17},
    {57, 68, 3, 17},
    {60, 68, 3, 17},
    {106, 34, 6, 17},
    {112, 34, 6, 17},
    {118, 34, 6, 17},
    {119, 51, 5, 17},
    {18, 0, 10, 17},
    {41, 17, 7, 17},
    {48, 17, 7, 17},
    {55, 17, 7, 17},
    {111, 0, 8, 17},
    {0, 35, 6, 17},
    {6, 35, 6, 17},
    {119, 0, 8, 17},
    {18, 17, 8, 17},
    {63, 68, 3, 17},
    {66, 68, 3, 17},
    {62, 17, 7, 17},
    {12, 51, 6, 17},
    {28, 0, 10, 17},
    {67, 0, 9, 17},
    {76, 0, 9, 17},
    {69, 17, 7, 17},
    {85, 0, 9, 17},
    {76, 17, 7, 17},
    {18, 51, 6, 17},
    {24, 51, 6, 17},
    {26, 17, 8, 17},
    {83, 17, 7, 17},
    {38, 0, 10, 17},
    {90, 17, 7, 17},
    {30, 51, 6, 17},
    {36, 51, 6, 17},
    {69, 68, 3, 17},
    {124, 51, 4, 17},
    {72, 68, 3, 17},
    {42, 51, 6, 17},
    {15, 68, 4, 17},
    {48, 51, 6, 17},
    {54, 51, 6, 17},
    {97, 17, 7, 17},
    {0, 52, 5, 17},
    {104, 17, 7, 17},
    {60, 51, 6, 17},
    {19, 68, 4, 17},
    {66, 51, 6, 17},
    {111, 17, 7, 17},
    {75, 68, 3, 17},
    {78, 68, 3, 17},
    {72, 51, 6, 17},
    {81, 68, 3, 17},
    {48, 0, 10, 17},
    {118, 17, 7, 17},
    {0, 18, 7, 17},
    {7, 18, 7, 17},
    {14, 34, 7, 17},
    {23, 68, 4, 17},
    {5, 52, 5, 17},
    {27, 68, 4, 17},
    {21, 34, 7, 17},
    {78, 51, 6, 17},
    {94, 0, 9, 17},
    {84, 51, 6, 17},
    {90, 51, 6, 17},
    {10, 68, 5, 17},
    {31, 68, 4, 17},
    {96, 51, 6, 17},
    {35, 68, 4, 17},
    {102, 51, 6, 17},
    {108, 51, 6, 17},
};
static const size_t atlas_size = sizeof(atlas) / sizeof(mu_Rect);

static constexpr mu_Color WHITE = {255, 255, 255, 255};
static constexpr mu_Color GRAY = {160, 160, 160, 255};
static constexpr mu_Color YELLOW = {255, 255, 32, 255};
static constexpr mu_Color RED = {255, 32, 32, 255};

} // namespace

// Import the private functions used to draw the UI.
namespace hrz
{
namespace assets_loader
{
void dev_ui(AssetsLoader* al, PlatformContext* platform, mu_Context* ctx, const char* window_name);
}

namespace blobs
{
void dev_ui(
    BlobAllocator* ba,
    const LayersInfo* layers_info,
    mu_Context* ctx,
    const char* window_name);
}

namespace job_scheduler
{
void dev_ui(JobScheduler* scheduler, mu_Context* ctx, const char* window_name);
}

namespace vector_data
{
void dev_ui(VectorDataLoader* loader, mu_Context* ctx, const char* window_name);
}

namespace planet
{
void raster_group_dev_ui(PlanetSurface* planet, mu_Context* ctx, const char* window_name);
void elevation_query_dev_ui(PlanetSurface* planet, mu_Context* ctx, const char* window_name);
} // namespace planet

namespace scene
{
void scene_model_dev_ui(
    Scene* scene,
    PlatformContext* platform,
    mu_Context* ctx,
    const char* window_name);
void viewport_dev_ui(Scene*, mu_Context*);
void camera_dev_ui(Scene*, mu_Context*, const char* window_name);
void flat_overlay_dev_ui(Scene*, mu_Context*, const char* window_name);
} // namespace scene

namespace vector_tiles_layers
{
void dev_ui(
    VectorTilesLayerSystem* system,
    const LayersInfo* layers_info,
    mu_Context* ctx,
    const char* window_name);
}

namespace debug_draw
{
void dev_ui(DebugDrawSystem* dd, mu_Context* ctx, const char* window_name);
}

namespace platform
{
void viewport_dev_ui(PlatformContext* platform, mu_Context* ctx);
}

void monitoring_dev_ui(
    Monitoring* m,
    RemoteMonitoring* rm,
    JobScheduler* js,
    const LayersInfo* layers_info,
    my::Instance* my,
    mu_Context* ctx,
    const char* window_name);
} // namespace hrz

namespace hrz
{
struct InstanceData
{
    float x;
    float y;
    float w;
    float h;
    uint8_t r, g, b, a;
    uint8_t uv_x, uv_y;
    uint8_t uv_w, uv_h;
};

struct UniformsData
{
    lm::mat4 projection;
};

struct DevUi
{
    enum class Status
    {
        LoadingAtlas,
        DecodingAtlas,
        UploadingAtlas,
        Ready,
        Error,
    };

    std::string main_window_name;

    mu_Context ui_ctx;
    uint32_t window_width;
    uint32_t window_height;

    int last_mouse_x = 0;
    int last_mouse_y = 0;

    Status status;
    blobs::AllocationTicket atlas_blob_ticket;
    hrz_jobs::DecodeBlobImageTicket atlas_decode_ticket;
    BlobImage atlas_image;

    my::ResourceHandle atlas;
    my::ResourceHandle atlas_sampler;
    std::vector<InstanceData> instances;
    my::ResourceHandle shader;
    my::ResourceHandle uniforms;

    struct
    {
        my::ResourceHandle instances_buffer;
        my::ResourceHandle vertex_buffer;
        my::ResourceHandle input;
    } rect;

    bool is_open;

    // This is used to not query the state of a windows's container while it has never been open.
    // This is because calling mu_get_container does the following:
    //   - If the container has already been initialized, return it.
    //   - If the container has never been initialized (because it has never been open), create it,
    //   open it, and return it.
    // In the second case, we actually don't want a new windows to be open. mu_begin_window takes in
    // a MU_OPT_CLOSED flag that prevents this behavior, but mu_get_container doesn't. Another way
    // to fix this issue would have been to add a new mu_get_container variant that doesn't
    // initialize new containers to match the MU_OPT_CLOSED behavior, but I didn't want to patch the
    // lib even more. This is the price of lightweightness I guess.
    //      -slerouzic, 2022-01-31
    bool open_once[DEV_UI_WINDOW_COUNT];

    mu_Rect current_clip;

    std::optional<hrz::gestures::SingleFingerGesture> drag_gesture;
};

namespace dev_ui
{
void _toggle_ui(DevUi* ui)
{
    assert(ui);
    ui->is_open = !ui->is_open;
}

void _resize_ui(DevUi* ui, uint32_t w, uint32_t h)
{
    assert(ui);
    ui->window_width = w;
    ui->window_height = h;
}

int _text_width(mu_Font font, const char* text, int len)
{
    int res = 0;
    for (const char* p = text; *p && len != 0; p++, len--)
    {
        unsigned char i = ATLAS_FONT + (unsigned char)*p;
        res += (i < atlas_size) ? atlas[i].w : 0;
    }
    return res;
}

int _text_height(mu_Font font)
{
    return 18;
}

DevUi* create(PlatformContext* platform)
{
    DevUi* ui = new DevUi();

    ui->main_window_name = std::string("Horizon v") + hrz::Version;
    ui->status = DevUi::Status::LoadingAtlas;

    ui->is_open = false;

    mu_init(&ui->ui_ctx);
    ui->ui_ctx.text_width = _text_width;
    ui->ui_ctx.text_height = _text_height;
    ui->ui_ctx.style->colors[MU_COLOR_TEXT] = WHITE;
    ui->ui_ctx.style->colors[MU_COLOR_WINDOWBG].a = 230;
    ui->ui_ctx.style->colors[MU_COLOR_TITLEBG].a = 230;

    _resize_ui(ui, 400, 300);

    ui->instances.reserve(MAX_INSTANCES);

    ui->drag_gesture = std::nullopt;

    platform::add_key_to_capture(platform, platform::Event::Key::Backspace);
    platform::add_key_to_capture(platform, platform::Event::Key::Enter);

    return ui;
}

void destroy(DevUi* ui, GpuResourceContext* rc)
{
    assert(ui);
    rc->dealloc(ui->atlas);
    rc->dealloc(ui->atlas_sampler);
    rc->dealloc(ui->uniforms);
    rc->dealloc(ui->rect.input);
    rc->dealloc(ui->rect.instances_buffer);
    rc->dealloc(ui->rect.vertex_buffer);
    delete ui;
}

void initialize_rendering(DevUi* ui, GpuResourceContext* rc)
{
    ui->atlas = my::ResourceHandle::null();

    {
        static const my::IndexName attribs[] =
            {{0, "i_vertex"}, {1, "i_geometry"}, {2, "i_color"}, {3, "i_uv"}};

        static const my::IndexName uniform_blocks[] = {{0, "Uniforms"}};

        static const my::IndexName samplers[] = {{0, "u_atlas"}};

        static const char* outputs[] = {
            "o_color",
        };

        my::ShaderResource res{};
        res.name = hrz_shaders::DevUi_name;
        res.link_hint = my::ShaderLinkHint::FirstUseImmediate;
        res.vertex_source_len = hrz_shaders::DevUi_vert_len;
        res.vertex_source = hrz_shaders::DevUi_vert;
        res.fragment_source_len = hrz_shaders::DevUi_frag_len;
        res.fragment_source = hrz_shaders::DevUi_frag;
        res.attrib_count = 4;
        res.attribs = attribs;
        res.uniform_block_count = 1;
        res.uniform_blocks = uniform_blocks;
        res.sampler_count = 1;
        res.samplers = samplers;
        res.output_count = 1;
        res.outputs = outputs;
        res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
        res.initial_state.depth.test = false;
        res.initial_state.depth.write = false;
        res.initial_state.color_blend.enable = true;
        res.initial_state.color_blend.color.src = my::ColorBlendState::One;
        res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
        res.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
        res.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;
        res.initial_state.color_blend.color.op = my::ColorBlendState::Add;
        res.initial_state.color_blend.mask = my::ColorBlendState::Components::RGBA;

        ui->shader = rc->alloc(&res, hrz::monitoring::systems::DevUi);
    }

    {
        my::BufferResource res(my::BufferResource::Uniform);
        res.size = sizeof(UniformsData);
        res.usage = my::UsageHint::Updatable;
        res.data = nullptr;

        ui->uniforms = rc->alloc(&res, hrz::monitoring::systems::DevUi);
    }

    {
        my::SamplerResource res;
        res.use_mipmaps = false;
        res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
        res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
        ui->atlas_sampler = rc->alloc(&res, hrz::monitoring::systems::DevUi);
    }

    {
        static_assert(
            sizeof(InstanceData) == sizeof(lm::vec4) + sizeof(uint8_t) * 8, "Size of InstanceData");

        my::BufferResource res(my::BufferResource::Vertex);
        res.size = sizeof(InstanceData) * MAX_INSTANCES;
        res.usage = my::UsageHint::Updatable;
        res.data = nullptr;

        ui->rect.instances_buffer = rc->alloc(&res, hrz::monitoring::systems::DevUi);
    }

    {
        static const lm::vec2 rect_vertices[] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};

        my::BufferResource res(my::BufferResource::Vertex);
        res.size = sizeof(rect_vertices);
        res.usage = my::UsageHint::Static;
        res.data = (void*)rect_vertices;

        ui->rect.vertex_buffer = rc->alloc(&res, hrz::monitoring::systems::DevUi);
    }

    {
        my::VertexInputStream streams[] = {
            // Vertex position
            {0, ui->rect.vertex_buffer, my::VertexFormat::Float32_2, 0, 8,
             my::VertexRate::PerVertex},
            // Instance geometry
            {1, ui->rect.instances_buffer, my::VertexFormat::Float32_4, 0, 24,
             my::VertexRate::PerInstance},
            // Instance color (sRGB)
            {2, ui->rect.instances_buffer, my::VertexFormat::UInt8Norm_4, 16, 24,
             my::VertexRate::PerInstance},
            // Instance UV
            {3, ui->rect.instances_buffer, my::VertexFormat::UInt8_4, 20, 24,
             my::VertexRate::PerInstance}};

        my::VertexInputResource res;
        res.attrib_count = 4;
        res.attribs = streams;

        ui->rect.input = rc->alloc(&res, hrz::monitoring::systems::DevUi);
    }
}

bool _is_position_in_window(DevUi* ui, int x, int y)
{
    auto mouse_in_window = [&](const char* name)
    {
        const auto* cont = mu_get_container(&ui->ui_ctx, name);
        if (cont)
        {
            return (
                cont->open && x >= cont->rect.x && x < cont->rect.x + cont->rect.w
                && y >= cont->rect.y && y < cont->rect.y + cont->rect.h);
        }
        else
        {
            return false;
        }
    };

    if (mouse_in_window(ui->main_window_name.c_str())) return true;

    for (int i = 0; i < DEV_UI_WINDOW_COUNT; ++i)
    {
        if (!ui->open_once[i]) continue;
        if (mouse_in_window(_window_name((DevUiWindow)i))) return true;
    }

    return false;
}

void _move_all_windows_to_cursor(DevUi* ui)
{
    auto move_window = [&](const char* name)
    {
        auto* cont = mu_get_container(&ui->ui_ctx, name);
        if (cont)
        {
            cont->rect.x = std::min(ui->last_mouse_x, (int)ui->window_width - cont->rect.w);
            cont->rect.y = std::min(ui->last_mouse_y, (int)ui->window_height - cont->rect.h);
        }
    };

    move_window(ui->main_window_name.c_str());
    for (int i = 0; i < DEV_UI_WINDOW_COUNT; ++i)
    {
        if (!ui->open_once[i]) continue;
        move_window(_window_name((DevUiWindow)i));
    }
}

void _handle_key_down_event_for_inputs(DevUi* ui, platform::Event::Key key)
{
    switch (key)
    {
        case platform::Event::Key::Backspace:
            mu_input_keydown(&ui->ui_ctx, MU_KEY_BACKSPACE);
            break;
        case platform::Event::Key::Enter: mu_input_keydown(&ui->ui_ctx, MU_KEY_RETURN); break;
        default: break;
    }
}

bool _handle_platform_event(DevUi* ui, const platform::Event& e, float device_pixel_ratio)
{
    bool control_has_focus = ui->ui_ctx.focus != 0;

    switch (e.kind)
    {
        case platform::Event::Kind::WindowResized:
        {
            _resize_ui(ui, e.window_resized.width, e.window_resized.height);
            return false;
        }
        default: break;
    }

    if (!ui->is_open) return false;

    bool is_capturable_mouse_event = false;
    int mouse_x;
    int mouse_y;

    switch (e.kind)
    {
        case hrz::platform::Event::Kind::MouseMove:
            mu_input_mousemove(
                &ui->ui_ctx, e.mouse_move.x / device_pixel_ratio,
                e.mouse_move.y / device_pixel_ratio);
            ui->last_mouse_x = e.mouse_move.x / device_pixel_ratio;
            ui->last_mouse_y = e.mouse_move.y / device_pixel_ratio;
            return false;
        case hrz::platform::Event::Kind::MouseButtonDown:
            is_capturable_mouse_event = true;
            mu_input_mousedown(
                &ui->ui_ctx, e.mouse_button.x / device_pixel_ratio,
                e.mouse_button.y / device_pixel_ratio, (int)e.mouse_button.button + 1);
            mouse_x = e.mouse_button.x / device_pixel_ratio;
            mouse_y = e.mouse_button.y / device_pixel_ratio;
            break;
        case hrz::platform::Event::Kind::MouseButtonUp:
            mu_input_mouseup(
                &ui->ui_ctx, e.mouse_button.x / device_pixel_ratio,
                e.mouse_button.y / device_pixel_ratio, (int)e.mouse_button.button + 1);
            mouse_x = e.mouse_button.x / device_pixel_ratio;
            mouse_y = e.mouse_button.y / device_pixel_ratio;
            return false;
            break;
        case hrz::platform::Event::Kind::MouseButtonDoubleClick:
            is_capturable_mouse_event = true;
            mouse_x = e.mouse_button.x / device_pixel_ratio;
            mouse_y = e.mouse_button.y / device_pixel_ratio;
            break;
        case hrz::platform::Event::Kind::MouseWheel:
            mu_input_scroll(&ui->ui_ctx, 0, -e.mouse_wheel.wheel * 15.0f);
            is_capturable_mouse_event = true;
            mouse_x = e.mouse_wheel.x / device_pixel_ratio;
            mouse_y = e.mouse_wheel.y / device_pixel_ratio;
            break;
        case platform::Event::Kind::KeyDown:
        {
            if (control_has_focus)
            {
                if (e.key == platform::Event::Key::Escape)
                {
                    ui->ui_ctx.focus = 0;
                    return true;
                }

                // This handles everything except the characters, which are
                // handled below.
                _handle_key_down_event_for_inputs(ui, e.key);
            }
            break;
        }
        case platform::Event::Kind::Character:
        {
            char buf[2] = {0, 0};
            buf[0] = e.character.c;
            mu_input_text(&ui->ui_ctx, buf);
            break;
        }
        default: return false;
    }

    if (is_capturable_mouse_event)
    {
        return _is_position_in_window(ui, mouse_x, mouse_y);
    }

    return control_has_focus;
}

bool _handle_gesture_event(DevUi* ui, const gestures::Event& e, float device_pixel_ratio)
{
    if (!ui->is_open) return false;

    switch (e.kind)
    {
        case hrz::gestures::Event::Kind::Qualification:
        {
            if (e.finger_count() == hrz::gestures::FingerCount::One)
            {
                const auto& single_finger_gesture = e.single_finger_gesture();

                int mouse_x = (int)(single_finger_gesture.position.x / device_pixel_ratio);
                int mouse_y = (int)(single_finger_gesture.position.y / device_pixel_ratio);

                bool in_window = _is_position_in_window(ui, mouse_x, mouse_y);

                if (!in_window) ui->ui_ctx.focus = 0;

                if (single_finger_gesture.type == hrz::gestures::SingleFingerGesture::Type::Tap)
                {
                    mu_input_mousedown(
                        &ui->ui_ctx, (int)(single_finger_gesture.position.x / device_pixel_ratio),
                        (int)(single_finger_gesture.position.y / device_pixel_ratio), 1);
                    mu_input_mouseup(
                        &ui->ui_ctx, (int)(single_finger_gesture.position.x / device_pixel_ratio),
                        (int)(single_finger_gesture.position.y / device_pixel_ratio), 1);

                    return in_window;
                }
                else if (
                    single_finger_gesture.type == hrz::gestures::SingleFingerGesture::Type::Drag)
                {
                    if (!ui->drag_gesture.has_value() && in_window)
                    {
                        ui->drag_gesture = {single_finger_gesture};

                        mu_input_mousedown(
                            &ui->ui_ctx,
                            (int)(single_finger_gesture.initial_position.x / device_pixel_ratio),
                            (int)(single_finger_gesture.initial_position.y / device_pixel_ratio),
                            1);
                        mu_input_mousemove(
                            &ui->ui_ctx,
                            (int)(single_finger_gesture.position.x / device_pixel_ratio),
                            (int)(single_finger_gesture.position.y / device_pixel_ratio));

                        return true;
                    }
                }
            }
            break;
        }
        case hrz::gestures::Event::Kind::Move:
        {
            if (e.finger_count() == hrz::gestures::FingerCount::One)
            {
                const auto& single_finger_gesture = e.single_finger_gesture();
                if (ui->drag_gesture.has_value()
                    && ui->drag_gesture.value().id == single_finger_gesture.id)
                {
                    mu_input_mousemove(
                        &ui->ui_ctx, (int)(single_finger_gesture.position.x / device_pixel_ratio),
                        (int)(single_finger_gesture.position.y / device_pixel_ratio));

                    ui->drag_gesture = {single_finger_gesture};
                }
            }
            break;
        }
        case hrz::gestures::Event::Kind::End:
        {
            if (e.finger_count() == hrz::gestures::FingerCount::One)
            {
                const auto& single_finger_gesture = e.single_finger_gesture();
                if (ui->drag_gesture.has_value()
                    && ui->drag_gesture.value().id == single_finger_gesture.id)
                {
                    mu_input_mouseup(
                        &ui->ui_ctx, (int)(single_finger_gesture.position.x / device_pixel_ratio),
                        (int)(single_finger_gesture.position.y / device_pixel_ratio), 1);

                    ui->drag_gesture = std::nullopt;
                }
            }
            break;
        }
        default: break;
    }

    return false;
}

bool handle_event(DevUi* ui, const Event& e, float device_pixel_ratio)
{
    assert(ui);

    switch (e.kind)
    {
        case hrz::Event::Kind::Platform:
            return _handle_platform_event(ui, e.platform, device_pixel_ratio);
        case hrz::Event::Kind::Gesture:
            return _handle_gesture_event(ui, e.gesture, device_pixel_ratio);
        default: break;
    }

    return false;
}

void _add_textured_quad(DevUi* ui, mu_Rect rect, mu_Rect uv, mu_Color color)
{
    assert(ui);

    if (ui->instances.size() >= MAX_INSTANCES)
    {
        HRZ_LOG_ERROR("Too many triangles in the dev UI");
        return;
    }

    int x0 = rect.x;
    int x1 = rect.x + rect.w;
    int y0 = rect.y;
    int y1 = rect.y + rect.h;

    x0 = std::max(x0, ui->current_clip.x);
    y0 = std::max(y0, ui->current_clip.y);
    x1 = std::min(x1, ui->current_clip.x + ui->current_clip.w);
    y1 = std::min(y1, ui->current_clip.y + ui->current_clip.h);

    if (x0 >= x1 || y0 >= y1) return;

    float clip_x0 = ((float)x0 - rect.x) / rect.w;
    float clip_y0 = ((float)y0 - rect.y) / rect.h;
    float clip_w = ((float)x1 - x0) / rect.w;
    float clip_h = ((float)y1 - y0) / rect.h;

    ui->instances.push_back(InstanceData{
        (float)x0, (float)y0, (float)(x1 - x0), (float)(y1 - y0), color.r, color.g, color.b,
        color.a,
        // UV coordinates fit in a uint8_t because it's 128x128.
        (uint8_t)((clip_x0 * uv.w) + uv.x), (uint8_t)((clip_y0 * uv.h) + uv.y),
        (uint8_t)(clip_w * uv.w), (uint8_t)(clip_h * uv.h)});
}

void _update_geometry(DevUi* ui)
{
    HRZ_SCOPED_SAMPLE("dev ui update geometry");

    assert(ui);
    ui->instances.clear();

    mu_Command* cmd = nullptr;
    while (mu_next_command(&ui->ui_ctx, &cmd))
    {
        switch (cmd->type)
        {
            case MU_COMMAND_RECT:
            {
                mu_Rect uv = atlas[ATLAS_WHITE];
                _add_textured_quad(ui, cmd->rect.rect, uv, cmd->rect.color);
                break;
            }
            case MU_COMMAND_ICON:
            {
                mu_Rect uv = atlas[cmd->icon.id];

                mu_Rect rect = cmd->icon.rect;
                int x = rect.x + (rect.w - uv.w) / 2;
                int y = rect.y + (rect.h - uv.h) / 2;

                rect = mu_rect(x, y, uv.w, uv.h);
                _add_textured_quad(ui, rect, uv, cmd->icon.color);
                break;
            }
            case MU_COMMAND_CLIP:
            {
                ui->current_clip = cmd->clip.rect;
                break;
            }
            case MU_COMMAND_TEXT:
            {
                mu_Rect dst = {cmd->text.pos.x, cmd->text.pos.y, 0, 0};
                for (const char* p = cmd->text.str; *p; p++)
                {
                    unsigned char i = ATLAS_FONT + (unsigned char)*p;
                    mu_Rect src = (i < atlas_size) ? atlas[i] : mu_Rect{};

                    dst.w = src.w;
                    dst.h = src.h;
                    _add_textured_quad(ui, dst, src, cmd->text.color);
                    dst.x += dst.w;
                }
                break;
            }
            default: break;
        }
    }
}

mu_Color _severity_to_text_color(log::Severity severity)
{
    switch (severity)
    {
        case log::Severity::Debug: return GRAY;
        case log::Severity::Info: return WHITE;
        case log::Severity::Warning: return YELLOW;
        case log::Severity::Error: return RED;
        default: return WHITE;
    }
}

void _log_window(PlatformContext* platform, mu_Context* ctx, const char* window_name)
{
    if (mu_begin_window_ex(ctx, window_name, mu_rect(300, 100, 600, 300), MU_OPT_CLOSED))
    {
        static const int layout = -1;
        static const int layout2 = 1000;
        static const int layout3[] = {50, 50, -1};
        static log::LogLineView lines[50];

        uint32_t line_count = hrz::log::get_history(lines);

        mu_layout_row(ctx, 2, layout3, 0);

        if (mu_button(ctx, "Copy"))
        {
            std::string clip_content;
            for (uint32_t i = 0; i < line_count; ++i)
            {
                clip_content += lines[i].line;
                clip_content += "\n";
            }
            hrz::platform::copy_to_clipboard(platform, clip_content);
        }
        if (mu_button(ctx, "Clear"))
        {
            hrz::log::clear_history();
        }

        mu_layout_row(ctx, 1, &layout, -1);
        static ui::StickyPanelState sticky_panel_state;
        ui::begin_sticky_panel(ctx, &sticky_panel_state, "Logs panel");

        mu_layout_row(ctx, 1, &layout2, 0);

        for (uint32_t i = 0; i < line_count; ++i)
        {
            mu_text_color(ctx, lines[i].line, _severity_to_text_color(lines[i].severity));
        }

        ui::end_sticky_panel(ctx, &sticky_panel_state);

        mu_end_window(ctx);
    }
}

void _viewport_dev_ui(Context* ctx, mu_Context* ui, const char* window_name)
{
    if (mu_begin_window_ex(ui, window_name, mu_rect(300, 100, 400, 300), MU_OPT_CLOSED))
    {
        platform::viewport_dev_ui(ctx->platform, ui);
        scene::viewport_dev_ui(ctx->scene, ui);
        mu_end_window(ui);
    }
}

void _traverse_ui(DevUi* ui, Context* ctx)
{
    HRZ_SCOPED_SAMPLE("dev ui traverse");
    assert(ui && ctx);

    int column_width = 180;
    auto rect = mu_rect(0, 0, column_width, ui->window_height);

    mu_begin(&ui->ui_ctx);

    hrz::StaticVector<DevUiWindow, DEV_UI_WINDOW_COUNT> to_open;

    if (mu_begin_window_ex(
            &ui->ui_ctx, ui->main_window_name.c_str(), rect,
            MU_OPT_AUTOSIZE | MU_OPT_NORESIZE | MU_OPT_NOSCROLL | MU_OPT_NOCLOSE))
    {
        int columns_widths[] = {column_width};

        for (int i = 0; i < DEV_UI_WINDOW_COUNT; ++i)
        {
            mu_layout_row(&ui->ui_ctx, 1, columns_widths, 0);
            if (mu_button(&ui->ui_ctx, _window_name((DevUiWindow)i)))
            {
                to_open.push_back((DevUiWindow)i);
            }
        }
        mu_end_window(&ui->ui_ctx);
    }

    for (DevUiWindow window : to_open)
    {
        ui->open_once[(int)window] = true;
        mu_get_container(&ui->ui_ctx, _window_name(window))->open = 1;
    }

    const LayersInfo* layers_info = scene::get_layers_info(ctx->scene);

    hrz::monitoring_dev_ui(
        ctx->monitoring, ctx->remote_monitoring, ctx->job_scheduler, layers_info, ctx->my_instance,
        &ui->ui_ctx, _window_name(DevUiWindow_Monitoring));

    assets_loader::dev_ui(
        ctx->assets_loader, ctx->platform, &ui->ui_ctx, _window_name(DevUiWindow_AssetsLoader));

    blobs::dev_ui(
        ctx->blob_allocator, layers_info, &ui->ui_ctx, _window_name(DevUiWindow_BlobAllocator));

    job_scheduler::dev_ui(ctx->job_scheduler, &ui->ui_ctx, _window_name(DevUiWindow_Jobs));

    _log_window(ctx->platform, &ui->ui_ctx, _window_name(DevUiWindow_Logs));

    planet::raster_group_dev_ui(ctx->planet, &ui->ui_ctx, _window_name(DevUiWindow_Rasters));

    planet::elevation_query_dev_ui(
        ctx->planet, &ui->ui_ctx, _window_name(DevUiWindow_ElevationQueries));

    vector_data::dev_ui(ctx->vector_loader, &ui->ui_ctx, _window_name(DevUiWindow_VectorLoader));

    vector_tiles_layers::dev_ui(
        ctx->vector_tiles_layers, layers_info, &ui->ui_ctx,
        _window_name(DevUiWindow_VectorTilesLayers));

    scene::flat_overlay_dev_ui(
        ctx->scene, &ui->ui_ctx, _window_name(DevUiWindow_VectorFlatOverlays));

    scene::scene_model_dev_ui(
        ctx->scene, ctx->platform, &ui->ui_ctx, _window_name(DevUiWindow_SceneModel));

    debug_draw::dev_ui(ctx->debug_draw, &ui->ui_ctx, _window_name(DevUiWindow_DebugDraw));

    _viewport_dev_ui(ctx, &ui->ui_ctx, _window_name(DevUiWindow_Viewport));

    scene::camera_dev_ui(ctx->scene, &ui->ui_ctx, _window_name(DevUiWindow_Cameras));

    mu_end(&ui->ui_ctx);
}

void update(DevUi* ui, Context* ctx)
{
    HRZ_SCOPED_SAMPLE("dev ui update");
    assert(ui && ctx);

    // Going from an encoded image in the resources to the GPU requires
    // using the blob image decoder, and hence loading the resource into
    // a blob.
    // It would be nice to be able to decode a resource directly, without
    // having to allocate a blob and copy the data into it before. But
    // because this is a very infrequent operation, it's probably not
    // worth the additional code.
    //     -tpetillon, 2022-10-10
    switch (ui->status)
    {
        case DevUi::Status::LoadingAtlas:
            if (!ui->atlas_blob_ticket.is_valid())
            {
                ui->atlas_blob_ticket = blobs::allocate_blob(
                    ctx->blob_allocator,
                    hrz_res::get_data(hrz_res::Resources::DevUiFontAtlas).size_bytes());
                blobs::register_owner(
                    ctx->blob_allocator, ui->atlas_blob_ticket, {monitoring::systems::DevUi});
            }
            else
            {
                auto allocation_state =
                    blobs::get_state(ctx->blob_allocator, ui->atlas_blob_ticket);
                switch (allocation_state)
                {
                    case blobs::BlobState::Allocated:
                    {
                        auto atlas_blob =
                            blobs::to_blob(ctx->blob_allocator, ui->atlas_blob_ticket);
                        {
                            auto blob_data = atlas_blob.get_mutable_data();
                            auto resource_data =
                                hrz_res::get_data(hrz_res::Resources::DevUiFontAtlas);
                            std::memcpy(
                                blob_data.data(), resource_data.data(), resource_data.size_bytes());
                        }
                        ui->atlas_decode_ticket = image_decoder::decode_async(
                            ctx->job_scheduler, std::move(atlas_blob), {monitoring::systems::DevUi},
                            hrz_proto::ImageFormat::R_8);
                        ui->status = DevUi::Status::DecodingAtlas;
                        break;
                    }
                    case blobs::BlobState::Error:
                    {
                        HRZ_LOG_ERROR("Could not allocate dev UI font atlas blob");
                        ui->atlas_blob_ticket.cancel();
                        ui->status = DevUi::Status::Error;
                        break;
                    }
                    default: break;
                }
            }
            break;
        case DevUi::Status::DecodingAtlas:
        {
            if (hrz_jobs::is_job_finished(ctx->job_scheduler, ui->atlas_decode_ticket))
            {
                if (hrz_jobs::get_job_status(ctx->job_scheduler, ui->atlas_decode_ticket)
                    == job_scheduler::JobStatus::Finished_Success)
                {
                    ui->atlas_image =
                        image_decoder::job_to_image(ctx->job_scheduler, ui->atlas_decode_ticket);
                    ui->status = DevUi::Status::UploadingAtlas;
                }
                else
                {
                    HRZ_LOG_ERROR("Could not decode dev UI font atlas");
                    hrz_jobs::cancel_job(ctx->job_scheduler, ui->atlas_decode_ticket);
                    ui->status = DevUi::Status::Error;
                }
            }
            break;
        }
        default: break;
    }

    if (ui->is_open)
    {
        _traverse_ui(ui, ctx);
        _update_geometry(ui);
    }
}

void work_gpu(DevUi* ui, GpuResourceContext* rc)
{
    assert(ui && rc);

    HRZ_SCOPED_SAMPLE("dev ui work GPU");

    if (ui->status == DevUi::Status::UploadingAtlas)
    {
        ui->atlas = hrz::to_gpu(
            ui->atlas_image, rc, false, false, {monitoring::systems::DevUi},
            {{"contents"_ss, "font atlas"_ss}});
        ui->atlas_image = {};
        ui->status = DevUi::Status::Ready;
    }
}

void draw(DevUi* ui, my::RenderContext* rc, float device_pixel_ratio)
{
    assert(ui && rc);

    HRZ_SCOPED_SAMPLE("dev ui draw");

    if (!ui->is_open || ui->status != DevUi::Status::Ready) return;

    {
        size_t size_to_update = sizeof(InstanceData) * ui->instances.size();
        rc->update_buffer(ui->rect.instances_buffer, 0, size_to_update, ui->instances.data());
    }

    {
        UniformsData uniforms;
        uniforms.projection = lm::orthographic_opengl(
            0.0f, (float)ui->window_width / device_pixel_ratio,
            (float)ui->window_height / device_pixel_ratio, 0.0f, 0.0f, 1.0f);

        static const size_t size_to_update = sizeof(UniformsData);
        rc->update_buffer(ui->uniforms, 0, size_to_update, &uniforms);
    }

    const my::UboBinding ubo_binding{0, ui->uniforms, 0, sizeof(UniformsData)};

    const my::TextureBinding texture_binding{
        0,
        ui->atlas,
        ui->atlas_sampler,
    };

    const auto info = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 6)
                          .instanced((uint32_t)ui->instances.size());

    rc->draw(info, ui->shader, ui->rect.input, 1, &ubo_binding, 1, &texture_binding);
}

bool toggle(DevUi* ui)
{
    _toggle_ui(ui);
    return ui->is_open;
}

void move_to_cursor(DevUi* ui)
{
    if (ui->is_open)
    {
        _move_all_windows_to_cursor(ui);
    }
}
} // namespace dev_ui
} // namespace hrz
