#include "hrz_core_scene.h"

#include "camera/hrz_core_camera_system.h"
#include "hrz_core_attribution.h"
#include "hrz_core_client_messages.h"
#include "hrz_core_clipping_plane_layers.h"
#include "hrz_core_debug_draw.h"
#include "hrz_core_events.h"
#include "hrz_core_gizmo_layers.h"
#include "hrz_core_global_flags.h"
#include "hrz_core_picking_id_allocator.h"
#include "hrz_core_platform.h"
#include "hrz_core_render.h"
#include "hrz_core_render_request.h"
#include "hrz_core_scene_model.h"
#include "hrz_core_scene_path.h"
#include "hrz_core_scene_view.h"
#include "hrz_core_selection.h"
#include "hrz_core_shape_editor.h"
#include "hrz_core_single_model_layers.h"
#include "hrz_core_three_d_tiles_layers.h"
#include "planet/hrz_core_planet_geometry.h"
#include "planet/hrz_core_planet_surface.h"
#include "vector/data_loader/hrz_core_vector_data_loader.h"
#include "vector/hrz_core_vector_flat_overlay.h"
#include "vector/hrz_core_vector_heatmaps.h"
#include "vector/hrz_core_vector_in_memory.h"
#include "vector/hrz_core_vector_tiles_layers.h"
#include "vector/symbol/hrz_core_vector_symbol_culling.h"

#include <hrz_common_geo.h>
#include <hrz_common_layers.h>
#include <hrz_common_metrics.h>
#include <hrz_common_proto_geo.h>
#include <hrz_common_proto_maths.h>
#include <hrz_common_proto_settings.h>
#include <hrz_common_ui_utils.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_mem.h>
#include <hrz_fnd_string_utils.h>
#include <hrz_fnd_time.h>
#include <hrz_protocol_path_builder.h>
#include <hrz_scene_model_version.h>

#include <limits>

extern "C"
{
#include <microui/microui.h>
}

#include <hrz_common_profiling.h>

#include <gsl/gsl-lite.hpp>

namespace
{
enum
{
    DEV_UI_LOG_SIZE = 64,
    SCENE_MODEL_MAX_LOG_LINE_LENGTH = 256,
    CAMERA_NOTIFICATION_MAX_LOG_LINE_LENGTH = 128,
};

#define LOG_INVALID_SCENE_VIEW_INDEX(index)                                               \
    HRZ_LOG_ERROR("Invalid scene view index: {}", hrz_proto::SceneViewIndex_Name(index)); \
    static_assert(true, "")

#define LOG_INVALID_CAMERA_INDEX(index)                                            \
    HRZ_LOG_ERROR("Invalid camera index: {}", hrz_proto::CameraIndex_Name(index)); \
    static_assert(true, "")

struct PositionPicking
{
    hrz_proto::SceneViewIndex view;
    hrz::picking::PositionTicket ticket;
};

struct AreaPicking
{
    hrz_proto::SceneViewIndex view;
    hrz::picking::AreaTicket ticket;
};
} // anonymous namespace

namespace hrz
{
struct PlatformContext;

struct Scene : public hrz_proto::ICameraService
{
    using IndexPool = GenIndexPool<uint64_t, 32, 32>;
    using PositionPickingPool = GenObjectPool<PositionPicking, IndexPool, 32>;
    using AreaPickingPool = GenObjectPool<AreaPicking, IndexPool, 32>;

    // @Dirty :Layers Not too fond of keeping a separate set to list active layers.
    // Maybe we could iterate on the buckets of the pool directly.
    //      -slerouzic, 2019-05-02
    IndexPool layer_id_pool;
    LayersInfo layers_info;
    SceneModel* model;
    camera::Camera* cameras[CAMERA_COUNT];
    SelectionSystem* selection;
    SingleModelLayerSystem* single_model_layer_system;
    InMemoryVectorDataBase* in_memory_vector_database;
    VectorDataLoader* vector_data_loader;
    vector_data::VectorDataLoaderChannel vector_data_channel;
    VectorTilesLayerSystem* vector_tiles_layer_system;
    ThreeDTilesLayerSystem* three_d_tiles_layer_system;
    GizmoLayerSystem* gizmo_layer_system;
    ShapeEditor* shape_editor;
    ClippingPlaneLayerSystem* clipping_plane_layer_system;
    DebugDrawSystem* debug_draw;
    SymbolCullingSystem* symbol_culling;
    AttributionRegistry* attributions;

    PlanetSurface* planet;
    PickingIdAllocator* picking_id_allocator;

    hrz::picking::FeatureReference last_quick_highlight_feature_reference;

    uint32_t flat_overlay_cascade_count;
    uint32_t flat_overlay_texture_size;
    uint32_t shadow_map_cascade_count;

    Render render;
    GpuResourceContext resource_context;

    float device_pixel_ratio;
    lm::uvec2 canvas_size;
    bool got_resize_event = false;

    bool scene_settings_model_updated = false;
    hrz_proto::SceneViewIndex main_view;

    struct SceneViewInstance
    {
        SceneView* view;
        RenderViewInfo render_info;
    };

    hrz::flat_hash_map<hrz_proto::SceneViewIndex, SceneViewInstance> views;
    bool has_just_added_views = false;

    // The 'render_request' scene member reflects the real state of the render request as gathered
    // from all the systems. It doesn't take into account the forced rendering flag because it can
    // pervert computations like 'is_working()'.
    RenderRequest render_request;

    PositionPickingPool position_picking_pool;
    AreaPickingPool area_picking_pool;

    struct DevUiLogs
    {
        std::string lines[DEV_UI_LOG_SIZE];
        uint32_t head = 0;
        uint32_t count = 0;

        std::string* get_string_to_write()
        {
            std::string* str = &lines[(head + count) % DEV_UI_LOG_SIZE];
            if (count < DEV_UI_LOG_SIZE) count++;
            if (count == DEV_UI_LOG_SIZE) head = (head + 1) % DEV_UI_LOG_SIZE;
            return str;
        }
    };

    DevUiLogs scene_model_logs;
    DevUiLogs camera_notification_logs;

    virtual ~Scene() = default;

    void set_fixed_target(
        const ::hrz_proto::FixedTargetCameraTransition& input,
        ::hrz_proto::Void& output) override
    {
        if (input.camera_index() >= 0 && (size_t)input.camera_index() < CAMERA_COUNT)
        {
            cameras[input.camera_index()]->go_to_fixed_target(input);
        }
        else
        {
            LOG_INVALID_CAMERA_INDEX(input.camera_index());
        }
    }

    void set_fixed_position(
        const ::hrz_proto::FixedPositionCameraTransition& input,
        ::hrz_proto::Void& output) override
    {
        if (input.camera_index() >= 0 && (size_t)input.camera_index() < CAMERA_COUNT)
        {
            cameras[input.camera_index()]->go_to_fixed_position(input);
        }
        else
        {
            LOG_INVALID_CAMERA_INDEX(input.camera_index());
        }
    }

    void set_orbit(const ::hrz_proto::OrbitCameraTransition& input, ::hrz_proto::Void& output)
        override
    {
        if (input.camera_index() >= 0 && (size_t)input.camera_index() < CAMERA_COUNT)
        {
            SceneModelAccessor accessor(model);

            float fovy = lm::radians(hrz_proto::CameraSettingsPathBuilder<hrz::SceneModelAccessor>(
                                         accessor, input.camera_index())
                                         .fovy()
                                         .get());

            lm::bbox2 viewport = lm::bbox2::invalid();
            for (size_t i = 0; i < SCENE_VIEW_COUNT; ++i)
            {
                auto scene_view = (hrz_proto::SceneViewIndex)(hrz_proto::SCENE_VIEW_0 + i);
                auto camera = hrz_proto::SceneViewSettingsPathBuilder<hrz::SceneModelAccessor>(
                                  accessor, scene_view)
                                  .camera()
                                  .get();

                if (camera != input.camera_index()) continue;

                // The first view that uses the camera has authority on the viewport
                viewport =
                    hrz::to_lm(hrz_proto::SceneViewSettingsPathBuilder<hrz::SceneModelAccessor>(
                                   accessor, scene_view)
                                   .viewport()
                                   .viewport()
                                   .get());
            }

            if (!lm::is_valid(viewport))
            {
                viewport = lm::bbox2({0, 0}, {1, 1});
            }

            lm::dvec2 viewport_pixel_size = lm::dvec2(canvas_size) * lm::size(viewport);
            float aspect_ratio = (float)std::abs(viewport_pixel_size.x / viewport_pixel_size.y);

            cameras[input.camera_index()]->go_to_orbit(input, fovy, aspect_ratio);
        }
        else
        {
            LOG_INVALID_CAMERA_INDEX(input.camera_index());
        }
    }

    void get_camera_pose(const ::hrz_proto::CameraIndexReference& input, ::hrz_proto::Pose& output)
        override
    {
        if (input.camera() >= 0 && (size_t)input.camera() < CAMERA_COUNT)
        {
            output.CopyFrom(cameras[input.camera()]->get_pose());
        }
        else
        {
            LOG_INVALID_CAMERA_INDEX(input.camera());
        }
    }

    void get_camera_angular_viewpoint(
        const ::hrz_proto::CameraIndexReference& input,
        ::hrz_proto::AngularViewpoint& output) override
    {
        if (input.camera() >= 0 && (size_t)input.camera() < CAMERA_COUNT)
        {
            output.CopyFrom(cameras[input.camera()]->get_angular_viewpoint());
        }
        else
        {
            LOG_INVALID_CAMERA_INDEX(input.camera());
        }
    }

    void get_camera_positional_viewpoint(
        const ::hrz_proto::CameraIndexReference& input,
        ::hrz_proto::PositionalViewpoint& output) override
    {
        if (input.camera() >= 0 && (size_t)input.camera() < CAMERA_COUNT)
        {
            output.CopyFrom(cameras[input.camera()]->get_positional_viewpoint());
        }
        else
        {
            LOG_INVALID_CAMERA_INDEX(input.camera());
        }
    }

    hrz_proto::CameraIndex get_current_camera_index(hrz_proto::SceneViewIndex view) const
    {
        hrz::SceneModelAccessor accessor(model);
        return hrz_proto::SceneViewSettingsPathBuilder<hrz::SceneModelAccessor>(accessor, view)
            .camera()
            .get();
    }

    void get_scene_view_view_box(
        const ::hrz_proto::SceneViewReference& input,
        ::hrz_proto::GeographicBounds& output) override
    {
        auto it = views.find(input.scene_view());
        if (it != views.end())
        {
            auto camera_index = get_current_camera_index(it->first);
            ViewportInfo viewport_info =
                scene::compute_viewport_info(it->first, model, device_pixel_ratio, canvas_size);
            output = cameras[camera_index]->get_view_box(viewport_info);
        }
        else
        {
            LOG_INVALID_SCENE_VIEW_INDEX(input.scene_view());
        }
    }

    void get_scene_view_view_polygon(
        const ::hrz_proto::SceneViewReference& input,
        ::hrz_proto::GeographicViewPolygon& output) override
    {
        auto it = views.find(input.scene_view());
        if (it != views.end())
        {
            auto camera_index = get_current_camera_index(it->first);
            ViewportInfo viewport_info =
                scene::compute_viewport_info(it->first, model, device_pixel_ratio, canvas_size);
            output = cameras[camera_index]->get_view_polygon(viewport_info);
        }
        else
        {
            LOG_INVALID_SCENE_VIEW_INDEX(input.scene_view());
        }
    }

    void get_scene_view_scale_and_altitude(
        const ::hrz_proto::SceneViewReference& input,
        ::hrz_proto::ViewScaleAltitude& output) override
    {
        auto it = views.find(input.scene_view());
        if (it != views.end())
        {
            output = scene::get_view_scale_altitude(it->second.view);
        }
        else
        {
            LOG_INVALID_SCENE_VIEW_INDEX(input.scene_view());
            output = hrz_proto::ViewScaleAltitude{};
        }
    }

    std::optional<ViewportInfo> compute_viewport_info_for_all_views(hrz_proto::CameraIndex camera)
    {
        std::optional<ViewportInfo> viewport_info;
        for (const auto& view : views)
        {
            if (get_current_camera_index(view.first) == camera)
            {
                const auto& info = scene::compute_viewport_info(
                    view.first, model, device_pixel_ratio, canvas_size);
                if (!viewport_info)
                {
                    viewport_info = info;
                }
                else
                {
                    viewport_info->subfrustum =
                        lm::merge(viewport_info->subfrustum, info.subfrustum);
                    viewport_info->size = lm::max(viewport_info->size, info.size);
                }
            }
        }
        return viewport_info;
    }

    void get_camera_view_box(
        const ::hrz_proto::CameraIndexReference& input,
        ::hrz_proto::GeographicBounds& output) override
    {
        auto viewport_info = compute_viewport_info_for_all_views(input.camera());
        if (viewport_info)
        {
            output = cameras[input.camera()]->get_view_box(*viewport_info);
        }
        else
        {
            HRZ_LOG_WARNING("Camera {} has no associated view.", (int)input.camera());
        }
    }

    void get_camera_view_polygon(
        const ::hrz_proto::CameraIndexReference& input,
        ::hrz_proto::GeographicViewPolygon& output) override
    {
        auto viewport_info = compute_viewport_info_for_all_views(input.camera());
        if (viewport_info)
        {
            output = cameras[input.camera()]->get_view_polygon(*viewport_info);
        }
        else
        {
            HRZ_LOG_WARNING("Camera {} has no associated view.", (int)input.camera());
        }
    }

    void lat_lon_alt_to_pixel_coords(
        const ::hrz_proto::GeographicPositionInSceneView& input,
        ::hrz_proto::PixelPositionResult& output) override
    {
        output = hrz_proto::PixelPositionResult{};

        auto it = views.find(input.scene_view());
        if (it != views.end())
        {
            auto camera_index = get_current_camera_index(it->first);
            ViewportInfo viewport_info =
                scene::compute_viewport_info(it->first, model, device_pixel_ratio, canvas_size);

            lm::vec2 position;
            bool below_horizon = false;
            bool has_result = cameras[camera_index]->geo_to_screen(
                from_proto(input.pos()), viewport_info, &position, &below_horizon);

            if (has_result)
            {
                lm::ibbox2 bbox = scene::get_event_viewport_on_canvas(it->second.view);
                output.mutable_pixel()->set_x(
                    (int)std::round((position.x + bbox.min.x) / device_pixel_ratio));
                output.mutable_pixel()->set_y(
                    (int)std::round((position.y + bbox.min.y) / device_pixel_ratio));
                output.set_below_horizon(below_horizon);
            }
            else
            {
                output.clear_pixel();
            }
        }
        else
        {
            LOG_INVALID_SCENE_VIEW_INDEX(input.scene_view());
        }
    }

    // Find the scene view that contains the horizontal center of the viewport,
    // and the bottom half vertically. This si important because some movements
    // do picks on this axis to determine the target.
    // Do do that we define a target zone, and choose the view whose viewport
    // has the largest area intersecting it.
    std::optional<hrz_proto::SceneViewIndex> find_center_view_for_camera(
        hrz_proto::CameraIndex cam_index)
    {
        static constexpr float kMargin = 0.005;
        static constexpr lm::bbox2 kTargetZone = {{0.5f - kMargin, 0.5f}, {0.5f + kMargin, 1.0f}};

        float max_area_best_view = std::numeric_limits<float>::min();
        std::optional<hrz_proto::SceneViewIndex> best_view;
        std::optional<hrz_proto::SceneViewIndex> first_view;

        hrz::SceneModelAccessor accessor(model);

        for (size_t i = 0; i < SCENE_VIEW_COUNT; ++i)
        {
            auto scene_view = (hrz_proto::SceneViewIndex)(hrz_proto::SCENE_VIEW_0 + i);
            auto camera = hrz_proto::SceneViewSettingsPathBuilder<hrz::SceneModelAccessor>(
                              accessor, scene_view)
                              .camera()
                              .get();

            if (camera != cam_index) continue;

            auto scissor =
                hrz::to_lm(hrz_proto::SceneViewSettingsPathBuilder<hrz::SceneModelAccessor>(
                               accessor, scene_view)
                               .viewport()
                               .scissor()
                               .get());

            auto intersection = lm::intersection(scissor, kTargetZone);
            float area = lm::area(intersection);
            if (area > max_area_best_view && lm::is_valid(intersection))
            {
                max_area_best_view = area;
                best_view = scene_view;
            }

            if (!first_view)
            {
                first_view = scene_view;
            }
        }

        return best_view ? best_view : first_view;
    }

    void move(const ::hrz_proto::CameraMovement& input, ::hrz_proto::Void& output) override
    {
        if (input.camera_index() >= 0 && (size_t)input.camera_index() < CAMERA_COUNT)
        {
            auto view_index = find_center_view_for_camera(input.camera_index());
            if (view_index)
            {
                auto view_info = scene::compute_viewport_info(
                    *view_index, model, device_pixel_ratio, canvas_size);
                cameras[input.camera_index()]->move(input, *view_index, view_info);
            }
            else
            {
                HRZ_LOG_WARNING("Camera {} has no associated view.", (int)input.camera_index());
            }
        }
        else
        {
            LOG_INVALID_CAMERA_INDEX(input.camera_index());
        }
    }

    void begin_continuous_movement(
        const ::hrz_proto::CameraMovement& input,
        ::hrz_proto::Void& output) override
    {
        if (input.camera_index() >= 0 && (size_t)input.camera_index() < CAMERA_COUNT)
        {
            auto view_index = find_center_view_for_camera(input.camera_index());
            if (view_index)
            {
                auto view_info = scene::compute_viewport_info(
                    *view_index, model, device_pixel_ratio, canvas_size);
                cameras[input.camera_index()]->begin_move(input, *view_index, view_info);
            }
            else
            {
                HRZ_LOG_WARNING("Camera {} has no associated view.", (int)input.camera_index());
            }
        }
        else
        {
            LOG_INVALID_CAMERA_INDEX(input.camera_index());
        }
    }

    void end_continuous_movement(
        const ::hrz_proto::CameraMovement& input,
        ::hrz_proto::Void& output) override
    {
        if (input.camera_index() >= 0 && (size_t)input.camera_index() < CAMERA_COUNT)
        {
            auto view_index = find_center_view_for_camera(input.camera_index());
            if (view_index)
            {
                auto view_info = scene::compute_viewport_info(
                    *view_index, model, device_pixel_ratio, canvas_size);
                cameras[input.camera_index()]->end_move(input, *view_index, view_info);
            }
            else
            {
                HRZ_LOG_WARNING("Camera {} has no associated view.", (int)input.camera_index());
            }
        }
        else
        {
            LOG_INVALID_CAMERA_INDEX(input.camera_index());
        }
    }

    void reset_north(const ::hrz_proto::ResetNorthParams& input, ::hrz_proto::Void& output) override
    {
        if (input.camera_index() >= 0 && (size_t)input.camera_index() < CAMERA_COUNT)
        {
            cameras[input.camera_index()]->reset_north(input);
        }
        else
        {
            LOG_INVALID_CAMERA_INDEX(input.camera_index());
        }
    }
};

namespace scene
{
namespace
{
void layer_path_to_buffer(
    const Scene* scene,
    const hrz_proto::Path& path,
    fmt::memory_buffer* buffer)
{
    const char* layer_type_name = "<Unknown layer type>";
    uint64_t layer_handle = 0;
    std::string path_str;

    switch (path.root().kind_case())
    {
        case hrz_proto::PathRoot::kSingleModelLayer:
            layer_type_name = "SingleModelLayer";
            layer_handle = path.root().single_model_layer().opaque();
            path_str = scene_model::SingleModelLayerPath(path).to_string();
            break;
        case hrz_proto::PathRoot::kDtmRasterLayer:
            layer_type_name = "DtmRasterLayer";
            layer_handle = path.root().dtm_raster_layer().opaque();
            path_str = scene_model::DtmRasterLayerPath(path).to_string();
            break;
        case hrz_proto::PathRoot::kImageryRasterLayer:
            layer_type_name = "ImageryRasterLayer";
            layer_handle = path.root().imagery_raster_layer().opaque();
            path_str = scene_model::ImageryRasterLayerPath(path).to_string();
            break;
        case hrz_proto::PathRoot::kVectorDataLayer:
            layer_type_name = "VectorDataLayer";
            layer_handle = path.root().vector_data_layer().opaque();
            path_str = scene_model::VectorDataLayerPath(path).to_string();
            break;
        case hrz_proto::PathRoot::kVectorTilesLayer:
            layer_type_name = "VectorTilesLayer";
            layer_handle = path.root().vector_tiles_layer().opaque();
            path_str = scene_model::VectorTilesLayerPath(path).to_string();
            break;
        case hrz_proto::PathRoot::kThreeDTilesLayer:
            layer_type_name = "ThreeDTilesLayer";
            layer_handle = path.root().three_d_tiles_layer().opaque();
            path_str = scene_model::ThreeDTilesLayerPath(path).to_string();
            break;
        case hrz_proto::PathRoot::kGizmoLayer:
            layer_type_name = "GizmoLayer";
            layer_handle = path.root().gizmo_layer().opaque();
            path_str = scene_model::GizmoLayerPath(path).to_string();
            break;
        case hrz_proto::PathRoot::kEditableShapeLayer:
            layer_type_name = "EditableShapeLayer";
            layer_handle = path.root().editable_shape_layer().opaque();
            path_str = scene_model::EditableShapeLayerPath(path).to_string();
            break;
        case hrz_proto::PathRoot::kClippingPlaneLayer:
            layer_type_name = "ClippingPlaneLayer";
            layer_handle = path.root().clipping_plane_layer().opaque();
            path_str = scene_model::ClippingPlaneLayerPath(path).to_string();
            break;
        case hrz_proto::PathRoot::kInMemoryVectorSourceLayer:
            layer_type_name = "InMemoryVectorSourceLayer";
            layer_handle = path.root().in_memory_vector_source_layer().opaque();
            path_str = scene_model::InMemoryVectorSourceLayerPath(path).to_string();
            break;
        default: assert(0 && "Not implemented"); break;
    }

    const char* layer_name = "<Unknown layer>";
    auto layer_it = scene->layers_info.layers.find(layer_handle);
    if (layer_it != scene->layers_info.layers.end())
    {
        layer_name = layer_it->second.name.c_str();
    }

    fmt::format_to(
        std::back_inserter(*buffer), "[Layer \"{}\"] /{}{}", layer_name, layer_type_name, path_str);
}

std::string_view path_to_string(const Scene* scene, const hrz_proto::Path& path)
{
    static fmt::memory_buffer buffer;
    buffer.clear();

    switch (path.root().kind_case())
    {
        case hrz_proto::PathRoot::kSingleModelLayer:
        case hrz_proto::PathRoot::kDtmRasterLayer:
        case hrz_proto::PathRoot::kImageryRasterLayer:
        case hrz_proto::PathRoot::kVectorDataLayer:
        case hrz_proto::PathRoot::kVectorTilesLayer:
        case hrz_proto::PathRoot::kThreeDTilesLayer:
        case hrz_proto::PathRoot::kGizmoLayer:
        case hrz_proto::PathRoot::kEditableShapeLayer:
        case hrz_proto::PathRoot::kClippingPlaneLayer:
        case hrz_proto::PathRoot::kInMemoryVectorSourceLayer:
            layer_path_to_buffer(scene, path, &buffer);
            break;
        case hrz_proto::PathRoot::kSceneViewSettings:
            fmt::format_to(
                std::back_inserter(buffer), "[Scene view index: {}] /SceneViewSettings{}",
                hrz_proto::SceneViewIndex_Name(path.root().scene_view_settings()),
                scene_model::SceneViewSettingsPath(path).to_string());
            break;
        case hrz_proto::PathRoot::kSceneSettings:
            fmt::format_to(
                std::back_inserter(buffer), "/SceneSettings{}",
                scene_model::SceneSettingsPath(path).to_string());
            break;
        case hrz_proto::PathRoot::kCameraSettings:
            fmt::format_to(
                std::back_inserter(buffer), "[Camera index: {}] /CameraSettings{}",
                hrz_proto::CameraIndex_Name(path.root().camera_settings()),
                scene_model::CameraSettingsPath(path).to_string());
            break;
        default: assert(0 && "Not implemented"); break;
    }

    buffer.push_back(0);
    return {buffer.data(), buffer.size()};
}

void add_scene_model_log_line(Scene* scene, const hrz_proto::Path& path, const std::string& prefix)
{
    HRZ_INCREMENT_COUNTER("Scene model modification count", {});

    auto& logs = scene->scene_model_logs;

    double since_epoch = hrz::now_frame_ms() / 1000.0;
    double millis = std::floor((since_epoch - std::floor(since_epoch)) * 1000.0);
    int minutes = std::floor(since_epoch / 60.0);
    int seconds = std::floor(since_epoch) - 60.0 * minutes;

    std::string_view path_str = path_to_string(scene, path);
    std::string* str = logs.get_string_to_write();
    str->resize(SCENE_MODEL_MAX_LOG_LINE_LENGTH);
    size_t length =
        fmt::format_to_n(
            &*str->begin(), SCENE_MODEL_MAX_LOG_LINE_LENGTH, "[{:>4}:{:02}.{:03}] {:<7} {}",
            minutes, seconds, millis, prefix, path_str.data())
            .size;
    str->resize(length);
}

void clear_scene_model_logs(Scene* scene)
{
    auto& logs = scene->scene_model_logs;
    logs.head = 0;
    logs.count = 0;
}

std::string_view get_default_layer_name(Scene* scene, hrz_proto::LayerType type)
{
    static fmt::memory_buffer buffer;
    buffer.clear();

    size_t index = scene->layers_info.layer_type_counters[type] + 1;

    switch (type)
    {
        case hrz_proto::LayerType::SINGLE_MODEL:
            fmt::format_to(std::back_inserter(buffer), "Single model {}", index);
            break;
        case hrz_proto::LayerType::DTM_RASTER:
            fmt::format_to(std::back_inserter(buffer), "DTM raster {}", index);
            break;
        case hrz_proto::LayerType::IMAGERY_RASTER:
            fmt::format_to(std::back_inserter(buffer), "Imagery raster {}", index);
            break;
        case hrz_proto::LayerType::VECTOR_DATA:
            fmt::format_to(std::back_inserter(buffer), "Vector data {}", index);
            break;
        case hrz_proto::LayerType::VECTOR_TILES:
            fmt::format_to(std::back_inserter(buffer), "Vector tiles {}", index);
            break;
        case hrz_proto::LayerType::THREE_D_TILES:
            fmt::format_to(std::back_inserter(buffer), "3D tiles {}", index);
            break;
        case hrz_proto::LayerType::CLIPPING_PLANE:
            fmt::format_to(std::back_inserter(buffer), "Clipping plane {}", index);
            break;
        case hrz_proto::LayerType::IN_MEMORY_VECTOR_SOURCE:
            fmt::format_to(std::back_inserter(buffer), "In-memory vector source {}", index);
            break;
        case hrz_proto::LayerType::GIZMO:
            fmt::format_to(std::back_inserter(buffer), "Gizmo {}", index);
            break;
        case hrz_proto::LayerType::EDITABLE_SHAPE:
            fmt::format_to(std::back_inserter(buffer), "Editable shape {}", index);
            break;
        default:
            assert(false && "Unhandled case");
            HRZ_LOG_WARNING("Unhandled layer type for computing default name.");
            fmt::format_to(std::back_inserter(buffer), "New layer");
            break;
    }
    return {buffer.data(), buffer.size()};
}
} // anonymous namespace

void notify_model_update(
    Scene* scene,
    scene_model::UpdateType update_type,
    const scene_model::SceneSettingsPath& path)
{
    scene->scene_settings_model_updated = true;
    planet::notify_model_update(scene->planet, update_type, path);
}

void notify_model_update(
    Scene* scene,
    scene_model::UpdateType update_type,
    const scene_model::CameraSettingsPath& path)
{
    scene->cameras[path.get_root()]->notify_model_update(update_type, path);
}

void notify_model_update(
    Scene* scene,
    scene_model::UpdateType update_type,
    const scene_model::SceneViewSettingsPath& path)
{
    auto view_index = path.get_root();
    auto it = scene->views.find(view_index);
    if (it != scene->views.end())
    {
        notify_model_update(it->second.view, update_type, path);
    }
}

inline void _assign_my_instance(Scene* scene, my::Instance* my, GpuResourceContext* gpu_rc)
{
    scene->render.my = my;
    *scene->render.rc = *gpu_rc;
}

Scene* create(
    uint32_t imagery_merge_group_count,
    uint32_t raster_atlas_size,
    bool compress_atlas_textures,
    uint32_t default_raster_provider_tile_cache_size,
    uint32_t flat_overlay_cascade_count,
    uint32_t flat_overlay_texture_size,
    uint32_t shadow_map_cascade_count,
    lm::uvec2 canvas_size,
    float device_pixel_ratio,
    const PlatformInfo& platform_info,
    const my::Instance::Info& my_instance_info,
    AssetsLoader* al)
{
    hrz_proto::SceneSettings scene_settings = default_scene_settings();
    hrz_proto::SceneViewSettings scene_view_settings = default_scene_view_settings();
    hrz_proto::CameraSettings camera_settings = default_camera_settings();

    auto scene = new Scene();
    scene->picking_id_allocator = hrz::picking::create_id_allocator();

    scene->model = scene_model::create();
    scene->in_memory_vector_database = vector_data::in_memory::create_system();
    scene->vector_data_loader = vector_data::create_loader(al, scene->in_memory_vector_database);
    scene->vector_data_channel = vector_data::create_channel(scene->vector_data_loader);
    scene->single_model_layer_system =
        single_model_layers::create_system(scene->picking_id_allocator);
    scene->vector_tiles_layer_system =
        vector_tiles_layers::create_system(scene->picking_id_allocator);
    scene->three_d_tiles_layer_system =
        three_d_tiles_layers::create_system(scene->picking_id_allocator, scene->vector_data_loader);
    scene->gizmo_layer_system = gizmo_layers::create_system();
    scene->shape_editor = editor::create_editor(scene->picking_id_allocator);
    scene->clipping_plane_layer_system = clipping_plane_layers::create_system();
    scene->debug_draw = debug_draw::create_system();
    scene->symbol_culling = symbol_culling::create();
    scene->canvas_size = canvas_size;
    scene->device_pixel_ratio = device_pixel_ratio;
    scene->attributions = attribution::create_registry();

    auto register_scene_view = [&](hrz_proto::SceneViewIndex view)
    {
        hrz_proto::PathRoot root;
        root.set_scene_view_settings(view);
        scene_model::register_element(scene->model, root);

        SceneModelAccessor accessor(scene->model);
        hrz_proto::SceneViewSettingsPathBuilder<SceneModelAccessor> builder(accessor, view);
        builder.set(scene_view_settings);
    };

    for (size_t i = 0; i < (size_t)SCENE_VIEW_COUNT; ++i)
    {
        register_scene_view((hrz_proto::SceneViewIndex)(hrz_proto::SCENE_VIEW_0 + i));
    }

    SceneModelAccessor accessor(scene->model);
    {
        hrz_proto::SceneSettingsPathBuilder<SceneModelAccessor> builder(accessor);
        builder.set(scene_settings);
    }

    auto register_camera = [&](hrz_proto::CameraIndex camera)
    {
        hrz_proto::PathRoot root;
        root.set_camera_settings(camera);
        scene_model::register_element(scene->model, root);

        SceneModelAccessor accessor(scene->model);
        hrz_proto::CameraSettingsPathBuilder<SceneModelAccessor> builder(accessor, camera);
        builder.set(camera_settings);

        scene->cameras[camera] = camera::create(
            camera, camera_settings.fovy(), camera_settings.user_controls_inertia(),
            camera_settings.movements_inertia(), camera_settings.min_height_above_terrain(),
            camera_settings.terrain_collision_inertia());
    };

    for (size_t i = 0; i < (size_t)CAMERA_COUNT; ++i)
    {
        register_camera((hrz_proto::CameraIndex)(hrz_proto::CAMERA_0 + i));
    }

    scene->planet = planet::create_surface(
        imagery_merge_group_count, raster_atlas_size, compress_atlas_textures,
        default_raster_provider_tile_cache_size, platform_info, my_instance_info,
        scene->picking_id_allocator);
    scene->selection = selection::create();

    scene->flat_overlay_cascade_count = flat_overlay_cascade_count;
    scene->flat_overlay_texture_size = flat_overlay_texture_size;
    scene->shadow_map_cascade_count = shadow_map_cascade_count;

    scene->main_view = hrz_proto::SCENE_VIEW_0;

    // Notify all subsystems so they can fetch their default data.
    {
        auto path_builder = hrz_proto::SceneSettingsPathBuilder<int>(0);
        auto path = scene_model::SceneSettingsPath(path_builder._path);
        notify_model_update(scene, scene_model::UpdateType::Set, path);
    }

    scene->render.rc = &scene->resource_context;
    scene->render.rd = my::Renderer::create();
    scene->render.rb = my::ResourceBinder::create();

    return scene;
}

void destroy(
    Scene* scene,
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js,
    FontRasterizer* fr,
    my::Instance* my,
    GpuResourceContext* gpu_rc)
{
    assert(scene);
    _assign_my_instance(scene, my, gpu_rc);

    attribution::destroy(scene->attributions);

    debug_draw::destroy_system(scene->debug_draw, &scene->render);

    editor::destroy_editor(scene->shape_editor, scene->picking_id_allocator, &scene->render);

    selection::destroy(scene->selection);

    single_model_layers::destroy_system(
        scene->single_model_layer_system, &scene->render, al, js, ba, scene->picking_id_allocator,
        scene->model, scene->planet);

    vector_tiles_layers::destroy_system(
        scene->vector_tiles_layer_system, &scene->render, al, ba, js, fr, scene->symbol_culling,
        scene->picking_id_allocator, scene->model, scene->planet);

    three_d_tiles_layers::destroy_system(
        scene->three_d_tiles_layer_system, al, js, ba, &scene->render, scene->picking_id_allocator,
        scene->model);

    clipping_plane_layers::destroy_system(scene->clipping_plane_layer_system, &scene->render);

    vector_data::destroy_loader(scene->vector_data_loader, js);
    vector_data::in_memory::destroy_system(scene->in_memory_vector_database, scene->model, ba);

    gizmo_layers::destroy_system(scene->gizmo_layer_system, &scene->render, scene->model);

    symbol_culling::destroy(scene->symbol_culling);

    scene_model::destroy(scene->model);

    for (size_t i = 0; i < (size_t)CAMERA_COUNT; ++i)
    {
        camera::destroy(scene->cameras[i]);
    }

    planet::destroy(scene->planet, al, ba, js, scene->picking_id_allocator, &scene->render);
    picking::destroy_id_allocator(scene->picking_id_allocator);

    for (auto& entry : scene->views)
    {
        destroy_scene_view(entry.second.view, al, js, &scene->render);
    }
    scene->views.clear();

    delete scene->render.rb;
    delete scene->render.rd;
    delete scene;
}

void initialize_rendering(Scene* scene, my::Instance* my, GpuResourceContext* gpu_rc)
{
    _assign_my_instance(scene, my, gpu_rc);

    scene->render.rd->register_bin(hrz::RenderPlanetBinBit, my::DepthSortMode::FrontToBack);
    scene->render.rd->register_bin(hrz::RenderWorldOpaqueBinBit, my::DepthSortMode::FrontToBack);
    scene->render.rd->register_bin(
        hrz::RenderWorldTransparentBinBit, my::DepthSortMode::BackToFront);
    scene->render.rd->register_bin(hrz::RenderHeatmapBinBit, my::DepthSortMode::NoSort);
    scene->render.rd->register_bin(hrz::RenderFlatOverlayBinBit, my::DepthSortMode::NoSort);
    scene->render.rd->register_bin(hrz::RenderDecalBinBit, my::DepthSortMode::FrontToBack);
    scene->render.rd->register_bin(hrz::RenderSymbolicBinBit, my::DepthSortMode::BackToFront);
    scene->render.rd->register_bin(
        hrz::RenderSymbolicOverlayBinBit, my::DepthSortMode::BackToFront);
    scene->render.rd->register_bin(hrz::RenderInWorldUiBinBit, my::DepthSortMode::FrontToBack);
    scene->render.rd->register_bin(
        hrz::RenderUiBinBit,
        (hrz::get_flag(hrz::Flag::EnabledDepthPeelingForUiElements))
            ? my::DepthSortMode::FrontToBack
            : my::DepthSortMode::BackToFront);

    planet::initialize_rendering(scene->planet, &scene->render);
    single_model_layers::initialize_rendering(scene->single_model_layer_system, &scene->render);
    vector_tiles_layers::initialize_rendering(scene->vector_tiles_layer_system, &scene->render);
    three_d_tiles_layers::initialize_rendering(scene->three_d_tiles_layer_system, &scene->render);
    gizmo_layers::initialize_rendering(scene->gizmo_layer_system, &scene->render);
    clipping_plane_layers::initialize_rendering(scene->clipping_plane_layer_system, &scene->render);
    editor::initialize_rendering(scene->shape_editor, &scene->render);
    debug_draw::initialize_rendering(scene->debug_draw, &scene->render);
}

StaticVector<std::pair<my::ResourceHandle, lm::ibbox2>, SCENE_VIEW_COUNT> get_color_outputs(
    Scene* scene)
{
    StaticVector<std::pair<my::ResourceHandle, lm::ibbox2>, SCENE_VIEW_COUNT> vec;
    for (auto& entry : scene->views)
    {
        vec.push_back(std::make_pair(
            get_color_output(entry.second.view),
            get_color_output_viewport_on_canvas(entry.second.view)));
    }
    return vec;
}

bool handle_event(Scene* scene, const Event& event, float device_pixel_ratio)
{
    if (event.kind == Event::Kind::Platform
        && event.platform.kind == platform::Event::Kind::WindowResized)
    {
        uint32_t w = (uint32_t)event.platform.window_resized.width;
        uint32_t h = (uint32_t)event.platform.window_resized.height;

        scene->canvas_size = {w, h};
        scene->device_pixel_ratio = device_pixel_ratio;
        scene->got_resize_event = true;

        for (auto& entry : scene->views)
        {
            set_canvas_size(
                entry.second.view, scene->model, scene->canvas_size, device_pixel_ratio);
        }
    }

    for (auto& entry : scene->views)
    {
        ViewportEvent viewport_event = make_viewport_event(entry.second.view, event);

        if (gizmo_layers::handle_event(
                scene->gizmo_layer_system, viewport_event, entry.first,
                entry.second.render_info.cam_view_info))
        {
            return true;
        }
        else if (editor::handle_event(
                     scene->shape_editor, viewport_event, entry.first,
                     entry.second.render_info.cam_view_info))
        {
            return true;
        }

        auto* camera = scene->cameras[scene->get_current_camera_index(entry.first)];
        assert(camera);
        camera->handle_event(viewport_event, entry.first, entry.second.render_info.cam_view_info);
    }

    return false;
}

void update_from_model(Scene* scene, AssetsLoader* al, BlobAllocator* ba, JobScheduler* js)
{
    HRZ_SCOPED_SAMPLE("scene update from model");

    SceneModelAccessor accessor(scene->model);
    hrz_proto::SceneSettingsPathBuilder<SceneModelAccessor> builder(accessor);
    auto settings = builder.get();

    scene->main_view = settings.main_view();
    uint32_t bitset = settings.active_views().bits();

    auto handle_view = [&](hrz_proto::SceneViewIndex view_index)
    {
        auto view_it = scene->views.find(view_index);
        bool has_view = view_it != scene->views.end();
        bool needs_view = (bitset & (1 << view_index)) != 0;

        if (has_view && !needs_view)
        {
            destroy_scene_view(view_it->second.view, al, js, &scene->render);
            scene->views.erase(view_it);
        }
        else if (!has_view && needs_view)
        {
            SceneView* view = create_scene_view(
                scene->model, view_index, scene->canvas_size, scene->device_pixel_ratio,
                scene->flat_overlay_cascade_count, scene->flat_overlay_texture_size,
                scene->shadow_map_cascade_count);
            initialize_rendering(view, &scene->render);
            scene->views.insert(std::make_pair(view_index, Scene::SceneViewInstance{view, {}}));
            scene->has_just_added_views = true;
        }
    };

    for (unsigned int i = 0; i < SCENE_VIEW_COUNT; ++i)
    {
        handle_view((hrz_proto::SceneViewIndex)(hrz_proto::SCENE_VIEW_0 + i));
    }

    if ((bitset & (1 << scene->main_view)) == 0)
    {
        HRZ_LOG_ERROR("The main view is not active, choosing a random one instead...");

        for (unsigned int i = 0; i < SCENE_VIEW_COUNT; ++i)
        {
            if ((bitset & (1 << i)) != 0)
            {
                scene->main_view = (hrz_proto::SceneViewIndex)(hrz_proto::SCENE_VIEW_0 + i);
                break;
            }
        }
    }

    scene->scene_settings_model_updated = false;
}

void work_start_frame(
    Scene* scene,
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js,
    my::Instance* my,
    GpuResourceContext* gpu_rc)
{
    HRZ_SCOPED_SAMPLE("scene start frame");

    scene->has_just_added_views = false;
    _assign_my_instance(scene, my, gpu_rc);

    scene->render_request.reset();

    if (hrz::get_flag(hrz::Flag::ForceRender))
    {
        scene->render_request.request_visual_render(RenderRequest::VisualCause::Animation);
        scene->render_request.schedule_flat_overlay_render();
    }

    if (scene->scene_settings_model_updated)
    {
        update_from_model(scene, al, ba, js);
        scene->render_request.request_visual_render();
        scene->render_request.schedule_planet_feedback();
        scene->render_request.schedule_flat_overlay_render();
    }

    for (auto& entry : scene->views)
    {
        work_start_frame(entry.second.view, scene->shape_editor, &scene->render);
    }
}

SceneView* get_main_scene_view(Scene* scene)
{
    auto it = scene->views.find(scene->main_view);
    if (it != scene->views.end())
    {
        return it->second.view;
    }
    else
    {
        return nullptr;
    }
}

static const char* notification_kind_name(hrz_proto::CameraNotification::KindCase notification)
{
    switch (notification)
    {
        case hrz_proto::CameraNotification::kAnimationStarted: return "Animation started";
        case hrz_proto::CameraNotification::kAnimationEnded: return "Animation ended";
        case hrz_proto::CameraNotification::kAnimationInterrupted: return "Animation interrupted";
        case hrz_proto::CameraNotification::kContinuousMovementStarted:
            return "Cont. movement started";
        case hrz_proto::CameraNotification::kContinuousMovementEnded: return "Cont. movement ended";
        case hrz_proto::CameraNotification::kContinuousMovementResumed:
            return "Cont. movement resumed";
        case hrz_proto::CameraNotification::kContinuousMovementInterruptedNoResume:
            return "Cont. movement interruped";
        case hrz_proto::CameraNotification::kContinuousMovementInterruptedWillResume:
            return "Cont. movement interruped, will resume";
        case hrz_proto::CameraNotification::kMotionStarted: return "Motion started";
        case hrz_proto::CameraNotification::kMotionEnded: return "Motion ended";
        default: return "Unknown notification";
    }
};

static const char* notification_payload_str(const hrz_proto::CameraNotification& notification)
{
    switch (notification.kind_case())
    {
        case hrz_proto::CameraNotification::kContinuousMovementStarted:
            return hrz_proto::CameraMovementType_Name(notification.continuous_movement_started())
                .c_str();
        case hrz_proto::CameraNotification::kContinuousMovementEnded:
            return hrz_proto::CameraMovementType_Name(notification.continuous_movement_ended())
                .c_str();
        case hrz_proto::CameraNotification::kContinuousMovementResumed:
            return hrz_proto::CameraMovementType_Name(notification.continuous_movement_resumed())
                .c_str();
        case hrz_proto::CameraNotification::kContinuousMovementInterruptedNoResume:
            return hrz_proto::CameraMovementType_Name(
                       notification.continuous_movement_interrupted_no_resume())
                .c_str();
        case hrz_proto::CameraNotification::kContinuousMovementInterruptedWillResume:
            return hrz_proto::CameraMovementType_Name(
                       notification.continuous_movement_interrupted_will_resume())
                .c_str();
        default: return "no payload";
    }
};

void add_camera_notification_log_line(
    Scene* scene,
    hrz_proto::CameraIndex index,
    const hrz_proto::CameraNotification& notification)
{
    auto& logs = scene->camera_notification_logs;

    double since_epoch = hrz::now_frame_s();
    double millis = std::floor((since_epoch - std::floor(since_epoch)) * 1000.0);
    int minutes = (int)(since_epoch / 60.0);
    int seconds = (int)since_epoch - 60 * minutes;

    std::string* str = logs.get_string_to_write();
    str->resize(CAMERA_NOTIFICATION_MAX_LOG_LINE_LENGTH);
    size_t length = fmt::format_to_n(
                        &*str->begin(), CAMERA_NOTIFICATION_MAX_LOG_LINE_LENGTH,
                        "[{:>4}:{:02}.{:03}] Camera {}: {} ({})", minutes, seconds, millis,
                        (int)index, notification_kind_name(notification.kind_case()),
                        notification_payload_str(notification))
                        .size;
    str->resize(length);
}

void work(
    Scene* scene,
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js,
    ImageDecoder* imgdec,
    FontRasterizer* fr,
    ClientMessageQueue* mq,
    ActorRunner* ar)
{
    assert(scene && al && js && ba && imgdec && fr && mq);
    HRZ_SCOPED_SAMPLE("scene work");

    auto request_renders = [&]()
    {
        scene->render_request.request_visual_render();
        scene->render_request.schedule_planet_feedback();
        scene->render_request.schedule_flat_overlay_render();
    };

    if (scene->got_resize_event)
    {
        request_renders();
        scene->got_resize_event = false;
    }

    hrz::SceneModelAccessor accessor(scene->model);

    std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT> picking_systems{};
    std::array<hrz_proto::CameraIndex, hrz::SCENE_VIEW_COUNT> view_to_camera_index{};
    std::array<ViewportInfo, hrz::SCENE_VIEW_COUNT> view_to_viewport_info{};
    std::array<double, hrz::SCENE_VIEW_COUNT> view_to_height_above_terrain{};

    for (const auto& view : scene->views)
    {
        hrz_proto::SceneViewSettingsPathBuilder<hrz::SceneModelAccessor> builder(
            accessor, view.first);
        auto camera_index = builder.camera().get();

        if (camera_index >= 0 && (size_t)camera_index < hrz::CAMERA_COUNT)
        {
            view_to_camera_index[view.first] = camera_index;
        }

        picking_systems[view.first] = get_picking_system(view.second.view);

        view_to_viewport_info[view.first] = compute_viewport_info(
            view.first, scene->model, scene->device_pixel_ratio, scene->canvas_size);

        view_to_height_above_terrain[view.first] = get_camera_height(view.second.view);
    }

    for (size_t i = 0; i < CAMERA_COUNT; ++i)
    {
        auto center_view_index = scene->find_center_view_for_camera((hrz_proto::CameraIndex)i)
                                     .value_or(scene->main_view);

        auto camera = scene->cameras[i];
        bool camera_has_moved = camera->work(
            scene->model, view_to_viewport_info[center_view_index],
            view_to_height_above_terrain[center_view_index], picking_systems, scene->planet,
            [&](gsl::span<const hrz_proto::CameraNotification> notifications)
            {
                for (const auto& notification : notifications)
                {
                    hrz_proto::CameraNotification message;
                    message.MergeFrom(notification);
                    message.set_camera_index((hrz_proto::CameraIndex)i);
                    add_camera_notification_log_line(scene, (hrz_proto::CameraIndex)i, message);
                    client_message_queue::enqueue_camera_notification_message(
                        mq, std::move(message));
                }
            });

        if (camera_has_moved)
        {
            request_renders();
        }
    }

    StaticVector<PlanetGeometry*, SCENE_VIEW_COUNT> planet_geometries;
    StaticVector<RenderViewInfo, SCENE_VIEW_COUNT> views_info;

    for (auto& entry : scene->views)
    {
        RenderViewInfo view_info;
        view_info.view = entry.first;

        const auto& camera_info = scene->cameras[view_to_camera_index[entry.first]]->get_info();
        const auto& viewport_info = view_to_viewport_info[entry.first];

        view_info.cam_view_info =
            CameraViewInfo::make_from_camera_and_viewport(camera_info, viewport_info);

        view_info.all_views = 0;
        view_info.view_main = 0;
        view_info.height_above_terrain = get_camera_height(entry.second.view);
        view_info.perceived_distance =
            camera_info.perceived_distance(view_info.height_above_terrain);

        entry.second.render_info = view_info;
        views_info.push_back(view_info);

        planet_geometries.push_back(get_planet_geometry(entry.second.view));
    }

    vector_data::in_memory::work(
        scene->in_memory_vector_database, scene->model, ba, scene->attributions);

    scene->render_request |= single_model_layers::work(
        scene->single_model_layer_system, scene->model, al, js, ba, imgdec, scene->selection,
        scene->attributions, scene->planet, views_info);

    scene->render_request |= symbol_culling::work(scene->symbol_culling, js, views_info);

    scene->render_request |= vector_tiles_layers::work(
        scene->vector_tiles_layer_system, scene->model, scene->vector_data_loader, al, ba, js,
        imgdec, fr, scene->symbol_culling, scene->attributions, ar, views_info, scene->planet,
        scene->selection);

    assert(scene->views.size() <= SCENE_VIEW_COUNT);

    scene->render_request |= three_d_tiles_layers::work(
        scene->three_d_tiles_layer_system, scene->model, scene->selection, al, js, ba, imgdec,
        scene->attributions, views_info);

    scene->render_request |= gizmo_layers::work(
        scene->gizmo_layer_system, scene->model, mq, scene->main_view, views_info);
    scene->render_request |= editor::work(scene->shape_editor, scene->model, mq);

    planet::work(
        scene->planet, al, ba, js, scene->model, scene->attributions, views_info,
        planet_geometries);

    bool rasters_have_changed = planet::layer_work(scene->planet, scene->model, al, ba, js);
    if (rasters_have_changed)
    {
        scene->render_request.request_visual_render();
        scene->render_request.schedule_planet_feedback();
    }

    scene->render_request |=
        clipping_plane_layers::work(scene->clipping_plane_layer_system, scene->model);

    ClippingPlaneInfo cpi[HRZ_S_MAX_CLIP_PLANES];
    clipping_plane_layers::get_clip_planes_info(scene->clipping_plane_layer_system, cpi);

    const HeatmapReprRegistry* heatmap_repr_registry =
        vector_tiles_layers::get_heatmap_repr_registry(scene->vector_tiles_layer_system);

    scene_model::defrag(scene->model);

    for (auto& entry : scene->views)
    {
        scene->render_request |= work(
            entry.second.view, scene->model, entry.second.render_info.cam_view_info,
            scene->canvas_size, scene->device_pixel_ratio, cpi, scene->planet,
            heatmap_repr_registry, scene->last_quick_highlight_feature_reference);
    }

    if (selection::has_changed_since_last_frame(scene->selection) || scene->has_just_added_views)
    {
        bool enable_highlight = selection::selected_objects_count(scene->selection) > 0;

        for (auto& entry : scene->views)
        {
            set_highlight_enabled(entry.second.view, enable_highlight);
        }

        scene->render_request.request_visual_render();
        scene->render_request.schedule_flat_overlay_render();
    }

    selection::finish_frame(scene->selection);
}

void register_views(Scene* scene)
{
    StaticVector<my::Renderer::ViewId, SCENE_VIEW_COUNT> main_views;
    std::vector<my::Renderer::ViewId> scene_view_views;

    assert(scene->views.size() <= SCENE_VIEW_COUNT);

    for (auto& entry : scene->views)
    {
        const CameraViewInfo& view_info = entry.second.render_info.cam_view_info;
        my::Renderer::ViewId view_id;

        if (entry.first == scene->main_view)
        {
            view_id = my::MainView;

            scene->render.rd->set_main_view(
                my::View{
                    view_info.proj,
                    view_info.cam.view,
                },
                hrz::RenderAllPhysicalBins);
        }
        else
        {
            view_id = scene->render.rd->add_auxiliary_view(
                my::View{
                    view_info.proj,
                    view_info.cam.view,
                },
                hrz::RenderAllPhysicalBins);
        }

        main_views.push_back(view_id);

        scene_view_views.clear();
        scene_view_views.push_back(view_id);

        register_aux_views(entry.second.view, &scene->render, view_id, scene_view_views);

        entry.second.render_info.view_main = view_id;
        entry.second.render_info.all_views =
            scene->render.rd->make_view_mask((int)scene_view_views.size(), scene_view_views.data());
    }

    scene->render.main_views =
        scene->render.rd->make_view_mask((int)main_views.size(), main_views.data());
}

void work_gpu(Scene* scene, my::Instance* my, GpuResourceContext* gpu_rc, BlobAllocator* ba)
{
    assert(scene && my && gpu_rc && ba);

    _assign_my_instance(scene, my, gpu_rc);

    register_views(scene);

    StaticVector<RenderViewInfo, SCENE_VIEW_COUNT> views_info;
    for (const auto& entry : scene->views)
    {
        views_info.push_back(entry.second.render_info);
    }

    scene->render_request |= planet::work_gpu(scene->planet, &scene->render, ba);
    scene->render_request |=
        single_model_layers::work_gpu(scene->single_model_layer_system, &scene->render, ba);
    scene->render_request |= vector_tiles_layers::work_gpu(
        scene->vector_tiles_layer_system, &scene->render, ba, scene->symbol_culling, views_info);
    scene->render_request |=
        three_d_tiles_layers::work_gpu(scene->three_d_tiles_layer_system, &scene->render, ba);
    gizmo_layers::work_gpu(scene->gizmo_layer_system, &scene->render);
    scene->render_request |= editor::work_gpu(scene->shape_editor, &scene->render);
    scene->render_request |= debug_draw::work_gpu(scene->debug_draw, &scene->render);

    for (auto& entry : scene->views)
    {
        scene->render_request |= work_gpu(entry.second.view, &scene->render);
    }

    symbol_culling::work_gpu(scene->symbol_culling, &scene->render);

    if (my->get_shaders_info().had_unlinked_shaders_last_frame)
    {
        scene->render_request.request_all();
    }

    if (scene->render_request.is_planet_feedback_scheduled())
    {
        for (auto& entry : scene->views)
        {
            planet::request_feedback_render(get_planet_geometry(entry.second.view));
        }
    }

    if (scene->render_request.is_flat_overlay_render_scheduled())
    {
        for (auto& entry : scene->views)
        {
            vector_flat_overlay::schedule_visual_render(
                get_vector_flat_overlay(entry.second.view),
                scene->render_request.is_visual_render_caused_by(
                    RenderRequest::VisualCause::Animation));
        }
    }

    scene->render.rd->reset();
}

void draw(Scene* scene, my::Instance* my, GpuResourceContext* gpu_rc)
{
    assert(scene && my && gpu_rc);

    HRZ_SCOPED_SAMPLE("scene draw");

    _assign_my_instance(scene, my, gpu_rc);

    register_views(scene);

    planet::GeometryResources planet_geometry_resources;
    planet::fill_in_geometry_resources(scene->planet, &planet_geometry_resources, my->get_info());

    StaticVector<RenderViewInfo, SCENE_VIEW_COUNT> views_info;
    for (const auto& entry : scene->views)
    {
        views_info.push_back(entry.second.render_info);
    }

    if (scene->render_request.is_any_render_requested())
    {
        attribution::reset_used_attributions(scene->attributions);

        single_model_layers::draw(
            scene->single_model_layer_system, &scene->render, scene->attributions);
        vector_tiles_layers::draw(
            scene->vector_tiles_layer_system, &scene->render, scene->render_request, views_info,
            scene->symbol_culling, scene->attributions);
        three_d_tiles_layers::draw(
            scene->three_d_tiles_layer_system, &scene->render, scene->attributions);
        gizmo_layers::draw(scene->gizmo_layer_system, &scene->render, views_info);
        clipping_plane_layers::draw(scene->clipping_plane_layer_system, &scene->render, views_info);
        editor::draw(scene->shape_editor, &scene->render, planet_geometry_resources);
        symbol_culling::draw(scene->symbol_culling, scene->render_request);
        debug_draw::draw(scene->debug_draw, &scene->render);

        planet::use_attributions(scene->planet, scene->attributions);
    }

    for (auto& entry : scene->views)
    {
        if (hrz::render::profiling::is_enabled() && my->get_info().has_disjoint_time_query)
        {
            hrz::render::profiling::new_view_start_point();
        }

        draw(entry.second.view, scene->render_request, &scene->render, planet_geometry_resources);
    }

    scene->render.rd->reset();
}

void enqueue_attribution_message(
    Scene* scene,
    ClientMessageQueue* mq,
    std::optional<hrz::uint128>* last_hash)
{
    auto attributions = attribution::get_frame_attributions(scene->attributions);
    hrz::uint128 current_hash = hrz::murmur3_x64_128(hrz::as_bytes(attributions));

    if (last_hash->has_value() && last_hash->value() == current_hash) return;

    *last_hash = current_hash;

    hrz_proto::AttributionsMessage message;
    message.mutable_attributions()->Reserve(attributions.size());
    for (const auto& src : attributions)
    {
        auto* dst = message.add_attributions();
        dst->set_text(std::string(src.title));
        dst->set_logo_url(std::string(src.logo));
    }
    client_message_queue::enqueue_attributions_message(mq, std::move(message));
}

void provide_client_vector_data(Scene* scene, const hrz_proto::VectorDataRequestResponse& response)
{
    assert(scene);
    scene->vector_data_channel.send(response);
}

void invalidate_client_vector_data(
    Scene* scene,
    const hrz_proto::VectorDataInvalidation& invalidation)
{
    assert(scene);
    scene->vector_data_channel.send(invalidation);
}

uint64_t create_layer(
    Scene* scene,
    hrz_proto::LayerType layer_type,
    std::string_view name,
    ActorRunner* ar)
{
    assert(scene && ar);

    auto handle = scene->layer_id_pool.alloc();

    if (name.empty())
    {
        name = get_default_layer_name(scene, layer_type);
    }

    auto layer = LayersInfo::Layer();
    layer.type = layer_type;
    layer.name = std::string(name.data(), name.length());

    scene->layers_info.layers.insert({handle, layer});
    scene->layers_info.layer_type_counters[layer_type]++;

    switch (layer_type)
    {
        case hrz_proto::LayerType::SINGLE_MODEL:
            single_model_layers::register_layer(
                scene->single_model_layer_system, scene->model, handle);
            break;
        case hrz_proto::LayerType::DTM_RASTER:
        case hrz_proto::LayerType::IMAGERY_RASTER:
            planet::register_layer(scene->planet, scene->model, handle, layer_type);
            break;
        case hrz_proto::LayerType::VECTOR_DATA:
            vector_data::register_layer(scene->vector_data_loader, scene->model, handle);
            break;
        case hrz_proto::LayerType::VECTOR_TILES:
            vector_tiles_layers::register_layer(
                scene->vector_tiles_layer_system, scene->model, scene->planet,
                scene->vector_data_loader, ar, handle);
            break;
        case hrz_proto::LayerType::THREE_D_TILES:
            three_d_tiles_layers::register_layer(
                scene->three_d_tiles_layer_system, scene->model, handle);
            break;
        case hrz_proto::LayerType::CLIPPING_PLANE:
            clipping_plane_layers::register_layer(
                scene->clipping_plane_layer_system, scene->model, handle);
            break;
        case hrz_proto::LayerType::IN_MEMORY_VECTOR_SOURCE:
            vector_data::in_memory::register_layer(
                scene->in_memory_vector_database, scene->model, handle);
            break;
        case hrz_proto::LayerType::GIZMO:
            gizmo_layers::register_layer(scene->gizmo_layer_system, scene->model, handle);
            break;
        case hrz_proto::LayerType::EDITABLE_SHAPE:
            editor::register_layer(scene->shape_editor, scene->model, handle);
            break;
        default: break;
    }

    return handle;
}

void destroy_layer(Scene* scene, uint64_t layer_id)
{
    assert(scene);

    auto it = scene->layers_info.layers.find(layer_id);
    if (it == scene->layers_info.layers.end())
    {
        HRZ_LOG_ERROR("Cannot destroy layer with id {}: not found", layer_id);
        return;
    }

    auto layer_type = it->second.type;

    scene->layer_id_pool.release(layer_id);
    scene->layers_info.layers.erase(layer_id);

    switch (layer_type)
    {
        case hrz_proto::LayerType::SINGLE_MODEL:
            single_model_layers::unregister_layer(scene->single_model_layer_system, layer_id);
            break;
        case hrz_proto::LayerType::DTM_RASTER:
        case hrz_proto::LayerType::IMAGERY_RASTER:
            planet::unregister_layer(scene->planet, layer_id, layer_type);
            break;
        case hrz_proto::LayerType::VECTOR_DATA:
            vector_data::unregister_layer(scene->vector_data_loader, layer_id);
            break;
        case hrz_proto::LayerType::VECTOR_TILES:
            vector_tiles_layers::unregister_layer(scene->vector_tiles_layer_system, layer_id);
            break;
        case hrz_proto::LayerType::THREE_D_TILES:
            three_d_tiles_layers::unregister_layer(scene->three_d_tiles_layer_system, layer_id);
            break;
        case hrz_proto::LayerType::CLIPPING_PLANE:
            clipping_plane_layers::unregister_layer(scene->clipping_plane_layer_system, layer_id);
            break;
        case hrz_proto::LayerType::IN_MEMORY_VECTOR_SOURCE:
            vector_data::in_memory::unregister_layer(scene->in_memory_vector_database, layer_id);
            break;
        case hrz_proto::LayerType::GIZMO:
            gizmo_layers::unregister_layer(scene->gizmo_layer_system, layer_id);
            break;
        case hrz_proto::LayerType::EDITABLE_SHAPE:
            editor::unregister_layer(scene->shape_editor, layer_id);
            break;
        default: break;
    }
}

void rename_layer(Scene* scene, uint64_t layer_id, std::string_view name)
{
    assert(scene);
    scene->layers_info.layers[layer_id].name = std::string(name.data(), name.size());
}

bool is_layer_valid(const Scene* scene, uint64_t layer_id)
{
    assert(scene);
    return scene->layer_id_pool.is_valid(layer_id);
}

hrz_proto::LayerType get_layer_type(const Scene* scene, uint64_t layer_id)
{
    assert(scene);

    if (!is_layer_valid(scene, layer_id))
    {
        return hrz_proto::LayerType::INVALID;
    }

    return scene->layers_info.layers.at(layer_id).type;
}

void retrieve_layer_info(const Scene* scene, uint64_t layer_id, ::hrz_proto::Layer& output)
{
    assert(scene);

    if (!is_layer_valid(scene, layer_id)) return;

    const auto& layer = scene->layers_info.layers.at(layer_id);
    output.set_type(layer.type);
    output.set_name(layer.name);
    output.mutable_handle()->set_opaque(layer_id);
}

void retrieve_all_layers(const Scene* scene, ::hrz_proto::LayerArray& output)
{
    assert(scene);

    for (auto it : scene->layers_info.layers)
    {
        uint64_t layer_handle = it.first;
        hrz_proto::LayerType type = get_layer_type(scene, layer_handle);

        ::hrz_proto::Layer* layer = output.add_layers();
        layer->set_type(type);
        layer->set_name(it.second.name);
        layer->mutable_handle()->set_opaque(layer_handle);
    }
}

void notify_model_update(
    Scene* scene,
    scene_model::UpdateType update_type,
    const hrz_proto::Path& path)
{
    switch (path.root().kind_case())
    {
        case hrz_proto::PathRoot::kSingleModelLayer:
        {
            scene_model::SingleModelLayerPath layer_path(path);
            if (layer_path.valid())
            {
                single_model_layers::notify_model_update(
                    scene->single_model_layer_system, path.root().single_model_layer().opaque(),
                    update_type, layer_path);
            }
            else
            {
                HRZ_LOG_ERROR("Invalid single model layer path in model operation");
            }
            break;
        }
        case hrz_proto::PathRoot::kDtmRasterLayer:
        {
            scene_model::DtmRasterLayerPath layer_path(path);

            if (layer_path.valid())
            {
                planet::notify_model_update(
                    scene->planet, path.root().dtm_raster_layer().opaque(), update_type,
                    layer_path);
            }
            else
            {
                HRZ_LOG_ERROR("Invalid DTM raster layer path in model operation");
            }
            break;
        }
        case hrz_proto::PathRoot::kImageryRasterLayer:
        {
            scene_model::ImageryRasterLayerPath layer_path(path);

            if (layer_path.valid())
            {
                planet::notify_model_update(
                    scene->planet, path.root().imagery_raster_layer().opaque(), update_type,
                    layer_path);
            }
            else
            {
                HRZ_LOG_ERROR("Invalid imagery raster layer path in model operation");
            }
            break;
        }
        case hrz_proto::PathRoot::kVectorDataLayer:
        {
            scene_model::VectorDataLayerPath layer_path(path);

            if (layer_path.valid())
            {
                vector_data::notify_update(
                    scene->vector_data_loader, path.root().vector_data_layer().opaque(),
                    update_type, layer_path);
            }
            else
            {
                HRZ_LOG_ERROR("Invalid vector data layer path in model operation");
            }
            break;
        }
        case hrz_proto::PathRoot::kVectorTilesLayer:
        {
            scene_model::VectorTilesLayerPath layer_path(path);

            if (layer_path.valid())
            {
                vector_tiles_layers::notify_update(
                    scene->vector_tiles_layer_system, path.root().vector_tiles_layer().opaque(),
                    update_type, layer_path);
            }
            else
            {
                HRZ_LOG_ERROR("Invalid vector tiles layer path in model operation");
            }
            break;
        }
        case hrz_proto::PathRoot::kThreeDTilesLayer:
        {
            scene_model::ThreeDTilesLayerPath layer_path(path);

            if (layer_path.valid())
            {
                three_d_tiles_layers::notify_update(
                    scene->three_d_tiles_layer_system, path.root().three_d_tiles_layer().opaque(),
                    update_type, layer_path);
            }
            else
            {
                HRZ_LOG_ERROR("Invalid 3D Tiles layer path in model operation");
            }
            break;
        }
        case hrz_proto::PathRoot::kGizmoLayer:
        {
            scene_model::GizmoLayerPath layer_path(path);

            if (layer_path.valid())
            {
                gizmo_layers::notify_model_update(
                    scene->gizmo_layer_system, path.root().gizmo_layer().opaque(), update_type,
                    layer_path);
            }
            else
            {
                HRZ_LOG_ERROR("Invalid gizmo layer path in model operation");
            }
            break;
        }
        case hrz_proto::PathRoot::kEditableShapeLayer:
        {
            scene_model::EditableShapeLayerPath layer_path(path);

            if (layer_path.valid())
            {
                editor::notify_model_update(
                    scene->shape_editor, path.root().editable_shape_layer().opaque(), update_type,
                    layer_path);
            }
            else
            {
                HRZ_LOG_ERROR("Invalid editable shape layer path in model operation");
            }
            break;
        }
        case hrz_proto::PathRoot::kClippingPlaneLayer:
        {
            scene_model::ClippingPlaneLayerPath layer_path(path);

            if (layer_path.valid())
            {
                clipping_plane_layers::notify_update(
                    scene->clipping_plane_layer_system, path.root().clipping_plane_layer().opaque(),
                    update_type, layer_path);
            }
            else
            {
                HRZ_LOG_ERROR("Invalid clipping plane layer path in model operation");
            }
            break;
        }
        case hrz_proto::PathRoot::kInMemoryVectorSourceLayer:
        {
            scene_model::InMemoryVectorSourceLayerPath layer_path(path);

            if (layer_path.valid())
            {
                vector_data::in_memory::notify_update(
                    scene->in_memory_vector_database,
                    path.root().in_memory_vector_source_layer().opaque(), update_type, layer_path);
            }
            else
            {
                HRZ_LOG_ERROR("Invalid in-memory vector source layer path in model operation");
            }
            break;
        }
        case hrz_proto::PathRoot::kSceneViewSettings:
        {
            scene_model::SceneViewSettingsPath settings_path(path);
            notify_model_update(scene, update_type, settings_path);
            break;
        }
        case hrz_proto::PathRoot::kSceneSettings:
        {
            scene_model::SceneSettingsPath settings_path(path);
            notify_model_update(scene, update_type, settings_path);
            break;
        }
        case hrz_proto::PathRoot::kCameraSettings:
        {
            scene_model::CameraSettingsPath settings_path(path);
            notify_model_update(scene, update_type, settings_path);
            break;
        }
        default: assert(0 && "Not implemented"); break;
    }
}

void set_model(Scene* scene, const hrz_proto::Path& path, std::string_view payload)
{
    add_scene_model_log_line(scene, path, "SET");
    scene_model::set_raw(scene->model, path, payload);
    notify_model_update(scene, scene_model::UpdateType::Set, path);
}

uint32_t add_model(Scene* scene, const hrz_proto::Path& path, std::string_view payload)
{
    add_scene_model_log_line(scene, path, "ADD");
    uint32_t count = scene_model::add_raw(scene->model, path, payload);
    notify_model_update(scene, scene_model::UpdateType::Add, path);
    return count;
}

uint32_t remove_model(Scene* scene, const hrz_proto::Path& path)
{
    add_scene_model_log_line(scene, path, "REMOVE");
    uint32_t count = scene_model::remove(scene->model, path);
    notify_model_update(scene, scene_model::UpdateType::Remove, path);
    return count;
}

std::string get_model(const Scene* scene, const hrz_proto::Path& path)
{
    return scene_model::get_raw(scene->model, path);
}

uint32_t count_model(const Scene* scene, const hrz_proto::Path& path)
{
    return scene_model::count(scene->model, path);
}

std::optional<PositionPickingTicket> schedule_pick(
    Scene* scene,
    lm::ivec2 mouse_position,
    gsl::span<const hrz_proto::LayerHandle> included_rasters)
{
    for (auto& entry : scene->views)
    {
        auto ticket = schedule_pick(entry.second.view, mouse_position, included_rasters);
        if (ticket.has_value())
        {
            return PositionPickingTicket{
                scene->position_picking_pool.alloc(PositionPicking{entry.first, ticket.value()}),
                entry.first};
        }
    }
    return std::nullopt;
}

std::optional<AreaPickingTicket> schedule_pick(Scene* scene, lm::ibbox2 rect)
{
    for (auto& entry : scene->views)
    {
        auto ticket = schedule_pick(entry.second.view, rect);
        if (ticket.has_value())
        {
            return AreaPickingTicket{
                scene->area_picking_pool.alloc(AreaPicking{entry.first, ticket.value()}),
                entry.first};
        }
    }
    return std::nullopt;
}

RasterDataFetchTicket schedule_raster_data_fetch(
    Scene* scene,
    const hrz::GeoPosition2& position,
    gsl::span<const hrz_proto::LayerHandle> raster_layers)
{
    auto ticket = hrz::planet::schedule_raster_data_fetch(scene->planet, position, raster_layers);
    return {ticket};
}

/**
 * Indicates that a picking event has occured.
 * The layer subsystems are responsible for checking whether
 * or not the picked object is one of their own, via the `system_id`
 * parameter and then setting the fields in the pick result message.
 */
static void pick(
    Scene* scene,
    const picking::PositionResult& result,
    hrz_proto::SceneViewIndex scene_view,
    hrz_proto::PickResults& pick_results)
{
    assert(scene);
    assert(result.position.has_value());
    HRZ_SCOPED_SAMPLE("scene notify picking");

    single_model_layers::pick(scene->single_model_layer_system, result, pick_results);

    vector_tiles_layers::pick(
        scene->vector_tiles_layer_system, result.ref, result.heatmap_values, pick_results);

    three_d_tiles_layers::pick(scene->three_d_tiles_layer_system, result, pick_results);

    editor::pick(scene->shape_editor, result.ref, result.position.value(), pick_results);

    planet::pick(
        scene->planet, result.ref, result.position.value(), result.included_rasters, scene_view,
        pick_results);
}

/**
 * Turns a list of object references from picking into a list of typed object references (that
 * depend on the layer type). Returns the number of picking::ObjectReference processed, and the
 * number of typed object references written. The output span must be pre-allocated so that it may
 * contain all input references.
 */
static std::pair<size_t, size_t> make_typed_object_references(
    Scene* scene,
    gsl::span<const picking::ObjectReference> objs,
    gsl::span<hrz_proto::TypedObjectReference> output)
{
    assert(objs.size() <= output.size());

    size_t in_cursor = 0;
    size_t out_cursor = 0;
    bool has_advanced_this_iteration = false;

    auto advance_fn = [&](const std::pair<size_t, size_t>& p)
    {
        in_cursor += p.first;
        out_cursor += p.second;

        if (p.first > 0)
        {
            has_advanced_this_iteration = true;
        }
    };

    while (in_cursor < objs.size())
    {
        advance_fn(single_model_layers::make_typed_object_references(
            scene->single_model_layer_system, objs.subspan(in_cursor), output.subspan(out_cursor)));

        advance_fn(vector_tiles_layers::make_typed_object_references(
            scene->vector_tiles_layer_system, objs.subspan(in_cursor), output.subspan(out_cursor)));

        advance_fn(three_d_tiles_layers::make_typed_object_references(
            scene->three_d_tiles_layer_system, objs.subspan(in_cursor),
            output.subspan(out_cursor)));

        advance_fn(editor::make_typed_object_references(
            scene->shape_editor, objs.subspan(in_cursor), output.subspan(out_cursor)));

        advance_fn(planet::make_typed_object_references(
            scene->planet, objs.subspan(in_cursor), output.subspan(out_cursor)));

        if (!std::exchange(has_advanced_this_iteration, false))
        {
            break;
        }
    }

    return std::make_pair(in_cursor, out_cursor);
}

void fill_picking_result(
    Scene* scene,
    const picking::PositionResult& raw_result,
    hrz_proto::SceneViewIndex scene_view,
    hrz_proto::PickResults& pick_results)
{
    auto picked_position = raw_result.position;

    if (picked_position.has_value())
    {
        auto geo = ecef_to_geo3(picked_position.value());

        pick_results.mutable_position()->set_latitude(lm::degrees(geo.lat));
        pick_results.mutable_position()->set_longitude(lm::degrees(geo.lon));
        pick_results.mutable_position()->set_altitude(geo.alt);

        scene::pick(scene, raw_result, scene_view, pick_results);
    }
    else
    {
        pick_results.clear_position();
    }
}

bool retrieve_mouse_hover_info(
    Scene* scene,
    PositionPickingTicket ticket,
    hrz_proto::MouseHoverInfoMessage& msg,
    picking::ObjectReference* obj_ref)
{
    if (!scene->position_picking_pool.is_valid(ticket.ticket)) return false;

    PositionPicking picking_ticket = *scene->position_picking_pool.get_object(ticket.ticket);

    auto it = scene->views.find(picking_ticket.view);
    if (it == scene->views.end())
    {
        scene->position_picking_pool.release(ticket.ticket);
        return false;
    }

    picking::PositionResult pick_result;
    if (!retrieve_picking_result(it->second.view, picking_ticket.ticket, &pick_result))
    {
        return false;
    }
    scene->position_picking_pool.release(ticket.ticket);

    if (pick_result.position.has_value())
    {
        auto geo = ecef_to_geo3(pick_result.position.value());
        msg.mutable_geo_pos()->set_latitude(lm::degrees(geo.lat));
        msg.mutable_geo_pos()->set_longitude(lm::degrees(geo.lon));
        msg.mutable_geo_pos()->set_altitude(geo.alt);
    }
    else
    {
        msg.clear_geo_pos();
    }

    *obj_ref = pick_result.ref;
    make_typed_object_references(scene, {&pick_result.ref, 1}, {msg.mutable_picked(), 1});

    return true;
}

bool retrieve_pick_results(Scene* scene, PositionPickingTicket ticket, hrz_proto::PickResults& msg)
{
    if (!scene->position_picking_pool.is_valid(ticket.ticket)) return false;

    PositionPicking picking_ticket = *scene->position_picking_pool.get_object(ticket.ticket);

    auto it = scene->views.find(picking_ticket.view);
    if (it == scene->views.end())
    {
        scene->position_picking_pool.release(ticket.ticket);
        return false;
    }

    picking::PositionResult pick_result;
    if (!retrieve_picking_result(it->second.view, picking_ticket.ticket, &pick_result))
    {
        return false;
    }
    scene->position_picking_pool.release(ticket.ticket);

    fill_picking_result(scene, pick_result, ticket.scene_view, msg);
    return true;
}

bool retrieve_pick_area_result(
    Scene* scene,
    AreaPickingTicket ticket,
    std::vector<hrz_proto::TypedObjectReference>& typed_refs)
{
    if (!scene->area_picking_pool.is_valid(ticket.ticket)) return false;

    AreaPicking picking_ticket = *scene->area_picking_pool.get_object(ticket.ticket);

    auto it = scene->views.find(picking_ticket.view);
    if (it == scene->views.end())
    {
        scene->area_picking_pool.release(ticket.ticket);
        return false;
    }

    std::vector<picking::AreaResult> result;
    if (!retrieve_picking_result(it->second.view, picking_ticket.ticket, result))
    {
        return false;
    }
    scene->area_picking_pool.release(ticket.ticket);

    static_assert(sizeof(picking::ObjectReference) == sizeof(picking::AreaResult), "Size mismatch");

    // AreaResult only contains ObjectReference, so we can safely cast its
    // pointer directly. The static asserts above check this.
    gsl::span<const picking::ObjectReference> refs_span(
        (const picking::ObjectReference*)result.data(), result.size());

    typed_refs.resize(refs_span.size());

    std::pair<size_t, size_t> ref_count =
        make_typed_object_references(scene, refs_span, typed_refs);
    typed_refs.resize(ref_count.second);

    return true;
}

bool retrieve_raster_data_fetch_results(
    Scene* scene,
    RasterDataFetchTicket ticket,
    std::vector<hrz_proto::PickLayerResult>& results)
{
    auto fetch_result =
        hrz::planet::retrieve_raster_data_fetch_results(scene->planet, ticket.ticket);
    if (fetch_result.has_value())
    {
        results = fetch_result->results;
        return true;
    }
    return false;
}

size_t select(
    Scene* scene,
    uint64_t layer_id,
    gsl::span<const vector_data::FeatureIdHash> feature_id_hashes)
{
    if (!feature_id_hashes.empty())
    {
        for (auto feature_id_hash : feature_id_hashes)
        {
            selection::select(scene->selection, layer_id, feature_id_hash);
        }
    }

    return selection::selected_objects_count(scene->selection);
}

size_t deselect(
    Scene* scene,
    uint64_t layer_id,
    gsl::span<const vector_data::FeatureIdHash> feature_id_hashes)
{
    if (!feature_id_hashes.empty())
    {
        for (auto feature_id_hash : feature_id_hashes)
        {
            selection::deselect(scene->selection, layer_id, feature_id_hash);
        }
    }

    return selection::selected_objects_count(scene->selection);
}

void deselect_all(Scene* scene)
{
    assert(scene);
    selection::deselect_all(scene->selection);
}

std::optional<picking::FeatureReference> make_feature_reference(
    Scene* scene,
    const picking::ObjectReference& obj)
{
    {
        auto id =
            single_model_layers::make_feature_reference(scene->single_model_layer_system, obj);
        if (id.has_value()) return id;
    }
    {
        auto id =
            vector_tiles_layers::make_feature_reference(scene->vector_tiles_layer_system, obj);
        if (id.has_value()) return id;
    }
    return three_d_tiles_layers::make_feature_reference(scene->three_d_tiles_layer_system, obj);
}

void quick_highlight(Scene* scene, const picking::FeatureReference& feature)
{
    scene->last_quick_highlight_feature_reference = feature;
}

RenderRequest get_render_request(Scene* scene)
{
    assert(scene);
    return scene->render_request;
}

bool is_working(Scene* scene)
{
    assert(scene);

    if (scene->render_request.is_visual_render_caused_by(RenderRequest::VisualCause::Scene))
    {
        return true;
    }

    StaticVector<const PlanetGeometry*, SCENE_VIEW_COUNT> planet_geometries;
    for (auto& entry : scene->views)
    {
        auto* scene_view = entry.second.view;
        planet_geometries.push_back(get_planet_geometry(scene_view));

        if (vector_flat_overlay::is_working(get_vector_flat_overlay(scene_view))) return true;
    }

    if (planet::is_working(scene->planet, planet_geometries)) return true;
    if (vector_tiles_layers::is_working(scene->vector_tiles_layer_system)) return true;
    if (single_model_layers::is_working(scene->single_model_layer_system)) return true;
    if (three_d_tiles_layers::is_working(scene->three_d_tiles_layer_system)) return true;
    if (symbol_culling::is_working(scene->symbol_culling)) return true;

    return false;
}

void scene_model_dev_ui(
    Scene* scene,
    PlatformContext* platform,
    mu_Context* ctx,
    const char* window_name)
{
    if (mu_begin_window_ex(ctx, window_name, mu_rect(300, 100, 580, 400), MU_OPT_CLOSED))
    {
        int window_width = mu_get_current_container(ctx)->rect.w - 16;
        mu_layout_row(ctx, 1, &window_width, 0);

        if (mu_button(ctx, "Copy base64 dump to clipboard"))
        {
            hrz_proto::SceneDumpRequest dump_request = {};
            dump_request.set_scene_name("Scene"); // Unrivaled creativity

            hrz_proto::SceneDump dump = {};
            dump_scene(scene, dump_request, dump);

            std::string bytes_dump = dump.SerializeAsString();

            std::string base64_dump;
            hrz::str::encode_base64(
                {(const std::byte*)bytes_dump.data(), bytes_dump.size()}, &base64_dump);

            hrz::platform::copy_to_clipboard(platform, base64_dump);
        }

        auto layer_count = scene->layers_info.layers.size();

        static fmt::memory_buffer buffer;
        if (mu_begin_treenode_ex(
                ctx, "Layers",
                hrz::format_to_buffer(
                    buffer, "{} layer{}", layer_count, layer_count == 1 ? "" : "s"),
                0))
        {
            static const int layout[] = {80, 165, -1};
            mu_layout_row(ctx, 3, layout, 0);

            for (auto pair : scene->layers_info.layers)
            {
                mu_text(ctx, hrz::format_to_buffer(buffer, "{}", pair.first));
                mu_text(ctx, hrz_proto::LayerType_Name(pair.second.type).c_str());
                mu_text(ctx, pair.second.name.c_str());
            }

            mu_end_treenode(ctx);
        }

        mu_layout_row(ctx, 1, &window_width, 0);
        mu_text(ctx, "Logs");

        static const int layout[] = {50, 50, -1};
        mu_layout_row(ctx, 2, layout, 0);

        if (mu_button(ctx, "Copy"))
        {
            std::string clip_content;
            uint32_t index = scene->scene_model_logs.head;
            for (uint32_t i = 0; i < scene->scene_model_logs.count; ++i)
            {
                if (index == DEV_UI_LOG_SIZE) index = 0;
                clip_content += scene->scene_model_logs.lines[index++];
                clip_content += "\n";
            }
            hrz::platform::copy_to_clipboard(platform, clip_content);
        }
        if (mu_button(ctx, "Clear"))
        {
            clear_scene_model_logs(scene);
        }

        static const int layout3 = -1;
        mu_layout_row(ctx, 1, &layout3, -1);

        static ui::StickyPanelState sticky_panel_state;
        ui::begin_sticky_panel(ctx, &sticky_panel_state, "Scene model logs");

        mu_layout_row(ctx, 1, &layout3, 0);

        uint32_t index = scene->scene_model_logs.head;
        for (uint32_t i = 0; i < scene->scene_model_logs.count; ++i)
        {
            if (index == DEV_UI_LOG_SIZE)
            {
                index = 0;
            }

            mu_text(ctx, scene->scene_model_logs.lines[index++].c_str());
        }

        ui::end_sticky_panel(ctx, &sticky_panel_state);

        mu_end_window(ctx);
    }
}

void viewport_dev_ui(Scene* scene, mu_Context* ctx)
{
    static const int layout[] = {150, -1};
    mu_layout_row(ctx, 2, layout, 0);

    fmt::memory_buffer buffer;

    for (const auto& it : scene->views)
    {
        mu_text(
            ctx,
            format_to_buffer(
                buffer, "Scene view {}{}", (int)it.first,
                it.first == scene->main_view ? " (main)" : ""));

        lm::vec2 size = it.second.render_info.cam_view_info.viewport.subview_size();
        mu_text(ctx, format_to_buffer(buffer, "{:.2f} x {:.2f}", size.x, size.y));
    }
}

void camera_dev_ui(Scene* scene, mu_Context* ctx, const char* window_name)
{
    if (!mu_begin_window_ex(ctx, window_name, mu_rect(300, 100, 430, 400), MU_OPT_CLOSED))
    {
        return;
    }

    fmt::memory_buffer unique_label_buffer;
    fmt::memory_buffer label_buffer;

    static const int layout = -1;
    mu_layout_row(ctx, 1, &layout, -1);

    if (mu_header(ctx, "Notifications"))
    {
        auto& logs = scene->camera_notification_logs;

        if (mu_button(ctx, "Clear"))
        {
            logs.head = 0;
            logs.count = 0;
        }

        mu_layout_row(ctx, 1, &layout, 150);

        static ui::StickyPanelState sticky_panel_state;
        ui::begin_sticky_panel(ctx, &sticky_panel_state, "Camera notifications logs");

        mu_layout_row(ctx, 1, &layout, 0);

        uint32_t index = logs.head;
        for (uint32_t i = 0; i < logs.count; ++i)
        {
            if (index == DEV_UI_LOG_SIZE)
            {
                index = 0;
            }
            mu_text(ctx, logs.lines[index++].c_str());
        }

        ui::end_sticky_panel(ctx, &sticky_panel_state);
    }

    mu_layout_row(ctx, 1, &layout, -1);

    for (size_t i = 0; i < CAMERA_COUNT; ++i)
    {
        const auto* camera = scene->cameras[i];

        bool scene_views[SCENE_VIEW_COUNT]{};
        size_t view_count = 0;
        for (size_t j = 0; j < SCENE_VIEW_COUNT; ++j)
        {
            const auto& it = scene->views.find((hrz_proto::SceneViewIndex)j);
            if (it != scene->views.end())
            {
                scene_views[j] =
                    scene->get_current_camera_index(it->first) == (hrz_proto::CameraIndex)i;
                if (scene_views[j]) view_count += 1;
            }
        }

        label_buffer.clear();
        fmt::format_to(std::back_inserter(label_buffer), "Camera {} (", i);
        if (view_count > 0)
        {
            size_t v = 0;
            for (size_t j = 0; j < SCENE_VIEW_COUNT; ++j)
            {
                if (scene_views[j])
                {
                    fmt::format_to(std::back_inserter(label_buffer), "scene view {}", j);
                    v += 1;
                    if (v < view_count)
                    {
                        fmt::format_to(std::back_inserter(label_buffer), ", ");
                    }
                    else
                    {
                        fmt::format_to(std::back_inserter(label_buffer), ")");
                        break;
                    }
                }
            }
        }
        else
        {
            fmt::format_to(std::back_inserter(label_buffer), "unused)");
        }
        label_buffer.push_back(0);

        if (mu_header_ex(
                ctx, format_to_buffer(unique_label_buffer, "Camera {}", i), label_buffer.data(), 0))
        {
            camera->dev_ui(ctx);
        }
    }

    mu_end_window(ctx);
}

void flat_overlay_dev_ui(Scene* scene, mu_Context* ctx, const char* window_name)
{
    if (!mu_begin_window_ex(ctx, window_name, mu_rect(300, 100, 420, 400), MU_OPT_CLOSED))
    {
        return;
    }

    fmt::memory_buffer unique_label_buffer;
    fmt::memory_buffer label_buffer;

    for (const auto& it : scene->views)
    {
        auto scene_view_index = (int)it.first;
        auto scene_view = it.second;

        label_buffer.clear();
        fmt::format_to(std::back_inserter(label_buffer), "Scene view {}", scene_view_index);
        if (scene_view_index == scene->main_view)
        {
            fmt::format_to(std::back_inserter(label_buffer), " (main)");
        }
        label_buffer.push_back(0);

        if (mu_header_ex(
                ctx, format_to_buffer(unique_label_buffer, "Scene view {}", scene_view_index),
                label_buffer.data(), 0))
        {
            vector_flat_overlay::dev_ui(get_vector_flat_overlay(scene_view.view), ctx);
        }
    }

    mu_end_window(ctx);
}

VectorDataLoader* get_vector_data_loader(Scene* scene)
{
    assert(scene);
    return scene->vector_data_loader;
}

VectorTilesLayerSystem* get_vector_tiles_layers(Scene* scene)
{
    assert(scene);
    return scene->vector_tiles_layer_system;
}

hrz_proto::ICameraService* get_camera_service(Scene* scene)
{
    assert(scene);
    return scene;
}

ShapeEditor* get_shape_editor(Scene* scene)
{
    assert(scene);
    return scene->shape_editor;
}

PlanetSurface* get_planet(Scene* scene)
{
    assert(scene);
    return scene->planet;
}

DebugDrawSystem* get_debug_draw(Scene* scene)
{
    assert(scene);
    return scene->debug_draw;
}

LayersInfo* get_layers_info(Scene* scene)
{
    assert(scene);
    return &scene->layers_info;
}

CameraViewInfo get_main_camera_view_info(const Scene* scene)
{
    assert(scene);
    return CameraViewInfo::make_from_camera_and_viewport(
        scene->cameras[scene->get_current_camera_index(scene->main_view)]->get_info(),
        scene::compute_viewport_info(
            scene->main_view, scene->model, scene->device_pixel_ratio, scene->canvas_size));
}

SceneModel* get_model(Scene* scene)
{
    return scene->model;
}

AttributionRegistry* get_attribution_registry(Scene* scene)
{
    assert(scene);
    return scene->attributions;
}

void dump_scene(
    const Scene* scene,
    const hrz_proto::SceneDumpRequest& input,
    hrz_proto::SceneDump& output)
{
    output.set_name(input.scene_name());
    output.set_version(hrz::scene_dump::SceneModelVersion);

    ConstSceneModelAccessor accessor(scene->model);

    {
        hrz_proto::SceneSettingsPathBuilder<ConstSceneModelAccessor> builder(accessor);
        output.mutable_scene_settings()->MergeFrom(builder.get());
    }

    for (const auto& view : scene->views)
    {
        auto* output_view = output.add_scene_view_settings();
        output_view->set_index(view.first);

        hrz_proto::SceneViewSettingsPathBuilder<ConstSceneModelAccessor> builder(
            accessor, view.first);
        output_view->mutable_settings()->MergeFrom(builder.get());
    }

    for (unsigned int i = 0; i < CAMERA_COUNT; ++i)
    {
        const auto* cam = scene->cameras[i];
        if (cam)
        {
            auto* output_cam = output.add_camera_settings();
            output_cam->set_index((hrz_proto::CameraIndex)i);

            hrz_proto::CameraSettingsPathBuilder<ConstSceneModelAccessor> builder(
                accessor, (hrz_proto::CameraIndex)i);
            output_cam->mutable_settings()->MergeFrom(builder.get());
        }
    }

    for (unsigned int i = 0; i < CAMERA_COUNT; ++i)
    {
        const auto* cam = scene->cameras[i];
        if (cam)
        {
            auto* output_cam = output.add_cameras();
            output_cam->set_index((hrz_proto::CameraIndex)i);

            auto viewpoint = cam->get_angular_viewpoint();
            output_cam->mutable_viewpoint()->MergeFrom(viewpoint);
        }
    }

    for (auto it : scene->layers_info.layers)
    {
        uint64_t layer_handle = it.first;

        hrz_proto::LayerHandle handle;
        handle.set_opaque(layer_handle);

        auto* output_layer = output.add_layers();

        auto name_it = scene->layers_info.layers.find(layer_handle);
        if (name_it != scene->layers_info.layers.end())
        {
            output_layer->set_name(it.second.name);
        }

        switch (get_layer_type(scene, layer_handle))
        {
            case hrz_proto::LayerType::SINGLE_MODEL:
            {
                hrz_proto::SingleModelLayerPathBuilder<ConstSceneModelAccessor> builder(
                    accessor, handle);
                output_layer->mutable_single_model()->MergeFrom(builder.get());
                break;
            }
            case hrz_proto::LayerType::DTM_RASTER:
            {
                hrz_proto::DtmRasterLayerPathBuilder<ConstSceneModelAccessor> builder(
                    accessor, handle);
                output_layer->mutable_dtm_raster()->MergeFrom(builder.get());
                break;
            }
            case hrz_proto::LayerType::IMAGERY_RASTER:
            {
                hrz_proto::ImageryRasterLayerPathBuilder<ConstSceneModelAccessor> builder(
                    accessor, handle);
                output_layer->mutable_imagery_raster()->MergeFrom(builder.get());
                break;
            }
            case hrz_proto::LayerType::VECTOR_DATA:
            {
                hrz_proto::VectorDataLayerPathBuilder<ConstSceneModelAccessor> builder(
                    accessor, handle);
                output_layer->mutable_vector_data()->MergeFrom(builder.get());
                break;
            }
            case hrz_proto::LayerType::VECTOR_TILES:
            {
                hrz_proto::VectorTilesLayerPathBuilder<ConstSceneModelAccessor> builder(
                    accessor, handle);
                output_layer->mutable_vector_tiles()->MergeFrom(builder.get());
                break;
            }
            case hrz_proto::LayerType::IN_MEMORY_VECTOR_SOURCE:
            {
                hrz_proto::InMemoryVectorSourceLayerPathBuilder<ConstSceneModelAccessor> builder(
                    accessor, handle);
                output_layer->mutable_in_memory_vector_source()->MergeFrom(builder.get());
                break;
            }
            case hrz_proto::LayerType::THREE_D_TILES:
            {
                hrz_proto::ThreeDTilesLayerPathBuilder<ConstSceneModelAccessor> builder(
                    accessor, handle);
                output_layer->mutable_three_d_tiles()->MergeFrom(builder.get());
                break;
            }
            case hrz_proto::LayerType::EDITABLE_SHAPE:
            {
                hrz_proto::EditableShapeLayerPathBuilder<ConstSceneModelAccessor> builder(
                    accessor, handle);
                output_layer->mutable_editable_shape()->MergeFrom(builder.get());
                break;
            }
            case hrz_proto::LayerType::GIZMO:
            {
                hrz_proto::GizmoLayerPathBuilder<ConstSceneModelAccessor> builder(accessor, handle);
                output_layer->mutable_gizmo()->MergeFrom(builder.get());
                break;
            }
            case hrz_proto::LayerType::CLIPPING_PLANE:
            {
                hrz_proto::ClippingPlaneLayerPathBuilder<ConstSceneModelAccessor> builder(
                    accessor, handle);
                output_layer->mutable_clipping_plane()->MergeFrom(builder.get());
                break;
            }
            default:
            {
                assert(!"Unhandled layer type");
                break;
            }
        }
    }
}

void load_scene_dump(
    Scene* scene,
    const hrz_proto::SceneLoadRequest& input,
    hrz_proto::LayerArray& output,
    ActorRunner* ar)
{
    hrz_proto::Void void_output;

    SceneModelAccessor accessor(scene->model);
    const auto& dump = input.dump();

    if (dump.version() != hrz::scene_dump::SceneModelVersion)
    {
        HRZ_LOG_WARNING(
            "Loading a scene dump with a possibly incompatible version (dump has version {:08x}, "
            "current version is {:08x})",
            dump.version(), hrz::scene_dump::SceneModelVersion);
    }

    if (input.clear_layers())
    {
        // We copy the handles first so that we don't get iterator invalidation
        // errors below.
        std::vector<uint64_t> layer_handles;
        for (auto it : scene->layers_info.layers)
        {
            layer_handles.push_back(it.first);
        }

        for (uint64_t layer_handle : layer_handles)
        {
            destroy_layer(scene, layer_handle);
        }

        scene->layers_info.layer_type_counters.clear();
    }

    std::string buffer;
    auto set_model_msg =
        [&buffer, scene](const hrz_proto::Path& path, const google::protobuf::MessageLite& msg)
    {
        buffer.clear();
        msg.SerializeToString(&buffer);
        set_model(scene, path, buffer);
    };

    if (dump.has_scene_settings())
    {
        hrz_proto::SceneSettingsPathBuilder<SceneModelAccessor> builder(accessor);
        set_model_msg(builder._path, dump.scene_settings());
    }

    for (const auto& view : dump.scene_view_settings())
    {
        hrz_proto::SceneViewSettingsPathBuilder<SceneModelAccessor> builder(accessor, view.index());
        set_model_msg(builder._path, view.settings());
    }

    for (const auto& cam : dump.camera_settings())
    {
        hrz_proto::CameraSettingsPathBuilder<SceneModelAccessor> builder(accessor, cam.index());
        set_model_msg(builder._path, cam.settings());
    }

    hrz_proto::OrbitCameraTransition camera_transition;
    camera_transition.mutable_limit_bounds()->set_east(180);
    camera_transition.mutable_limit_bounds()->set_west(-180);
    camera_transition.mutable_limit_bounds()->set_north(90);
    camera_transition.mutable_limit_bounds()->set_south(-90);
    camera_transition.set_max_altitude(100000000);
    camera_transition.set_min_tilt(0);
    camera_transition.set_max_tilt(3.14159265358);
    *camera_transition.mutable_go_to_animation() = input.camera_animation();
    camera_transition.set_is_interruptible(true);
    camera_transition.set_altitude_mode(hrz_proto::AltitudeMode::RELATIVE_TO_ELLIPSOID);
    camera_transition.mutable_correction_animation()->set_duration(1.0);
    camera_transition.mutable_correction_animation()->set_easing_exponent(2.0);
    camera_transition.mutable_correction_animation()->set_easing_function(
        hrz_proto::EasingFunctions::EASE_INOUT);
    camera_transition.mutable_correction_animation()->set_trajectory_type(
        hrz_proto::TrajectoryType::INTERPOLATED);

    for (const auto& cam : dump.cameras())
    {
        camera_transition.set_camera_index(cam.index());
        *camera_transition.mutable_angular_viewpoint() = cam.viewpoint();
        get_camera_service(scene)->set_orbit(camera_transition, void_output);
    }

    for (const auto& layer : dump.layers())
    {
        hrz_proto::LayerHandle handle;
        hrz_proto::LayerType layer_type;

        switch (layer.kind_case())
        {
            case hrz_proto::LayerDump::kSingleModel:
            {
                layer_type = hrz_proto::LayerType::SINGLE_MODEL;
                handle.set_opaque(create_layer(scene, layer_type, layer.name(), ar));

                hrz_proto::SingleModelLayerPathBuilder<SceneModelAccessor> builder(
                    accessor, handle);
                set_model_msg(builder._path, layer.single_model());
                break;
            }
            case hrz_proto::LayerDump::kDtmRaster:
            {
                layer_type = hrz_proto::LayerType::DTM_RASTER;
                handle.set_opaque(create_layer(scene, layer_type, layer.name(), ar));

                hrz_proto::DtmRasterLayerPathBuilder<SceneModelAccessor> builder(accessor, handle);
                set_model_msg(builder._path, layer.dtm_raster());
                break;
            }
            case hrz_proto::LayerDump::kImageryRaster:
            {
                layer_type = hrz_proto::LayerType::IMAGERY_RASTER;
                handle.set_opaque(create_layer(scene, layer_type, layer.name(), ar));

                hrz_proto::ImageryRasterLayerPathBuilder<SceneModelAccessor> builder(
                    accessor, handle);
                set_model_msg(builder._path, layer.imagery_raster());
                break;
            }
            case hrz_proto::LayerDump::kVectorData:
            {
                layer_type = hrz_proto::LayerType::VECTOR_DATA;
                handle.set_opaque(create_layer(scene, layer_type, layer.name(), ar));

                hrz_proto::VectorDataLayerPathBuilder<SceneModelAccessor> builder(accessor, handle);
                set_model_msg(builder._path, layer.vector_data());
                break;
            }
            case hrz_proto::LayerDump::kVectorTiles:
            {
                layer_type = hrz_proto::LayerType::VECTOR_TILES;
                handle.set_opaque(create_layer(scene, layer_type, layer.name(), ar));

                hrz_proto::VectorTilesLayerPathBuilder<SceneModelAccessor> builder(
                    accessor, handle);
                set_model_msg(builder._path, layer.vector_tiles());
                break;
            }
            case hrz_proto::LayerDump::kInMemoryVectorSource:
            {
                layer_type = hrz_proto::LayerType::IN_MEMORY_VECTOR_SOURCE;
                handle.set_opaque(create_layer(scene, layer_type, layer.name(), ar));

                hrz_proto::InMemoryVectorSourceLayerPathBuilder<SceneModelAccessor> builder(
                    accessor, handle);
                set_model_msg(builder._path, layer.in_memory_vector_source());
                break;
            }
            case hrz_proto::LayerDump::kThreeDTiles:
            {
                layer_type = hrz_proto::LayerType::THREE_D_TILES;
                handle.set_opaque(create_layer(scene, layer_type, layer.name(), ar));

                hrz_proto::ThreeDTilesLayerPathBuilder<SceneModelAccessor> builder(
                    accessor, handle);
                set_model_msg(builder._path, layer.three_d_tiles());
                break;
            }
            case hrz_proto::LayerDump::kEditableShape:
            {
                layer_type = hrz_proto::LayerType::EDITABLE_SHAPE;
                handle.set_opaque(create_layer(scene, layer_type, layer.name(), ar));

                hrz_proto::EditableShapeLayerPathBuilder<SceneModelAccessor> builder(
                    accessor, handle);
                set_model_msg(builder._path, layer.editable_shape());
                break;
            }
            case hrz_proto::LayerDump::kGizmo:
            {
                layer_type = hrz_proto::LayerType::GIZMO;
                handle.set_opaque(create_layer(scene, layer_type, layer.name(), ar));

                hrz_proto::GizmoLayerPathBuilder<SceneModelAccessor> builder(accessor, handle);
                set_model_msg(builder._path, layer.gizmo());
                break;
            }
            case hrz_proto::LayerDump::kClippingPlane:
            {
                layer_type = hrz_proto::LayerType::CLIPPING_PLANE;
                handle.set_opaque(create_layer(scene, layer_type, layer.name(), ar));

                hrz_proto::ClippingPlaneLayerPathBuilder<SceneModelAccessor> builder(
                    accessor, handle);
                set_model_msg(builder._path, layer.clipping_plane());
                break;
            }
            default:
            {
                assert(!"Unhandled layer type");
                break;
            }
        }

        if (handle.opaque() != 0)
        {
            auto* output_layer = output.add_layers();
            output_layer->mutable_handle()->MergeFrom(handle);
            output_layer->set_type(layer_type);
            output_layer->set_name(layer.name());
        }
    }
}

} // namespace scene
} // namespace hrz
