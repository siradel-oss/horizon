#include "assets_loader/hrz_core_assets_loader.h"
#include "hrz_core.h"
#include "hrz_core_actor_runner.h"
#include "hrz_core_client_message_queue.h"
#include "hrz_core_client_messages.h"
#include "hrz_core_debug_draw.h"
#include "hrz_core_dev_ui.h"
#include "hrz_core_events.h"
#include "hrz_core_gestures.h"
#include "hrz_core_global_flags.h"
#include "hrz_core_image_decoder.h"
#include "hrz_core_job_scheduler.h"
#include "hrz_core_mapbox_translation.h"
#include "hrz_core_picking_system.h"
#include "hrz_core_platform.h"
#include "hrz_core_platform_detection.h"
#include "hrz_core_platform_events_proto.h"
#include "hrz_core_render.h"
#include "hrz_core_render_request.h"
#include "hrz_core_rpc_dispatcher.h"
#include "hrz_core_scene.h"
#include "hrz_core_shader_loader.h"
#include "hrz_core_shaders.h"
#include "hrz_core_shape_editor.h"
#include "hrz_core_stylus.h"
#include "hrz_core_version.h"
#include "monitoring/hrz_core_monitoring.h"
#include "monitoring/hrz_core_monitoring_remote.h"
#include "vector/hrz_core_vector_data_loader.h"

#include <hrz_common_blob_allocator.h>
#include <hrz_common_font_rasterizer.h>
#include <hrz_common_geo.h>
#include <hrz_common_metrics.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proj.h>
#include <hrz_common_proto_geo.h>
#include <hrz_common_proto_maths.h>
#include <hrz_core_resources.h>
#include <hrz_fnd_defines.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_mem.h>
#include <hrz_fnd_time.h>

#include <basisu_transcoder.h>

#include <deque>
#include <vector>

#ifdef HRZ_EMSCRIPTEN
#    include "hrz_js_lib.h"
#endif

#define HRZ_GRAPHICS_SETTINGS                                                             \
    HRZ_DEFINE_GRAPHICS_SETTING(shadows_enabled, "Enable shadows")                        \
    HRZ_DEFINE_GRAPHICS_SETTING(shadows_cascade_count, "Shadow map cascade count")        \
    HRZ_DEFINE_GRAPHICS_SETTING(atmosphere_enabled, "Enable atmosphere")                  \
    HRZ_DEFINE_GRAPHICS_SETTING(flat_overlay_resolution, "Flat overlay resolution")       \
    HRZ_DEFINE_GRAPHICS_SETTING(flat_overlay_cascade_count, "Flat overlay cascade count") \
    HRZ_DEFINE_GRAPHICS_SETTING(                                                          \
        ui_elements_depth_peeling_enabled, "Enable depth peeling for UI elements")        \
    HRZ_DEFINE_GRAPHICS_SETTING(imagery_merge_group_count, "Imagery merge group count")   \
    HRZ_DEFINE_GRAPHICS_SETTING(raster_atlas_size, "Raster atlas size")                   \
    HRZ_DEFINE_GRAPHICS_SETTING(                                                          \
        raster_atlas_texture_compression_enabled, "Enable raster atlas texture compression")

void collect_present_shaders(hrz::GpuResourceContext* rc)
{
    my::IndexName attribs[] = {{0, "i_pos"}};

    my::IndexName ubos[] = {
        {0, "SceneViewport"},
    };

    my::IndexName samplers[] = {
        {0, "u_scene"},
    };

    const char* outputs[] = {"o_color"};

    my::ShaderResource res{};
    res.name = hrz_shaders::Present_name;
    res.link_hint = my::ShaderLinkHint::Initial;
    res.vertex_source_len = hrz_shaders::Present_vert_len;
    res.vertex_source = hrz_shaders::Present_vert;
    res.fragment_source_len = hrz_shaders::Present_frag_len;
    res.fragment_source = hrz_shaders::Present_frag;
    res.attrib_count = HRZ_ARRAY_COUNT(attribs);
    res.attribs = attribs;
    res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
    res.uniform_blocks = ubos;
    res.sampler_count = HRZ_ARRAY_COUNT(samplers);
    res.samplers = samplers;
    res.output_count = HRZ_ARRAY_COUNT(outputs);
    res.outputs = outputs;
    res.initial_state.color_blend.enable = false;
    res.initial_state.depth.test = false;
    res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
    res.initial_state.stencil.enable = false;

    rc->alloc(&res, hrz::monitoring::systems::Presentation);
}

namespace
{
void my_log_adapter(
    const char* prefix,
    my::LogSeverity severity,
    const char* message,
    size_t message_length,
    const char* file,
    int line)
{
    hrz::log::message(prefix, (hrz::log::Severity)severity, {message, message_length}, file, line);
}

class PresentTechnique
{
    my::ResourceHandle _ubo;
    my::ResourceHandle _sampler;
    my::ResourceHandle _vertex_buffer;
    my::ResourceHandle _vertex_input;
    my::ResourceHandle _shader;

    struct SceneViewportUniformData
    {
        lm::ivec2 origin;
        lm::ivec2 size;
        lm::ivec2 screen_resolution;
        lm::ivec2 _padding;
    };

    size_t _ubo_data_stride;

    lm::uvec2 _last_screen_resolution;
    lm::ibbox2 _last_scene_viewport[hrz::SCENE_VIEW_COUNT];

public:
    void init(my::Instance* my, hrz::GpuResourceContext* rc)
    {
        _ubo_data_stride = hrz::render::compute_ubo_stride<SceneViewportUniformData>(
            my->get_uniform_buffer_offset_alignment());

        {
            my::BufferResource res(my::BufferResource::Uniform);
            res.size = _ubo_data_stride * hrz::SCENE_VIEW_COUNT;
            res.data = nullptr;
            res.usage = my::UsageHint::Updatable;

            _ubo = rc->alloc(&res, hrz::monitoring::systems::Presentation);
        }

        {
            my::SamplerResource res;
            res.use_mipmaps = false;
            res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.is_shadow = false;
            res.use_mipmaps = false;

            _sampler = rc->alloc(&res, hrz::monitoring::systems::Presentation);
        }

        {
            static const lm::vec2 triangle_data[] = {
                lm::vec2(0, 0),
                lm::vec2(1, 0),
                lm::vec2(0, 1),
                lm::vec2(1, 1),
            };

            my::BufferResource res(my::BufferResource::Vertex);
            res.usage = my::UsageHint::Static;
            res.size = sizeof(lm::vec2) * HRZ_ARRAY_COUNT(triangle_data);
            res.data = triangle_data;

            _vertex_buffer = rc->alloc(&res, hrz::monitoring::systems::Presentation);
        }

        {
            const my::VertexInputStream streams[] = {
                {0, _vertex_buffer, my::VertexFormat::Float32_2, 0, 0, my::VertexRate::PerVertex},
            };

            my::VertexInputResource res;
            res.attrib_count = HRZ_ARRAY_COUNT(streams);
            res.attribs = streams;

            _vertex_input = rc->alloc(&res, hrz::monitoring::systems::Presentation);
        }

        _shader = rc->retrieve_shader(hrz_shaders::Present_name);

        _last_screen_resolution = {0, 0};

        for (size_t i = 0; i < hrz::SCENE_VIEW_COUNT; ++i)
        {
            _last_scene_viewport[i] = lm::ibbox2::invalid();
        }
    }

    void destroy(my::ResourceContext* rc)
    {
        rc->dealloc(_ubo);
        rc->dealloc(_sampler);
        rc->dealloc(_vertex_buffer);
        rc->dealloc(_vertex_input);
    }

    void execute(
        my::RenderContext* render,
        uint32_t vp_width,
        uint32_t vp_height,
        const hrz::StaticVector<std::pair<my::ResourceHandle, lm::ibbox2>, hrz::SCENE_VIEW_COUNT>&
            views)
    {
        // Copy each scene view to its position on the back buffer.
        //
        // It is achieved through two triangles and a shader instead of a blit, because of
        // compositing issues when using `glBlitFramebuffer()` on certain browsers. Namely,
        // the scene is rendered as black on Chrome when it hasn't been redrawn this frame,
        // and the dev UI is open.
        //
        // Not the bug that causes the issue itself, but an example of the type of bug we
        // are dealing with: https://bugs.chromium.org/p/chromium/issues/detail?id=768969
        //     -tpetillon, 2022-01-27

        {
            my::Rect vp = {0, 0, vp_width, vp_height};

            my::ClearTarget clear = {
                my::Attachment::Color0, my::ClearValue::make_color_float(0, 0, 0, 1)};

            render->set_framebuffer(my::ResourceHandle::null(), {vp, vp});
            render->clear(1, &clear);
        }

        bool update_ubo = false;

        if (lm::uvec2{vp_width, vp_height} != _last_screen_resolution)
        {
            _last_screen_resolution = {vp_width, vp_height};
            update_ubo = true;
        }
        for (size_t i = 0; i < views.size(); ++i)
        {
            if (views[i].second != _last_scene_viewport[i])
            {
                _last_scene_viewport[i] = views[i].second;
                update_ubo = true;
            }
        }

        if (update_ubo)
        {
            for (size_t i = 0; i < views.size(); ++i)
            {
                const auto& view = views[i];

                SceneViewportUniformData viewport;
                viewport.origin = view.second.min;
                viewport.size = lm::size(view.second);
                viewport.screen_resolution = {(int32_t)vp_width, (int32_t)vp_height};

                render->update_buffer(
                    _ubo, _ubo_data_stride * i, sizeof(SceneViewportUniformData), &viewport);
            }
        }

        for (size_t i = 0; i < views.size(); ++i)
        {
            const my::UboBinding ubo_binding{
                0, _ubo, (uint32_t)(_ubo_data_stride * i), sizeof(SceneViewportUniformData)};

            const my::TextureBinding texture_binding{
                0,
                views[i].first,
                _sampler,
            };

            const auto batch_info = my::DrawBatchInfo(my::PrimitiveType::TriangleStrip, 4);

            render->draw(batch_info, _shader, _vertex_input, 1, &ubo_binding, 1, &texture_binding);
        }
    }
};

class MouseHoverPicking
{
public:
    void configure(const hrz_proto::MouseHoverConfiguration& config)
    {
        _should_remove_highlight = _highlight_enabled && !config.enable_highlight();

        _highlight_enabled = config.enable_highlight();
        _info_enabled = config.enable_info();
        _highlight_rate_ms = (double)std::max(33.3f, config.highlight_rate_ms());
        _info_rate_ms = (double)std::max(33.3f, config.info_rate_ms());
    }

    void work(
        hrz::ClientMessageQueue* mq,
        hrz::Scene* scene,
        const hrz::RenderRequest& last_frame_render_request,
        float device_pixel_ratio)
    {
        double now = hrz::now_frame_ms();

        _force_picking_after_delay |=
            (_current_pick_mouse_pos != _mouse_pos
             || (last_frame_render_request.is_render_requested(hrz::RenderRequest::Type::Visual)));

        bool should_do_info = _in_canvas && _info_enabled && now - _last_info >= _info_rate_ms
            && _force_picking_after_delay;

        bool should_do_highlight = _in_canvas && _highlight_enabled
            && now - _last_highlight >= _highlight_rate_ms && _force_picking_after_delay;

        if (_picking_ticket.ticket != 0)
        {
            hrz_proto::MouseHoverInfoMessage hover_info;
            hrz::picking::ObjectReference hovered_object;

            if (hrz::scene::retrieve_mouse_hover_info(
                    scene, _picking_ticket, hover_info, &hovered_object))
            {
                if (should_do_info)
                {
                    hrz_proto::MouseHoverInfoMessage message;
                    message.Swap(&hover_info);

                    message.mutable_screen_pos()->set_x(
                        _current_pick_mouse_pos.x / device_pixel_ratio);
                    message.mutable_screen_pos()->set_y(
                        _current_pick_mouse_pos.y / device_pixel_ratio);

                    hrz::client_message_queue::enqueue_mouse_hover_info_message(
                        mq, std::move(message));

                    _last_info = now;
                }

                if (should_do_highlight)
                {
                    auto hovered_feature =
                        hrz::scene::make_feature_picking_id(scene, hovered_object)
                            .value_or(hrz::picking::FeatureReference());
                    hrz::scene::quick_highlight(scene, hovered_feature);
                    _last_highlight = now;
                }

                _picking_ticket.ticket = 0;
                _force_picking_after_delay = false;
            }
        }
        else if (should_do_info || should_do_highlight)
        {
            _current_pick_mouse_pos = _mouse_pos;
            auto ticket = hrz::scene::schedule_pick(scene, _current_pick_mouse_pos);
            if (ticket.has_value())
            {
                _picking_ticket = ticket.value();
            }
        }

        if (_should_remove_highlight)
        {
            hrz::scene::quick_highlight(scene, hrz::picking::FeatureReference());
            _should_remove_highlight = false;
        }

        if (_info_enabled && _mouse_has_left)
        {
            // Send an empty mouse hover info message.
            hrz::client_message_queue::enqueue_mouse_hover_info_message(
                mq, hrz_proto::MouseHoverInfoMessage());
            _mouse_has_left = false;
        }
    }

    void handle_event(const hrz::Event& event)
    {
        if (event.kind != hrz::Event::Kind::Platform)
        {
            return;
        }

        if (event.platform.kind == hrz::platform::Event::Kind::MouseMove)
        {
            _in_canvas = true;
            _mouse_pos = {event.platform.mouse_move.x, event.platform.mouse_move.y};
        }
        else if (event.platform.kind == hrz::platform::Event::Kind::MouseLeave)
        {
            _in_canvas = false;
            _should_remove_highlight = true;
            _mouse_has_left = true;
        }
    }

private:
    bool _info_enabled = false;
    bool _highlight_enabled = false;
    double _info_rate_ms = 250.0;
    double _highlight_rate_ms = 50.0;
    bool _should_remove_highlight = false;
    bool _mouse_has_left = false;
    bool _in_canvas = false;
    bool _force_picking_after_delay = false;
    hrz::scene::PositionPickingTicket _picking_ticket = {0};
    lm::ivec2 _current_pick_mouse_pos = {0, 0};
    lm::ivec2 _mouse_pos = {0, 0};

    double _last_info = hrz::now_frame_ms();
    double _last_highlight = hrz::now_frame_ms();
};

struct KeyInteractionBindings
{
    hrz::flat_hash_map<hrz::platform::Event::Key, hrz_proto::KeyAction> mappings;
    hrz::platform::Event::Key mod_key;

    explicit KeyInteractionBindings(const hrz_proto::ViewerOptions& options)
    {
        for (const auto& binding : options.key_bindings().bindings())
        {
            if (binding.action() == hrz_proto::KeyAction::MOD_KEY)
            {
                mod_key = hrz::platform::from_proto(binding.key());
            }
            else if (binding.key() != hrz_proto::Key::K_NONE)
            {
                mappings[hrz::platform::from_proto(binding.key())] = binding.action();
            }
        }
    }

    bool handle_platform_event(
        const hrz::platform::Event& event,
        hrz::DevUi* ui,
        hrz::Scene* scene,
        hrz::RemoteMonitoring* remote_monitoring,
        std::vector<hrz::Event>& events)
    {
        auto execute_action = [&](hrz_proto::KeyAction action) -> bool
        {
            switch (action)
            {
                case hrz_proto::KeyAction::RESET_NORTH:
                {
                    hrz_proto::CameraAnimationOptions animation_options;
                    animation_options.set_duration(0.5);
                    animation_options.set_easing_function(hrz_proto::EasingFunctions::EASE_INOUT);
                    animation_options.set_easing_exponent(2);

                    for (size_t i = 0; i < hrz::CAMERA_COUNT; ++i)
                    {
                        hrz_proto::ResetNorthParams params;
                        params.set_camera_index((hrz_proto::CameraIndex)(hrz_proto::CAMERA_0 + i));
                        params.set_reset_tilt(true);
                        params.mutable_animation_options()->CopyFrom(animation_options);

                        hrz_proto::Void output;
                        hrz::scene::get_camera_service(scene)->reset_north(params, output);
                    }
                    break;
                }
                case hrz_proto::KeyAction::DESELECT_ALL: hrz::scene::deselect_all(scene); break;
                case hrz_proto::KeyAction::TOGGLE_DEV_UI: hrz::dev_ui::toggle(ui); break;
                case hrz_proto::KeyAction::MOVE_DEV_UI: hrz::dev_ui::move_to_cursor(ui); break;
                case hrz_proto::KeyAction::EDITOR_APPEND:
                    hrz::editor::set_mode(
                        hrz::scene::get_shape_editor(scene),
                        hrz_proto::ShapeEditorMode::APPEND_MODE);
                    break;
                case hrz_proto::KeyAction::EDITOR_SELECT:
                    hrz::editor::set_mode(
                        hrz::scene::get_shape_editor(scene),
                        hrz_proto::ShapeEditorMode::SELECTION_MODE);
                    break;
                case hrz_proto::KeyAction::EDITOR_DELETE_SELECTED_POINT:
                    hrz::editor::delete_selected_control_point(hrz::scene::get_shape_editor(scene));
                    break;
                case hrz_proto::KeyAction::TOGGLE_MONITORING:
                    if (hrz::monitoring::is_connected(remote_monitoring))
                    {
                        hrz::monitoring::disconnect(remote_monitoring);
                        hrz::profiling::set_profiling_enabled(false);
                        hrz::metrics::set_metrics_registries_enabled(false);
                    }
                    else
                    {
                        hrz::monitoring::try_connect(
                            remote_monitoring,
                            [](bool success)
                            {
                                if (success)
                                {
                                    hrz::profiling::set_profiling_enabled(true);
                                    hrz::metrics::set_metrics_registries_enabled(true);
                                }
                            });
                    }
                    break;
                default: return false;
            }
            return true;
        };

        if (event.kind == hrz::platform::Event::Kind::KeyDown)
        {
            if (event.key == mod_key)
            {
                events.push_back(hrz::Event{hrz::Event::Kind::ModKeyDown});
                return true;
            }

            auto it = mappings.find(event.key);
            if (it != mappings.end())
            {
                return execute_action(it->second);
            }
        }

        if (event.kind == hrz::platform::Event::Kind::KeyUp && event.key == mod_key)
        {
            events.push_back(hrz::Event{hrz::Event::Kind::ModKeyUp});
            return true;
        }

        return false;
    }
};

#if HRZ_INTERNAL_INTEGRATION
// This is all the temporary stuff used exclusively by the desktop client
// to test features normally only accessible from a full-featured clients, like
// picking, multiselection, etc.
class DesktopClientIntegration
{
    hrz::scene::PositionPickingTicket _position_picking_ticket;
    hrz::scene::AreaPickingTicket _area_picking_ticket;
    lm::ivec2 _last_mouse_press_pos = {-1, -1};
    lm::ivec2 _mouse_press_pos = {-1, -1};
    bool _must_quit = false;
    bool _is_modkey_down = false;

    bool _is_doing_rectangle_selection = false;
    bool _is_drawing_rectangle = false;
    bool _rectangle_drawn = false;
    lm::ivec2 _rect_0, _rect_1;

    hrz::scene::PositionPickingTicket _raster_data_fetch_picking_ticket;
    hrz::scene::RasterDataFetchTicket _raster_data_fetch_ticket;
    lm::ivec2 _mouse_pos = {-1, -1};
    bool _is_requesting_raster_data_fetch = false;

    bool _handle_platform_event(const hrz::platform::Event& event, hrz::Scene* scene)
    {
        bool prevent_propagation = false;

        switch (event.kind)
        {
            case hrz::platform::Event::Kind::KeyDown:
            {
                switch (event.key)
                {
                    case hrz::platform::Event::Key::Escape: _must_quit = true; break;
                    case hrz::platform::Event::Key::T: _is_doing_rectangle_selection = true; break;
                    case hrz::platform::Event::Key::F:
                        _is_requesting_raster_data_fetch = true;
                        break;
                    default: break;
                }
                break;
            }
            case hrz::platform::Event::Kind::KeyUp:
            {
                switch (event.key)
                {
                    case hrz::platform::Event::Key::T:
                        _is_drawing_rectangle = false;
                        _is_doing_rectangle_selection = false;
                        break;
                    default: break;
                }
                break;
            }
            case hrz::platform::Event::Kind::MouseMove:
            {
                _mouse_pos = {event.mouse_move.x, event.mouse_move.y};
                if (_is_drawing_rectangle)
                {
                    _rect_1 = _mouse_pos;
                    prevent_propagation = true;
                }
                _last_mouse_press_pos = {-1, -1};
                break;
            }
            case hrz::platform::Event::Kind::MouseButtonDown:
            {
                if (event.mouse_button.button == hrz::platform::Event::MouseButton::Left)
                {
                    if (_is_doing_rectangle_selection)
                    {
                        _rect_0 = {event.mouse_button.x, event.mouse_button.y};
                        _rect_1 = _rect_0;
                        _is_drawing_rectangle = true;
                        prevent_propagation = true;
                    }
                    else
                    {
                        _last_mouse_press_pos = {
                            event.mouse_button.x,
                            event.mouse_button.y,
                        };
                    }
                }
                break;
            }
            case hrz::platform::Event::Kind::MouseButtonUp:
            {
                if (event.mouse_button.button == hrz::platform::Event::MouseButton::Left)
                {
                    if (_is_drawing_rectangle)
                    {
                        _is_drawing_rectangle = false;
                        _rectangle_drawn = true;
                        prevent_propagation = true;
                    }
                    else if (
                        _last_mouse_press_pos != lm::ivec2(-1, -1)
                        && lm::all(
                            lm::abs(
                                _last_mouse_press_pos
                                - lm::ivec2(event.mouse_button.x, event.mouse_button.y))
                            < lm::ivec2(4)))
                    {
                        _mouse_press_pos = _last_mouse_press_pos;
                    }
                }
            }
            default: break;
        }

        return prevent_propagation;
    }

    bool _handle_gesture_event(const hrz::gestures::Event& event, hrz::Scene* scene)
    {
        switch (event.kind)
        {
            case hrz::gestures::Event::Kind::Qualification:
            {
                if (event.finger_count() == hrz::gestures::FingerCount::One)
                {
                    const auto& single_finger_gesture = event.single_finger_gesture();
                    if (single_finger_gesture.type == hrz::gestures::SingleFingerGesture::Type::Tap)
                    {
                        _mouse_press_pos = {
                            (int)single_finger_gesture.position.x,
                            (int)single_finger_gesture.position.y};
                    }
                }
                break;
            }
            default: break;
        }

        return false;
    }

public:
    bool must_quit() const { return _must_quit; }

    void register_keys_to_capture(hrz::PlatformContext* platform)
    {
        hrz::platform::add_key_to_capture(platform, hrz::platform::Event::Key::Escape);
        hrz::platform::add_key_to_capture(platform, hrz::platform::Event::Key::T);
        hrz::platform::add_key_to_capture(platform, hrz::platform::Event::Key::F);
    }

    bool handle_event(const hrz::Event& event, hrz::Scene* scene)
    {
        bool prevent_propagation = false;

        switch (event.kind)
        {
            case hrz::Event::Kind::Platform: return _handle_platform_event(event.platform, scene);
            case hrz::Event::Kind::Gesture: return _handle_gesture_event(event.gesture, scene);
            case hrz::Event::Kind::ModKeyDown: _is_modkey_down = true; return false;
            case hrz::Event::Kind::ModKeyUp: _is_modkey_down = false; return false;
            default: break;
        }

        return prevent_propagation;
    }

    void update(hrz::Scene* scene)
    {
        if (_mouse_press_pos != lm::ivec2(-1, -1))
        {
            ::hrz_proto::LayerArray layers;
            hrz::scene::retrieve_all_layers(scene, layers);

            std::vector<::hrz_proto::LayerHandle> included_rasters;
            for (const auto& layer : layers.layers())
            {
                if (layer.has_handle()
                    && (layer.type() == ::hrz_proto::LayerType::IMAGERY_RASTER
                        || layer.type() == ::hrz_proto::LayerType::DTM_RASTER))
                {
                    included_rasters.push_back(layer.handle());
                }
            }

            auto ticket = hrz::scene::schedule_pick(scene, _mouse_press_pos, included_rasters);
            if (ticket.has_value())
            {
                _position_picking_ticket = ticket.value();
            }
            _mouse_press_pos = lm::ivec2(-1, -1);
        }

        if (_rectangle_drawn)
        {
            lm::ibbox2 bbox = lm::ibbox2::invalid();
            bbox = lm::expand(lm::expand(bbox, _rect_0), _rect_1);
            auto ticket = hrz::scene::schedule_pick(scene, bbox);
            if (ticket.has_value())
            {
                _area_picking_ticket = ticket.value();
            }
            _rectangle_drawn = false;
        }

        if (_is_requesting_raster_data_fetch)
        {
            ::hrz_proto::LayerArray layers;
            hrz::scene::retrieve_all_layers(scene, layers);

            std::vector<::hrz_proto::LayerHandle> included_rasters;
            for (const auto& layer : layers.layers())
            {
                // Since we only want to retrieve the picked position, we don't need to include
                // imagery rasters.
                if (layer.has_handle() && layer.type() == ::hrz_proto::LayerType::DTM_RASTER)
                {
                    included_rasters.push_back(layer.handle());
                }
            }

            auto ticket = hrz::scene::schedule_pick(scene, _mouse_pos, included_rasters);
            if (ticket.has_value())
            {
                _raster_data_fetch_picking_ticket = ticket.value();
            }
            _is_requesting_raster_data_fetch = false;
        }

        hrz_proto::PickResults picking_results;
        if (_position_picking_ticket.ticket != 0
            && hrz::scene::retrieve_pick_results(scene, _position_picking_ticket, picking_results))
        {
            if (!_is_modkey_down)
            {
                hrz::scene::deselect_all(scene);
            }

            if (!picking_results.has_position())
            {
                HRZ_LOG_INFO("Picked outside of planet.");
            }
            else
            {
                auto picked_pos = picking_results.position();
                HRZ_LOG_INFO(
                    "Picked position: {:.9} deg lat, {:.9} deg lon, {:7} m", picked_pos.latitude(),
                    picked_pos.longitude(), picked_pos.altitude());

                if (picking_results.results_size() == 0)
                {
                    HRZ_LOG_INFO("No layer picked.");
                }

                for (const auto& result : picking_results.results())
                {
                    HRZ_LOG_INFO(
                        "Picked layer of type {} with handle {:x}",
                        hrz_proto::LayerType_Name(result.layer().type()),
                        result.layer().handle().opaque());

                    auto print_feature_id = [](const hrz_proto::FeatureId& feature_id)
                    {
                        for (const auto& attribute : feature_id.attributes())
                        {
                            auto attribute_id = attribute.id();
                            const auto& value = attribute.value();

                            if (value.has_number_value())
                            {
                                HRZ_LOG_INFO(
                                    "    - ID attribute {} = {}", attribute_id,
                                    value.number_value());
                            }
                            else if (value.has_int64_value())
                            {
                                HRZ_LOG_INFO(
                                    "    - ID attribute {} = {}", attribute_id,
                                    value.int64_value());
                            }
                            else if (value.has_uint64_value())
                            {
                                HRZ_LOG_INFO(
                                    "    - ID attribute {} = {}", attribute_id,
                                    value.uint64_value());
                            }
                            else if (value.has_string_value())
                            {
                                HRZ_LOG_INFO(
                                    "    - ID attribute {} = {}", attribute_id,
                                    value.string_value());
                            }
                            else if (value.has_boolean_value())
                            {
                                HRZ_LOG_INFO(
                                    "    - ID attribute {} = {}", attribute_id,
                                    value.boolean_value());
                            }
                            else
                            {
                                HRZ_LOG_INFO("    - ID attribute {} = null", attribute_id);
                            }
                        }
                    };

                    switch (result.payload_case())
                    {
                        case hrz_proto::PickLayerResult::kRaster:
                        {
                            auto raster = result.raster();
                            switch (raster.payload_case())
                            {
                                case hrz_proto::RasterPickResult::kColor:
                                    HRZ_LOG_INFO(
                                        "Payload [{}] - Color: (r:{}, g:{}, b:{}, a:{}){}",
                                        hrz_proto::LayerType_Name(result.layer().type()),
                                        raster.color().r(), raster.color().g(), raster.color().b(),
                                        raster.color().a(), raster.nodata() ? " - Nodata" : "");
                                    break;
                                case hrz_proto::RasterPickResult::kNumber:
                                    switch (result.layer().type())
                                    {
                                        case hrz_proto::LayerType::DTM_RASTER:
                                            HRZ_LOG_INFO(
                                                "Payload [{}] - Elevation: {} m{}",
                                                hrz_proto::LayerType_Name(result.layer().type()),
                                                raster.number(),
                                                raster.nodata() ? " - Nodata" : "");
                                            break;
                                        case hrz_proto::LayerType::IMAGERY_RASTER:
                                            HRZ_LOG_INFO(
                                                "Payload [{}] - Scalar value: {}{}",
                                                hrz_proto::LayerType_Name(result.layer().type()),
                                                raster.number(),
                                                raster.nodata() ? " - Nodata" : "");
                                            break;
                                        default:
                                            assert(false);
                                            HRZ_LOG_ERROR("Unhandled type");
                                            break;
                                    }
                                    break;
                                case hrz_proto::RasterPickResult::kNothing:
                                    HRZ_LOG_INFO(
                                        "Payload - No further information found about that layer.");
                                    break;
                                default:
                                    assert(false);
                                    HRZ_LOG_ERROR("Unhandled payload");
                                    break;
                            }
                            break;
                        }
                        case hrz_proto::PickLayerResult::kVector:
                        {
                            HRZ_LOG_INFO("Payload - Picked vector feature");

                            HRZ_LOG_INFO(
                                "    Position: {:.9} deg lat, {:.9} deg lon, {:.2} m alt",
                                result.vector().feature_anchor().latitude(),
                                result.vector().feature_anchor().longitude(),
                                result.vector().feature_anchor().altitude());

                            if (result.vector().has_feature_id())
                            {
                                print_feature_id(result.vector().feature_id());

                                hrz::vector_data::FeatureIdHash feature_id[] = {
                                    hrz::vector_data::FeatureId::from_proto(
                                        result.vector().feature_id())
                                        .hash()};
                                hrz::scene::select(
                                    scene, result.layer().handle().opaque(),
                                    gsl::span<const hrz::vector_data::FeatureIdHash>(feature_id));
                            }

                            for (int i = 0; i < result.vector().ids_size(); ++i)
                            {
                                const auto& name = result.vector().ids(i);
                                const auto& value = result.vector().values(i);

                                if (value.has_number_value())
                                {
                                    HRZ_LOG_INFO(
                                        "    Attribute {} = {}", name, value.number_value());
                                }
                                else if (value.has_int64_value())
                                {
                                    HRZ_LOG_INFO(
                                        "    Attribute {} = {}", name, value.int64_value());
                                }
                                else if (value.has_uint64_value())
                                {
                                    HRZ_LOG_INFO(
                                        "    Attribute {} = {}", name, value.uint64_value());
                                }
                                else if (value.has_string_value())
                                {
                                    HRZ_LOG_INFO(
                                        "    Attribute {} = {}", name, value.string_value());
                                }
                                else if (value.has_boolean_value())
                                {
                                    HRZ_LOG_INFO(
                                        "    Attribute {} = {}", name, value.boolean_value());
                                }
                                else
                                {
                                    HRZ_LOG_INFO("    Attribute {} = null", name);
                                }
                            }

                            for (const auto& heatmap : result.vector().heatmaps())
                            {
                                HRZ_LOG_INFO(
                                    "Payload - Picked heatmap representation {} at value {}",
                                    heatmap.repr_id(), heatmap.value());
                            }
                            break;
                        }
                        case hrz_proto::PickLayerResult::kModel:
                        {
                            hrz::vector_data::FeatureIdHash feature_id[] = {0};
                            HRZ_LOG_INFO(
                                "Payload - Data texture value = {}",
                                result.model().data_texture_value());
                            hrz::scene::select(
                                scene, result.layer().handle().opaque(),
                                gsl::span<const hrz::vector_data::FeatureIdHash>(feature_id));
                            break;
                        }
                        case hrz_proto::PickLayerResult::kThreeDTile:
                        {
                            if (result.three_d_tile().has_feature_id())
                            {
                                HRZ_LOG_INFO(
                                    "Payload - 3D Tile feature at ECEF position ({}, "
                                    "{}, "
                                    "{}), value = {}",
                                    result.three_d_tile().position().x(),
                                    result.three_d_tile().position().y(),
                                    result.three_d_tile().position().z(),
                                    result.three_d_tile().data_texture_value());
                                print_feature_id(result.three_d_tile().feature_id());

                                hrz::vector_data::FeatureIdHash feature_id[] = {
                                    hrz::vector_data::FeatureId::from_proto(
                                        result.three_d_tile().feature_id())
                                        .hash()};
                                hrz::scene::select(
                                    scene, result.layer().handle().opaque(),
                                    gsl::span<const hrz::vector_data::FeatureIdHash>(feature_id));
                            }
                            else
                            {
                                HRZ_LOG_INFO(
                                    "Payload - 3D Tile layer at ECEF position ({}, {}, "
                                    "{}), value = {}",
                                    result.three_d_tile().position().x(),
                                    result.three_d_tile().position().y(),
                                    result.three_d_tile().position().z(),
                                    result.three_d_tile().data_texture_value());
                            }

                            for (int i = 0; i < result.three_d_tile().names_size(); ++i)
                            {
                                const auto& name = result.three_d_tile().names(i);
                                const auto& value = result.three_d_tile().values(i);

                                if (value.has_number_value())
                                {
                                    HRZ_LOG_INFO(
                                        "    Attribute {} = {}", name, value.number_value());
                                }
                                else if (value.has_int64_value())
                                {
                                    HRZ_LOG_INFO(
                                        "    Attribute {} = {}", name, value.int64_value());
                                }
                                else if (value.has_uint64_value())
                                {
                                    HRZ_LOG_INFO(
                                        "    Attribute {} = {}", name, value.uint64_value());
                                }
                                else if (value.has_string_value())
                                {
                                    HRZ_LOG_INFO(
                                        "    Attribute {} = {}", name, value.string_value());
                                }
                                else if (value.has_boolean_value())
                                {
                                    HRZ_LOG_INFO(
                                        "    Attribute {} = {}", name, value.boolean_value());
                                }
                                else
                                {
                                    HRZ_LOG_INFO("    Attribute {} = null", name);
                                }
                            }
                            break;
                        }
                        case hrz_proto::PickLayerResult::kEditableShape:
                        {
                            break;
                        }
                        default: HRZ_LOG_ERROR("Unknown picking payload"); break;
                    }
                }
            }

            _position_picking_ticket.ticket = 0;
        }

        std::vector<hrz_proto::TypedObjectReference> typed_objs;
        if (_area_picking_ticket.ticket != 0
            && hrz::scene::retrieve_pick_area_result(scene, _area_picking_ticket, typed_objs))
        {
            if (!_is_modkey_down)
            {
                hrz::scene::deselect_all(scene);
            }

            HRZ_LOG_INFO("{} features selected", typed_objs.size());

            hrz::flat_hash_map<uint64_t, std::vector<hrz::vector_data::FeatureIdHash>>
                selected_objects_per_layer;
            for (const auto& ref : typed_objs)
            {
                if (ref.has_single_model())
                {
                    selected_objects_per_layer[ref.single_model().opaque()].push_back(0);
                }
                else if (ref.has_three_d_tiles())
                {
                    if (!ref.three_d_tiles().has_feature_id())
                    {
                        selected_objects_per_layer[ref.three_d_tiles().layer().opaque()].push_back(
                            0);
                    }
                    else
                    {
                        selected_objects_per_layer[ref.three_d_tiles().layer().opaque()].push_back(
                            hrz::vector_data::FeatureId::from_proto(
                                ref.three_d_tiles().feature_id())
                                .hash());
                    }
                }
                else if (ref.has_vector_tiles())
                {
                    selected_objects_per_layer[ref.vector_tiles().layer().opaque()].push_back(
                        hrz::vector_data::FeatureId::from_proto(ref.vector_tiles().feature_id())
                            .hash());
                }
            }

            for (const auto& layer : selected_objects_per_layer)
            {
                hrz::scene::select(scene, layer.first, layer.second);
            }

            _area_picking_ticket.ticket = 0;
        }

        picking_results = {};
        if (_raster_data_fetch_picking_ticket.ticket != 0
            && hrz::scene::retrieve_pick_results(
                scene, _raster_data_fetch_picking_ticket, picking_results))
        {
            ::hrz_proto::LayerArray layers;
            hrz::scene::retrieve_all_layers(scene, layers);

            std::vector<::hrz_proto::LayerHandle> included_rasters;
            for (const auto& layer : layers.layers())
            {
                if (layer.has_handle()
                    && (layer.type() == ::hrz_proto::LayerType::IMAGERY_RASTER
                        || layer.type() == ::hrz_proto::LayerType::DTM_RASTER))
                {
                    included_rasters.push_back(layer.handle());
                }
            }

            auto picked_pos = picking_results.position();
            hrz::GeoPosition2 geo = hrz::from_proto(picked_pos).latlon();

            HRZ_LOG_INFO(
                "Scheduling raster data fetch at lat {} deg; lon {} deg", picked_pos.latitude(),
                picked_pos.longitude());

            _raster_data_fetch_ticket =
                hrz::scene::schedule_raster_data_fetch(scene, geo, included_rasters);
            _raster_data_fetch_picking_ticket.ticket = 0;
        }

        std::vector<hrz_proto::PickLayerResult> fetch_results;
        if (_raster_data_fetch_ticket.ticket != 0
            && hrz::scene::retrieve_raster_data_fetch_results(
                scene, _raster_data_fetch_ticket, fetch_results))
        {
            for (const auto& result : fetch_results)
            {
                HRZ_LOG_INFO(
                    "Fetched layer of type {} with handle {:x}",
                    hrz_proto::LayerType_Name(result.layer().type()),
                    result.layer().handle().opaque());

                switch (result.payload_case())
                {
                    case HrzProtocol::PickLayerResult::kRaster:
                    {
                        auto raster = result.raster();
                        switch (raster.payload_case())
                        {
                            case hrz_proto::RasterPickResult::kColor:
                                HRZ_LOG_INFO(
                                    "Payload [{}] - Color: (r:{}, g:{}, b:{}, a:{}){}",
                                    hrz_proto::LayerType_Name(result.layer().type()),
                                    raster.color().r(), raster.color().g(), raster.color().b(),
                                    raster.color().a(), raster.nodata() ? " - Nodata" : "");
                                break;
                            case hrz_proto::RasterPickResult::kNumber:
                                switch (result.layer().type())
                                {
                                    case hrz_proto::LayerType::DTM_RASTER:
                                        HRZ_LOG_INFO(
                                            "Payload [{}] - Elevation: {} m{}",
                                            hrz_proto::LayerType_Name(result.layer().type()),
                                            raster.number(), raster.nodata() ? " - Nodata" : "");
                                        break;
                                    case hrz_proto::LayerType::IMAGERY_RASTER:
                                        HRZ_LOG_INFO(
                                            "Payload [{}] - Scalar value: {}{}",
                                            hrz_proto::LayerType_Name(result.layer().type()),
                                            raster.number(), raster.nodata() ? " - Nodata" : "");
                                        break;
                                    default:
                                        assert(false);
                                        HRZ_LOG_ERROR("Unhandled type");
                                        break;
                                }
                                break;
                            case hrz_proto::RasterPickResult::kNothing:
                                HRZ_LOG_INFO(
                                    "Payload - No further information found about that layer.");
                                break;
                            default:
                                assert(false);
                                HRZ_LOG_ERROR("Unhandled payload");
                                break;
                        }
                        break;
                    }

                    default: HRZ_LOG_ERROR("Wrong payload for raster data fetch result."); break;
                }
            }

            _raster_data_fetch_ticket.ticket = 0;
        }
    }
};
#endif

class MyceliumProfilingInstance : public my::Instance
{
    std::unique_ptr<my::Instance> _inst;

public:
    MyceliumProfilingInstance() : _inst(my::Instance::create()) {}

    //
    // my::Instance
    //

    void configure_shaders_linking(const ShadersLinkingConfig& config) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: configure_shaders_linking");
        _inst->configure_shaders_linking(config);
    }

    void advance_shaders_link(bool idle) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: advance_shaders_link");
        _inst->advance_shaders_link(idle);
    }

    void add_global_shader_define(const char* name, const char* value = "") override
    {
        _inst->add_global_shader_define(name, value);
    }

    const Info& get_info() const override { return _inst->get_info(); }

    size_t get_uniform_buffer_offset_alignment() const override
    {
        return _inst->get_uniform_buffer_offset_alignment();
    }

    bool is_texture_format_available(my::TextureFormat format) const override
    {
        return _inst->is_texture_format_available(format);
    }

    bool is_texture_download_ready(uint64_t id) const override
    {
        HRZ_SCOPED_SAMPLE("mycelium: is_texture_download_ready");
        return _inst->is_texture_download_ready(id);
    }

    void cancel_texture_download(uint64_t id) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: cancel_texture_download");
        _inst->cancel_texture_download(id);
    }

    my::TextureDownloadData retrieve_texture_download(uint64_t id) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: retrieve_texture_download");
        return _inst->retrieve_texture_download(id);
    }

    my::QueryTimeResult retrieve_elapsed_time(uint64_t id, my::QueryTimeStatus& status) override
    {
        return _inst->retrieve_elapsed_time(id, status);
    }

    void delete_query(uint64_t id) override { _inst->delete_query(id); }

    void activate_resource_allocation_reports() override
    {
        _inst->activate_resource_allocation_reports();
    }

    void deactivate_resource_allocation_reports() override
    {
        _inst->deactivate_resource_allocation_reports();
    }

    size_t get_resource_allocation_report_count() const override
    {
        return _inst->get_resource_allocation_report_count();
    }

    const my::ResourceAllocationReport* get_resource_allocation_reports() override
    {
        return _inst->get_resource_allocation_reports();
    }

    void clear_resource_allocation_reports() override
    {
        _inst->clear_resource_allocation_reports();
    }

    void set_memory_limit(uint64_t size_bytes) override { _inst->set_memory_limit(size_bytes); }

    GpuMemoryInfo get_memory_info() override { return _inst->get_memory_info(); }

    ShadersInfo get_shaders_info() const override { return _inst->get_shaders_info(); }

    bool begin_frame() override
    {
        HRZ_SCOPED_SAMPLE("mycelium: begin_frame");
        return _inst->begin_frame();
    }

    void end_frame() override
    {
        HRZ_SCOPED_SAMPLE("mycelium: end_frame");
        _inst->end_frame();
    }

    //
    // my::ResourceContext
    //

    my::ResourceHandle alloc(const my::Resource* res) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: resource alloc");
        return _inst->alloc(res);
    }

    void dealloc(my::ResourceHandle handle) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: resource dealloc");
        _inst->dealloc(handle);
    }

    void realloc_buffer(my::ResourceHandle handle, const my::BufferResource* res) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: resource realloc_buffer");
        _inst->realloc_buffer(handle, res);
    }

    void update_texture_layout(my::ResourceHandle handle, const my::TextureLayout& layout) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: resource update_texture_layout");
        _inst->update_texture_layout(handle, layout);
    }

    void update_renderbuffer_size(my::ResourceHandle handle, uint32_t width, uint32_t height)
        override
    {
        HRZ_SCOPED_SAMPLE("mycelium: resource update_renderbuffer_size");
        _inst->update_renderbuffer_size(handle, width, height);
    }

    my::ResourceHandle retrieve_shader(const char* name) const override
    {
        HRZ_SCOPED_SAMPLE("mycelium: retrieve shader");
        return _inst->retrieve_shader(name);
    }

    //
    // RenderContext
    //

    void clear(uint32_t clear_count, const my::ClearTarget* values) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: cmd clear");
        _inst->clear(clear_count, values);
    }

    void set_viewport(const my::ViewportState& viewport) override { _inst->set_viewport(viewport); }

    void set_framebuffer(my::ResourceHandle fbo, const my::ViewportState& vp) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: cmd set_framebuffer");
        _inst->set_framebuffer(fbo, vp);
    }

    void update_buffer(my::ResourceHandle buffer, size_t offset, size_t size, const void* data)
        override
    {
        HRZ_SCOPED_SAMPLE("mycelium: cmd update_buffer");
        _inst->update_buffer(buffer, offset, size, data);
    }

    void update_texture(
        my::ResourceHandle texture,
        my::TextureFormat format,
        int level,
        uint32_t x,
        uint32_t y,
        uint32_t z,
        uint32_t w,
        uint32_t h,
        uint32_t d,
        gsl::span<const std::byte> data,
        TextureUpdateDataLayout data_layout) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: cmd update_texture");
        _inst->update_texture(texture, format, level, x, y, z, w, h, d, data, data_layout);
    }

    void draw(
        const my::DrawBatchInfo& info,
        my::ResourceHandle shader,
        my::ResourceHandle vertex_input,
        uint32_t ubo_count,
        const my::UboBinding* ubos,
        uint32_t texture_count,
        const my::TextureBinding* textures) override
    {
        HRZ_SCOPED_SAMPLE_A("mycelium: cmd draw");
        _inst->draw(info, shader, vertex_input, ubo_count, ubos, texture_count, textures);
    }

    void blit_framebuffers(
        my::ResourceHandle src,
        my::Rect src_rect,
        my::Rect dst_rect,
        my::AspectFlags aspects,
        my::Attachment src_attachment,
        uint32_t dst_attachment_count,
        const my::Attachment* dst_attachments,
        my::SamplerParams::Filter filter) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: cmd blit_framebuffers");
        _inst->blit_framebuffers(
            src, src_rect, dst_rect, aspects, src_attachment, dst_attachment_count, dst_attachments,
            filter);
    }

    my::TextureDownloadData color_texture_download_sync(
        my::ResourceHandle framebuffer,
        my::Attachment color_attachment,
        my::Rect rect,
        my::TextureDownloadFormat format) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: cmd color_texture_download_sync");
        return _inst->color_texture_download_sync(framebuffer, color_attachment, rect, format);
    }

    void color_texture_download_async(
        uint64_t download_id,
        my::ResourceHandle framebuffer,
        my::Attachment color_attachment,
        my::Rect rect,
        my::TextureDownloadFormat format,
        my::ResourceHandle buffer) override
    {
        HRZ_SCOPED_SAMPLE("mycelium: cmd color_texture_download_async");
        _inst->color_texture_download_async(
            download_id, framebuffer, color_attachment, rect, format, buffer);
    }

    void begin_time_query(uint64_t query_id, const char* name) override
    {
        _inst->begin_time_query(query_id, name);
    }

    void end_time_query(uint64_t query_id) override { _inst->end_time_query(query_id); }
};

class LoadingScreenTechnique
{
    static constexpr uint32_t NumFramesToSkipBeforeFadeout = 2;
    static constexpr double FadeoutDurationMs = 500;

    my::ResourceHandle _ubo;
    my::ResourceHandle _vertex_buffer;
    my::ResourceHandle _vertex_input;
    my::ResourceHandle _shader;
    my::ResourceHandle _texture;
    my::ResourceHandle _sampler;

    bool _fadeout = false;
    double _fadeout_start_ms;
    uint32_t _num_frames_skipped;

    struct LoadingScreenUniformData
    {
        uint32_t num_shaders_total;
        uint32_t num_shaders_ready;
        uint32_t viewport_width;
        uint32_t viewport_height;
        uint32_t draw_logo;
        float fadeout;
        uint32_t _padding[2];
    };

    struct SceneViewportUniformData
    {
        lm::ivec2 origin;
        lm::ivec2 size;
        lm::ivec2 screen_resolution;
        lm::ivec2 _padding;
    };

public:
    void init(my::Instance* my, hrz::GpuResourceContext* rc)
    {
        {
            my::BufferResource res(my::BufferResource::Uniform);
            res.size = sizeof(LoadingScreenUniformData);
            res.data = nullptr;
            res.usage = my::UsageHint::Updatable;

            _ubo = rc->alloc(&res, hrz::monitoring::systems::LoadingScreen);
        }

        {
            static const lm::vec2 triangle_data[] = {
                lm::vec2(0, 0),
                lm::vec2(1, 0),
                lm::vec2(0, 1),
                lm::vec2(1, 1),
            };

            my::BufferResource res(my::BufferResource::Vertex);
            res.usage = my::UsageHint::Static;
            res.size = sizeof(lm::vec2) * HRZ_ARRAY_COUNT(triangle_data);
            res.data = triangle_data;

            _vertex_buffer = rc->alloc(&res, hrz::monitoring::systems::LoadingScreen);
        }

        {
            const my::VertexInputStream streams[] = {
                {0, _vertex_buffer, my::VertexFormat::Float32_2, 0, 0, my::VertexRate::PerVertex},
            };

            my::VertexInputResource res;
            res.attrib_count = HRZ_ARRAY_COUNT(streams);
            res.attribs = streams;

            _vertex_input = rc->alloc(&res, hrz::monitoring::systems::LoadingScreen);
        }

        {
            auto data = hrz_res::get_data(hrz_res::Resources::HrzLogo);
            my::TextureResource res;
            res.layout.type = my::TextureLayout::Type2D;
            res.layout.format = my::TextureFormat::RGBA8;
            res.layout.width = 96;
            res.layout.height = 96;
            res.layout.depth = 1;
            res.layout.levels = 1;
            res.data = {&data, 1};
            res.generate_mipmaps = false;
            res.is_render_graph_texture = false;
            res.name = "hrz logo";

            _texture = rc->alloc(&res, hrz::monitoring::systems::LoadingScreen);
        }

        {
            my::SamplerResource res;
            res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
            res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
            res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
            res.sampler.min_filter = my::SamplerParams::Filter::Linear;
            res.sampler.is_shadow = false;
            res.use_mipmaps = false;

            _sampler = rc->alloc(&res, hrz::monitoring::systems::LoadingScreen);
        }

        {
            my::IndexName attribs[] = {{0, "i_pos"}};
            my::IndexName ubos[] = {{0, "LoadScreen"}};
            my::IndexName samplers[] = {{0, "u_logo"}};
            const char* outputs[] = {"o_color"};

            my::ShaderResource res{};
            res.name = hrz_shaders::LoadingScreen_name;
            res.link_hint = my::ShaderLinkHint::FirstUseImmediate;
            res.vertex_source_len = hrz_shaders::LoadingScreen_vert_len;
            res.vertex_source = hrz_shaders::LoadingScreen_vert;
            res.fragment_source_len = hrz_shaders::LoadingScreen_frag_len;
            res.fragment_source = hrz_shaders::LoadingScreen_frag;
            res.attrib_count = HRZ_ARRAY_COUNT(attribs);
            res.attribs = attribs;
            res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
            res.uniform_blocks = ubos;
            res.sampler_count = HRZ_ARRAY_COUNT(samplers);
            res.samplers = samplers;
            res.output_count = HRZ_ARRAY_COUNT(outputs);
            res.outputs = outputs;
            res.initial_state.color_blend.enable = true;
            res.initial_state.color_blend.color.src = my::ColorBlendState::SrcAlpha;
            res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
            res.initial_state.depth.test = false;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.stencil.enable = false;

            _shader = rc->alloc(&res, hrz::monitoring::systems::LoadingScreen);
        }
    }

    void destroy(my::ResourceContext* rc)
    {
        rc->dealloc(_ubo);
        rc->dealloc(_vertex_buffer);
        rc->dealloc(_vertex_input);
        rc->dealloc(_texture);
        rc->dealloc(_sampler);
    }

    void start_fadeout()
    {
        _fadeout = true;
        _num_frames_skipped = 0;
    }

    bool execute(
        my::RenderContext* render,
        uint32_t vp_width,
        uint32_t vp_height,
        uint32_t num_shaders_total,
        uint32_t num_shaders_ready)
    {
        {
            my::Rect vp = {0, 0, vp_width, vp_height};
            render->set_framebuffer(my::ResourceHandle::null(), {vp, vp});
        }

        if (_fadeout && _num_frames_skipped <= NumFramesToSkipBeforeFadeout)
        {
            // We need to skip the few first frames because they take more than the total fadeout
            // animation time on some platforms.

            _fadeout_start_ms = hrz::now_frame_ms();
            _num_frames_skipped++;
        }

        double elapsed = hrz::now_frame_ms() - _fadeout_start_ms;

        LoadingScreenUniformData uniform_data;
        uniform_data.num_shaders_total = num_shaders_total;
        uniform_data.num_shaders_ready = std::min(num_shaders_total, num_shaders_ready);
        uniform_data.viewport_width = vp_width;
        uniform_data.viewport_height = vp_height;
        uniform_data.fadeout = _fadeout ? std::pow(1.0 - elapsed / FadeoutDurationMs, 2) : 1;

        const my::UboBinding ubo_binding{0, _ubo, 0, sizeof(SceneViewportUniformData)};
        const my::TextureBinding texture_binding{0, _texture, _sampler};
        const auto batch_info = my::DrawBatchInfo(my::PrimitiveType::TriangleStrip, 4);

        // Loading bar
        uniform_data.draw_logo = false;
        render->update_buffer(_ubo, 0, sizeof(LoadingScreenUniformData), &uniform_data);
        render->draw(batch_info, _shader, _vertex_input, 1, &ubo_binding, 1, &texture_binding);

        // Logo
        uniform_data.draw_logo = true;
        render->update_buffer(_ubo, 0, sizeof(LoadingScreenUniformData), &uniform_data);
        render->draw(batch_info, _shader, _vertex_input, 1, &ubo_binding, 1, &texture_binding);

        return _fadeout && elapsed <= FadeoutDurationMs;
    }
};

class Core
{
    hrz::ClientMessageQueue* _message_queue;
    hrz::JobScheduler* _job_scheduler;
    hrz::AssetsLoader* _assets_loader;
    hrz::BlobAllocator* _blob_allocator;
    hrz::ActorRunner* _actor_runner;
    hrz::PlatformContext* _platform;
    hrz::ImageDecoder* _image_decoder;
    hrz::FontRasterizer* _font_rasterizer;
    hrz::MapboxTranslationSystem* _mapbox_translation;
    hrz::DevUi* _dev_ui;
    hrz::Scene* _scene;
    hrz::Monitoring _monitoring;
    hrz::RemoteMonitoring* _remote_monitoring;

    std::vector<hrz::Event> _events;

    std::deque<hrz::scene::PositionPickingTicket> _pending_picking_tickets;
    std::deque<hrz::scene::AreaPickingTicket> _pending_picking_area_tickets;
    std::deque<hrz::scene::RasterDataFetchTicket> _pending_raster_data_fetch_tickets;

    std::unique_ptr<MyceliumProfilingInstance> _my;
    hrz::GpuResourceContext _gpu_rc;
    std::unique_ptr<PresentTechnique> _present_technique;
    std::unique_ptr<LoadingScreenTechnique> _loading_screen_technique;

    bool _capture_next_frame = false;
    bool _viewer_ready = false;

    hrz::GestureSystem* _gestures;
    hrz::StylusSystem* _stylus;
    MouseHoverPicking _mouse_hover_picking;

    uint32_t _viewport_width = 0;
    uint32_t _viewport_height = 0;

#if HRZ_INTERNAL_INTEGRATION
    std::unique_ptr<DesktopClientIntegration> _desktop_integration;
#endif

    hrz_proto::ViewerOptions _options;
    KeyInteractionBindings _key_interactions;
    hrz_proto::GraphicsLevel _graphics_level;
    hrz_proto::GraphicsSettings _graphics_settings;

    int64_t _frame_time_timer_us;
    int64_t _events_dur_us = 0;
    int64_t _update_dur_us = 0;
    int64_t _update_gpu_dur_us = 0;
    int64_t _draw_dur_us = 0;
    int64_t _swap_dur_us = 0;

    size_t _blob_allocator_capacity = 0;
    size_t _max_video_memory_size = 0;
#if HRZ_EMSCRIPTEN
    uint32_t _max_wasm_memory_size = 0;
#endif

    hrz::RenderRequest _last_frame_render_request;

    bool _attribution_messages_enabled = false;
    double _last_attribution_message_time_s = 0;
    std::optional<hrz::uint128> _last_attribution_hash = std::nullopt;

public:
    Core(hrz::PlatformContext* platform, const hrz_proto::ViewerOptions& options) :
        _platform(platform), _monitoring(options), _options(options), _key_interactions(options)
    {
        hrz::platform::add_key_to_capture(platform, _key_interactions.mod_key);
        hrz::platform::add_key_bypassing_focus(platform, _key_interactions.mod_key);

        for (const auto& it : _key_interactions.mappings)
        {
            hrz::platform::add_key_to_capture(platform, it.first);
        }

#if HRZ_INTERNAL_INTEGRATION
        if (_options.internal_integration())
        {
            _desktop_integration.reset(new DesktopClientIntegration());
            _desktop_integration->register_keys_to_capture(platform);
        }
#endif

        _my = std::make_unique<MyceliumProfilingInstance>();
        const auto& my_info = _my->get_info();
        HRZ_LOG_INFO("OpenGL info");
        HRZ_LOG_INFO("    Vendor     : {}", my_info.vendor);
        HRZ_LOG_INFO("    Renderer   : {}", my_info.renderer);
        HRZ_LOG_INFO("    Version    : {}", my_info.version);

        _monitoring.add_info("GL Vendor", my_info.vendor);
        _monitoring.add_info("GL Renderer", my_info.renderer);
        _monitoring.add_info("GL Version", my_info.version);

        std::string user_agent = "";
#ifdef HRZ_EMSCRIPTEN
        {
            char* ua_str = hrz_js_get_user_agent();
            user_agent = ua_str;
            std::free(ua_str);
        }
#endif

        auto platform_info =
            hrz::detect_platform(HRZ_PLATFORM_NAME, user_agent, my_info.vendor, my_info.renderer);

        HRZ_LOG_INFO("Detected platform info");
        HRZ_LOG_INFO("    OS         : {}", hrz::PlatformInfo::OsName[platform_info.os]);
        HRZ_LOG_INFO("    Runtime    : {}", hrz::PlatformInfo::RuntimeName[platform_info.runtime]);
        HRZ_LOG_INFO(
            "    GPU vendor : {} ({})", hrz::PlatformInfo::GpuVendorName[platform_info.gpu_vendor],
            hrz::PlatformInfo::GpuFormFactorName[platform_info.gpu_form_factor]);

        _monitoring.add_info("OS", hrz::PlatformInfo::OsName[platform_info.os]);
        _monitoring.add_info("Runtime", hrz::PlatformInfo::RuntimeName[platform_info.runtime]);
        _monitoring.add_info(
            "GPU vendor",
            fmt::format(
                "{} ({})", hrz::PlatformInfo::GpuVendorName[platform_info.gpu_vendor],
                hrz::PlatformInfo::GpuFormFactorName[platform_info.gpu_form_factor]));

        _max_video_memory_size = options.max_video_memory_size();
        if (_max_video_memory_size > 0)
        {
            HRZ_LOG_INFO(
                "Max video memory: {}", hrz::bytes_to_string(_max_video_memory_size).data());
        }

        _my->set_memory_limit(_max_video_memory_size);
        _my->activate_resource_allocation_reports();

        {
            my::Instance::ShadersLinkingConfig linking_config;
            linking_config.force_compile_all_initial = _options.force_shader_compilation();
            linking_config.allow_parallel_shader_compile = true;
            linking_config.allow_compile_all_when_idle = true;

            // @Workaround(008-Apple-ParallelShaderCompile)
            if (platform_info.os == hrz::PlatformInfo::IOs
                || platform_info.os == hrz::PlatformInfo::MacOS)
            {
                linking_config.allow_parallel_shader_compile = false;
                // We also don't compile when idle because it stutters heavily...
                // Instead we accept a few stutters when loading new stuff.
                linking_config.allow_compile_all_when_idle = false;
            }

            _my->configure_shaders_linking(linking_config);
        }

        _gpu_rc.rc = _my.get();
        _gpu_rc.monitoring = &_monitoring;
        _gpu_rc.default_system_for_allocs = hrz::monitoring::systems::NoSystem;

        if (platform_info.runtime == hrz::PlatformInfo::Safari)
        {
            // @Workaround(004-Safari-UniformBufferArrayLoad)
            HRZ_LOG_INFO("Enabling Safari-specific workaround for uniform buffer array indexing");
            _my->add_global_shader_define("WORKAROUND_004");
        }

        if (platform_info.os == hrz::PlatformInfo::Android)
        {
            // @Workaround(005-Android-LoadDataToStructure)
            HRZ_LOG_INFO(
                "Enabling Android-specific workaround for loading arrays from uniform buffers");
            _my->add_global_shader_define("WORKAROUND_005");
        }

        if (platform_info.os == hrz::PlatformInfo::MacOS
            || platform_info.os == hrz::PlatformInfo::IOs)
        {
            // @Workaround(007-Apple-ArithmeticPrecisionLoss)
            HRZ_LOG_INFO(
                "Enabling Apple-specific workaround for precision loss in shader arithmetic");
            _my->add_global_shader_define("WORKAROUND_007");
        }

        _graphics_level = options.graphics_level();

        // Don't forget to update the graphics level documentation when these rules change.
        if (_graphics_level == hrz_proto::GraphicsLevelAuto)
        {
            _graphics_level = hrz_proto::GraphicsLevelMedium;

            if (platform_info.os == hrz::PlatformInfo::IOs
                && platform_info.runtime != hrz::PlatformInfo::Native)
            {
                HRZ_LOG_INFO(
                    "Using low graphics because it seems we're running WebGL on iOS (low "
                    "power, suboptimal WebGL 2 implementation)");
                _graphics_level = hrz_proto::GraphicsLevelLow;
            }
            else if (
                platform_info.gpu_vendor == hrz::PlatformInfo::Intel
                && platform_info.runtime != hrz::PlatformInfo::Native)
            {
                HRZ_LOG_INFO(
                    "Using low graphics because it seems we're running WebGL on an Intel "
                    "integrated "
                    "GPU (low power, suboptimal WebGL 2 implementation)");
                _graphics_level = hrz_proto::GraphicsLevelLow;
            }
            else if (
                platform_info.gpu_form_factor == hrz::PlatformInfo::GpuFormFactor::Discrete
                && platform_info.gpu_vendor == hrz::PlatformInfo::Nvidia)
            {
                HRZ_LOG_INFO(
                    "Using high graphics because we're running on a discrete Nvidia GPU, "
                    "supposedly high-end");
                _graphics_level = hrz_proto::GraphicsLevelHigh;
            }
            else if (platform_info.gpu_form_factor == hrz::PlatformInfo::GpuFormFactor::Software)
            {
                HRZ_LOG_INFO(
                    "Using low graphics because it seems we're emulating WebGL on the CPU.");
                _graphics_level = hrz_proto::GraphicsLevelLow;
            }
        }

        _monitoring.add_info("Graphics level", hrz_proto::GraphicsLevel_Name(_graphics_level));
        HRZ_LOG_INFO(
            "The graphics level in effect is: {}", hrz_proto::GraphicsLevel_Name(_graphics_level));

        hrz::set_flag(hrz::Flag::ForceRender, options.force_render());
        hrz::set_flag(hrz::Flag::ForceFlatOverlayRender, options.force_flat_overlay_render());
        hrz::set_flag(hrz::Flag::EnableFlatOverlays, !options.disable_flat_overlays());
        hrz::set_flag(hrz::Flag::EnableTerrain, !options.disable_terrain());

        // Don't forget to update the graphics level documentation when these rules change.
        _graphics_settings.set_shadows_enabled(_graphics_level >= hrz_proto::GraphicsLevelHigh);
        _graphics_settings.set_atmosphere_enabled(
            _graphics_level >= hrz_proto::GraphicsLevelMedium);
        _graphics_settings.set_ui_elements_depth_peeling_enabled(
            _graphics_level >= hrz_proto::GraphicsLevelMedium);

        // Determine scene parameters from graphics level and available video memory.
        _graphics_settings.set_imagery_merge_group_count(3);
        _graphics_settings.set_raster_atlas_size(4096); // 15x15=255 tiles
        _graphics_settings.set_flat_overlay_cascade_count(4);
        _graphics_settings.set_flat_overlay_resolution(2048);
        _graphics_settings.set_shadows_cascade_count(4);

        uint64_t max_video_memory_size = options.max_video_memory_size() > 0
            ? options.max_video_memory_size()
            : std::numeric_limits<uint64_t>::max();

        // Don't forget to update the graphics level documentation when the memory thresholds
        // change.
        if (max_video_memory_size <= 600 * 1024 * 1024
            || _graphics_level == hrz_proto::GraphicsLevel::GraphicsLevelLow)
        {
            if (_graphics_level != hrz_proto::GraphicsLevel::GraphicsLevelLow)
            {
                HRZ_LOG_INFO(
                    "Max video memory size is 600 MiB or less, applying low graphics video "
                    "memory settings");
            }

            _graphics_settings.set_imagery_merge_group_count(2);
            _graphics_settings.set_raster_atlas_size(2080); // 8x8=64 tiles
            _graphics_settings.set_flat_overlay_cascade_count(2);
            _graphics_settings.set_flat_overlay_resolution(1536);
            _graphics_settings.set_shadows_cascade_count(3);
        }
        else if (
            max_video_memory_size <= 800 * 1024 * 1024
            || _graphics_level == hrz_proto::GraphicsLevel::GraphicsLevelMedium)
        {
            if (_graphics_level != hrz_proto::GraphicsLevel::GraphicsLevelMedium)
            {
                HRZ_LOG_INFO(
                    "Max video memory size is 800 MiB or less, applying medium graphics video "
                    "memory settings");
            }

            _graphics_settings.set_imagery_merge_group_count(2);
            _graphics_settings.set_raster_atlas_size(3120); // 12x12=144 tiles
            _graphics_settings.set_flat_overlay_cascade_count(3);
            _graphics_settings.set_flat_overlay_resolution(2048);
            _graphics_settings.set_shadows_cascade_count(3);
        }

        _graphics_settings.set_raster_atlas_texture_compression_enabled(
            max_video_memory_size <= 800 * 1024 * 1024);

        const auto& overrides = options.graphics_settings_overrides();

        // Override the final graphics settings with the overrides in the viewer options
#define HRZ_DEFINE_GRAPHICS_SETTING(PRP, NAME) \
    if (overrides.has_##PRP()) _graphics_settings.set_##PRP(overrides.PRP());

        HRZ_GRAPHICS_SETTINGS

#undef HRZ_DEFINE_GRAPHICS_SETTING

        // Ensure some boundaries for the overriden graphics settings
        _graphics_settings.set_flat_overlay_cascade_count(
            std::max(1u, _graphics_settings.flat_overlay_cascade_count()));
        _graphics_settings.set_flat_overlay_resolution(
            std::max(1u, _graphics_settings.flat_overlay_resolution()));
        _graphics_settings.set_shadows_cascade_count(
            std::max(1u, _graphics_settings.shadows_cascade_count()));
        _graphics_settings.set_imagery_merge_group_count(
            std::max(1u, _graphics_settings.imagery_merge_group_count()));
        _graphics_settings.set_raster_atlas_size(
            std::max(1u, _graphics_settings.raster_atlas_size()));

        HRZ_LOG_INFO("The graphics settings in effect are: ");

#define HRZ_DEFINE_GRAPHICS_SETTING(PRP, NAME) \
    HRZ_LOG_INFO("    " NAME ": {}", _graphics_settings.PRP());

        HRZ_GRAPHICS_SETTINGS

#undef HRZ_DEFINE_GRAPHICS_SETTING

        hrz::set_flag(hrz::Flag::EnableShadows, _graphics_settings.shadows_enabled());
        hrz::set_flag(hrz::Flag::EnableAtmosphere, _graphics_settings.atmosphere_enabled());
        hrz::set_flag(
            hrz::Flag::EnabledDepthPeelingForUiElements,
            _graphics_settings.ui_elements_depth_peeling_enabled());
        hrz::set_flag(
            hrz::Flag::EnableRasterAtlasCompression,
            _graphics_settings.raster_atlas_texture_compression_enabled());

        if (!_graphics_settings.shadows_enabled())
        {
            _my->add_global_shader_define("SHADOWS_DISABLED");
        }
        if (!_graphics_settings.atmosphere_enabled())
        {
            _my->add_global_shader_define("ATMOSPHERE_DISABLED");
        }

        HRZ_LOG_INFO("Global flags");
        hrz::iterate_flags([&](const char* name, bool value)
                           { HRZ_LOG_INFO("    [{}] {}", value ? "X" : " ", name); });

        _blob_allocator_capacity = (size_t)_options.blob_memory_pool_size();
        if (_blob_allocator_capacity == 0)
        {
            _blob_allocator_capacity = _options.use_system_allocator_for_blobs()
                ? std::numeric_limits<size_t>::max()
                : 300 * 1024 * 1024;
        }
        _blob_allocator = hrz::blobs::create_allocator(
            _blob_allocator_capacity, _options.use_system_allocator_for_blobs());

#if HRZ_EMSCRIPTEN
        _max_wasm_memory_size = options.max_wasm_memory_size();
#endif

        hrz::shaders::collect_all_shaders(&_gpu_rc);

        auto canvas_size = hrz::platform::get_current_canvas_size(_platform).value_or(lm::uvec2{});

        _image_decoder = hrz::image_decoder::create(platform_info, my_info);
        _font_rasterizer = hrz::font_rasterizer::create();
        _mapbox_translation = hrz::mapbox::create_translation_system();
        _message_queue = hrz::client_message_queue::create();
        _job_scheduler =
            hrz::job_scheduler::create(_options.worker_count(), _blob_allocator, _font_rasterizer);
#if HRZ_DESKTOP
        _assets_loader = hrz::assets_loader::create(
            _options.user_agent().c_str(), options.native_http_referrer().c_str(),
            options.native_http_cache_size());
#else
        _assets_loader = hrz::assets_loader::create();
#endif
        _gestures = hrz::gestures::create_system();
        _stylus = hrz::stylus::create_system();
        _remote_monitoring = hrz::monitoring::create_remote_monitoring();

        _scene = hrz::scene::create(
            _graphics_settings.imagery_merge_group_count(), _graphics_settings.raster_atlas_size(),
            _graphics_settings.raster_atlas_texture_compression_enabled(),
            _options.raster_provider_tile_cache_size(),
            _graphics_settings.flat_overlay_cascade_count(),
            _graphics_settings.flat_overlay_resolution(),
            _graphics_settings.shadows_cascade_count(), canvas_size,
            hrz::platform::get_current_device_pixel_ratio(_platform), platform_info, my_info,
            _assets_loader);

        hrz::scene::initialize_rendering(_scene, _my.get(), &_gpu_rc);

        _dev_ui = hrz::dev_ui::create(platform);
        hrz::dev_ui::initialize_rendering(_dev_ui, &_gpu_rc);

        _present_technique.reset(new PresentTechnique());
        _present_technique->init(_my.get(), &_gpu_rc);

        if (_options.show_loading_screen())
        {
            _loading_screen_technique.reset(new LoadingScreenTechnique());
            _loading_screen_technique->init(_my.get(), &_gpu_rc);
        }

        basist::basisu_transcoder_init();

        _actor_runner = hrz::actor_runner::create(
            _options.run_actors_on_main_thread(), hrz::scene::get_attribution_registry(_scene),
            _blob_allocator, _message_queue, _job_scheduler, hrz::scene::get_model(_scene),
            hrz::scene::get_vector_data_loader(_scene));

        _frame_time_timer_us = hrz::now_frame_us_s64();
    }

    ~Core()
    {
        bool leak_threads =
#if HRZ_EMSCRIPTEN
            true
#else
            false
#endif
            ;

        hrz::actor_runner::destroy(_actor_runner, leak_threads);
        hrz::blobs::cancel_all_pending_allocations(_blob_allocator);
        hrz::monitoring::destroy(_remote_monitoring);
        hrz::scene::destroy(
            _scene, _assets_loader, _blob_allocator, _job_scheduler, _font_rasterizer, _my.get(),
            &_gpu_rc);
        hrz::assets_loader::destroy(_assets_loader);
        hrz::job_scheduler::destroy(
            _job_scheduler, leak_threads

        );
        hrz::image_decoder::destroy(_image_decoder);
        hrz::font_rasterizer::destroy(_font_rasterizer);
        hrz::mapbox::destroy_translation_system(_mapbox_translation);
        hrz::client_message_queue::destroy(_message_queue);
        hrz::dev_ui::destroy(_dev_ui, &_gpu_rc);
        hrz::gestures::destroy_system(_gestures);
        hrz::stylus::destroy_system(_stylus);
        hrz::blobs::destroy_allocator(_blob_allocator);

        hrz::render::profiling::clear(_my.get());

        _present_technique->destroy(_my.get());
        _my.reset();
    }

    inline hrz::ClientMessageQueue* message_queue() { return _message_queue; }

    inline hrz::ActorRunner* actor_runner() { return _actor_runner; }

    inline hrz::Scene* scene() { return _scene; }

    inline hrz::AssetsLoader* assets_loader() { return _assets_loader; }

    inline hrz::BlobAllocator* blob_allocator() { return _blob_allocator; }

    inline hrz::MapboxTranslationSystem* mapbox_translation() { return _mapbox_translation; }

    inline hrz::RemoteMonitoring* remote_monitoring() { return _remote_monitoring; }

    void fill_in_viewer_configuration_message(hrz_proto::ViewerConfiguration& config) const
    {
        config.set_graphics_profile(_graphics_level);
        config.mutable_graphics_settings()->CopyFrom(_graphics_settings);

        config.set_blob_memory_pool_size(_blob_allocator_capacity);
        config.set_max_video_memory_size(_max_video_memory_size);

#ifdef HRZ_EMSCRIPTEN
        config.set_max_wasm_memory_size(_max_wasm_memory_size);
#endif
    }

    void enqueue_events(const hrz_proto::EventsStream& events)
    {
        float device_pixel_ratio = hrz::platform::get_current_device_pixel_ratio(_platform);
        for (const auto& event : events.events())
        {
            _events.push_back(
                hrz::Event::make_platform(hrz::platform::from_proto(event, device_pixel_ratio)));
        }
    }

    bool events()
    {
        HRZ_SCOPED_SAMPLE("core events");
        hrz::platform::advance_events(_platform);

        float device_pixel_ratio = hrz::platform::get_current_device_pixel_ratio(_platform);

        hrz::platform::Event platform_event;
        while (hrz::platform::dequeue_event(_platform, &platform_event))
        {
            if (hrz::stylus::handle_platform_event(_stylus, platform_event)) continue;
            if (hrz::gestures::handle_platform_event(_gestures, platform_event)) continue;
            if (_key_interactions.handle_platform_event(
                    platform_event, _dev_ui, _scene, _remote_monitoring, _events))
                continue;

            _events.push_back(hrz::Event::make_platform(platform_event));
        }

        while (hrz::stylus::dequeue_event(_stylus, platform_event))
        {
            _events.push_back(hrz::Event::make_platform(platform_event));
        }

        hrz::gestures::Event gesture_event;
        while (hrz::gestures::dequeue_event(_gestures, gesture_event))
        {
            _events.push_back(hrz::Event::make_gesture(gesture_event));
        }

        for (const auto& event : _events)
        {
            _mouse_hover_picking.handle_event(event);

            if (hrz::dev_ui::handle_event(_dev_ui, event, device_pixel_ratio)) continue;

            if (event.kind == hrz::Event::Kind::Platform)
            {
                switch (event.platform.kind)
                {
                    case hrz::platform::Event::Kind::WindowClosed: return false;
                    case hrz::platform::Event::Kind::WindowResized:
                        _viewport_width = event.platform.window_resized.width;
                        _viewport_height = event.platform.window_resized.height;
                        break;
                    default: break;
                }
            }

#if HRZ_INTERNAL_INTEGRATION
            if (_desktop_integration)
            {
                if (_desktop_integration->handle_event(event, _scene)) continue;
                if (_desktop_integration->must_quit()) return false;
            }
#endif

            if (hrz::scene::handle_event(_scene, event, device_pixel_ratio)) continue;
        }
        _events.clear();

        hrz::gestures::remove_ended_gestures(_gestures);

        return true;
    }

    void pick_screen(
        const ::hrz_proto::PickRequest& request,
        ::hrz_proto::PickRequestResult& output_ticket)
    {
        auto included_rasters = std::vector<::hrz_proto::LayerHandle>();
        included_rasters.reserve(request.included_rasters_size());
        for (const auto& handle : request.included_rasters())
        {
            included_rasters.push_back(handle);
        }

        float device_pixel_ratio = hrz::platform::get_current_device_pixel_ratio(_platform);
        auto ticket = hrz::scene::schedule_pick(
            _scene,
            lm::ivec2(
                request.coords().x() * device_pixel_ratio,
                request.coords().y() * device_pixel_ratio),
            included_rasters);

        if (ticket.has_value())
        {
            _pending_picking_tickets.push_back(ticket.value());
            output_ticket.mutable_ticket()->set_opaque(ticket.value().ticket);
            output_ticket.set_has_a_ticket(true);
        }
        else
        {
            output_ticket.set_has_a_ticket(false);
        }
    }

    void pick_screen_area(
        const ::hrz_proto::Bboxi& input,
        ::hrz_proto::PickAreaRequestResult& output)
    {
        float device_pixel_ratio = hrz::platform::get_current_device_pixel_ratio(_platform);
        auto ticket = hrz::scene::schedule_pick(
            _scene,
            lm::ibbox2(
                lm::ivec2(input.x_min() * device_pixel_ratio, input.y_min() * device_pixel_ratio),
                lm::ivec2(input.x_max() * device_pixel_ratio, input.y_max() * device_pixel_ratio)));

        if (ticket.has_value())
        {
            _pending_picking_area_tickets.push_back(ticket.value());
            output.mutable_ticket()->set_opaque(ticket.value().ticket);
            output.set_has_a_ticket(true);
        }
        else
        {
            output.set_has_a_ticket(false);
        }
    }

    void fetch_raster_data(
        const ::hrz_proto::RasterDataFetchRequest& request,
        ::hrz_proto::RasterDataFetchResult& output)
    {
        auto raster_layers = std::vector<::hrz_proto::LayerHandle>();
        raster_layers.reserve(request.raster_layers_size());
        for (const auto& handle : request.raster_layers())
        {
            raster_layers.push_back(handle);
        }

        hrz::GeoPosition2 geo = hrz::from_proto(request.position()).latlon();

        auto ticket = hrz::scene::schedule_raster_data_fetch(_scene, geo, raster_layers);
        _pending_raster_data_fetch_tickets.push_back(ticket);

        output.mutable_ticket()->set_opaque(ticket.ticket);
        output.set_has_a_ticket(true);
    }

    void configure_mouse_hover(const hrz_proto::MouseHoverConfiguration& config)
    {
        _mouse_hover_picking.configure(config);
    }

    void update()
    {
        HRZ_SCOPED_SAMPLE("core update");

        hrz::scene::work_start_frame(
            _scene, _assets_loader, _blob_allocator, _job_scheduler, _my.get(), &_gpu_rc);

#if HRZ_INTERNAL_INTEGRATION
        if (_desktop_integration)
        {
            _desktop_integration->update(_scene);
        }
#endif

        hrz_proto::PickResults pick_results;
        while (!_pending_picking_tickets.empty())
        {
            auto ticket = _pending_picking_tickets.front();
            if (hrz::scene::retrieve_pick_results(_scene, ticket, pick_results))
            {
                _pending_picking_tickets.pop_front();

                hrz_proto::PickResultMessage message;
                message.mutable_results()->Swap(&pick_results);
                message.mutable_ticket()->set_opaque(ticket.ticket);
                hrz::client_message_queue::enqueue_pick_message(_message_queue, std::move(message));
            }
            else
            {
                break;
            }
        }

        {
            std::vector<hrz_proto::TypedObjectReference> typed_refs;
            while (!_pending_picking_area_tickets.empty())
            {
                auto ticket = _pending_picking_area_tickets.front();

                if (hrz::scene::retrieve_pick_area_result(_scene, ticket, typed_refs))
                {
                    _pending_picking_area_tickets.pop_front();

                    hrz_proto::PickAreaResultMessage message;
                    message.mutable_refs()->Reserve(typed_refs.size());
                    for (auto& typed_ref : typed_refs)
                    {
                        message.add_refs()->Swap(&typed_ref);
                    }
                    message.mutable_ticket()->set_opaque(ticket.ticket);
                    hrz::client_message_queue::enqueue_pick_area_message(
                        _message_queue, std::move(message));
                }
                else
                {
                    break;
                }
            }
        }

        std::vector<hrz_proto::PickLayerResult> raster_fetch_results;
        while (!_pending_raster_data_fetch_tickets.empty())
        {
            auto ticket = _pending_raster_data_fetch_tickets.front();
            if (hrz::scene::retrieve_raster_data_fetch_results(
                    _scene, ticket, raster_fetch_results))
            {
                _pending_raster_data_fetch_tickets.pop_front();

                hrz_proto::RasterDataFetchMessage message;
                message.mutable_ticket()->set_opaque(ticket.ticket);
                for (const auto& result : raster_fetch_results)
                {
                    *(message.mutable_results()->Add()) = result;
                }
                hrz::client_message_queue::enqueue_raster_data_fetch_message(
                    _message_queue, std::move(message));
            }
            else
            {
                break;
            }
        }

        _mouse_hover_picking.work(
            _message_queue, _scene, _last_frame_render_request,
            hrz::platform::get_current_device_pixel_ratio(_platform));

        hrz::job_scheduler::work(_job_scheduler);
        hrz::assets_loader::work(_assets_loader, _blob_allocator, _message_queue);
        hrz::scene::work(
            _scene, _assets_loader, _blob_allocator, _job_scheduler, _image_decoder,
            _font_rasterizer, _message_queue, _actor_runner);
        hrz::mapbox::work(
            _mapbox_translation, _assets_loader, _blob_allocator, _message_queue, _scene,
            _actor_runner);

        hrz::dev_ui::Context ctx;
        ctx.monitoring = &_monitoring;
        ctx.assets_loader = _assets_loader;
        ctx.blob_allocator = _blob_allocator;
        ctx.job_scheduler = _job_scheduler;
        ctx.vector_loader = hrz::scene::get_vector_data_loader(_scene);
        ctx.planet = hrz::scene::get_planet(_scene);
        ctx.debug_draw = hrz::scene::get_debug_draw(_scene);
        ctx.vector_tiles_layers = hrz::scene::get_vector_tiles_layers(_scene);
        ctx.my_instance = _my.get();
        ctx.scene = _scene;
        ctx.platform = _platform;
        ctx.remote_monitoring = _remote_monitoring;

        hrz::monitoring::work(
            _remote_monitoring, _message_queue, &_monitoring, _blob_allocator,
            hrz::scene::get_layers_info(_scene));

        hrz::actor_runner::work(
            _actor_runner, hrz::scene::get_attribution_registry(_scene), _blob_allocator,
            _message_queue, _job_scheduler, hrz::scene::get_model(_scene),
            hrz::scene::get_vector_data_loader(_scene));

        hrz::dev_ui::update(_dev_ui, &ctx);

        _monitoring.work();
    }

    void update_gpu()
    {
        HRZ_SCOPED_SAMPLE("core update gpu");

        if (_viewport_width == 0 || _viewport_height == 0) return;

        hrz::scene::work_gpu(_scene, _my.get(), &_gpu_rc, _blob_allocator);
        hrz::dev_ui::work_gpu(_dev_ui, &_gpu_rc);
    }

    void draw()
    {
        HRZ_SCOPED_SAMPLE("core draw");

        // If we tried to draw with a viewport size of zero, a few textures and framebuffers
        // would get recreated with a size of zero, which is invalid.
        // Instead we just don't draw anything, until the viewport size is greater than zero
        // again.
        if (_viewport_width == 0 || _viewport_height == 0) return;

        float device_pixel_ratio = hrz::platform::get_current_device_pixel_ratio(_platform);

        if (hrz::render::profiling::is_enabled() && _my->get_info().has_disjoint_time_query)
        {
            hrz::render::profiling::new_frame_start_point();
        }

        _last_frame_render_request = hrz::scene::get_render_request(_scene);
        hrz::scene::draw(_scene, _my.get(), &_gpu_rc);

        _present_technique->execute(
            _my.get(), _viewport_width, _viewport_height, hrz::scene::get_color_outputs(_scene));

        if (_capture_next_frame)
        {
            capture_and_return_frame();
            _capture_next_frame = false;
        }

        _my->set_framebuffer(
            my::ResourceHandle::null(),
            my::ViewportState{
                {0, 0, _viewport_width, _viewport_height},
                {0, 0, _viewport_width, _viewport_height}});

        hrz::debug_draw::draw_display(hrz::scene::get_debug_draw(_scene), _my.get());
        hrz::dev_ui::draw(_dev_ui, _my.get(), device_pixel_ratio);

        if (_loading_screen_technique
            && !_loading_screen_technique->execute(
                _my.get(), _viewport_width, _viewport_height, 1, 1))
        {
            _loading_screen_technique->destroy(&_gpu_rc);
            _loading_screen_technique.reset(nullptr);
        }

        auto render_request = hrz::scene::get_render_request(_scene);
        _monitoring.register_frame(render_request);
        if (hrz::render::profiling::is_enabled() && _my->get_info().has_disjoint_time_query)
        {
            hrz::render::profiling::set_render_requests(render_request);
        }
    }

    bool work_loading()
    {
        HRZ_SCOPED_SAMPLE("work loading");
        assert(!_viewer_ready);

        hrz::profiling::begin_frame();

        // We want to process events to catch the initial window resize event as well as
        // potential early exits of the engine.
        // We also make the monitoring system work as we want information as soon as possible
        // about the engine.
        if (!events()) return false;
        hrz::monitoring::work(
            _remote_monitoring, _message_queue, &_monitoring, _blob_allocator,
            hrz::scene::get_layers_info(_scene));

        // Draw loading screen.
        if (_loading_screen_technique)
        {
            auto info = _my->get_shaders_info();
            _loading_screen_technique->execute(
                _my.get(), _viewport_width, _viewport_height, info.to_link_initial,
                info.to_link_initial_done);
            hrz::platform::swap_window(_platform);
        }

        _my->advance_shaders_link(
            false); // Not idle! We don't want to compile all shaders if not explicitly requested
        auto info = _my->get_shaders_info();

        if (info.to_link_initial_done >= info.to_link_initial)
        {
            hrz::client_message_queue::enqueue_viewer_ready_message(_message_queue, {});

            if (_loading_screen_technique)
            {
                _loading_screen_technique->start_fadeout();
            }

            _viewer_ready = true;
            HRZ_LOG_INFO("The viewer is ready to render.");
        }
        else
        {
            hrz_proto::ViewerLoadingProgressMessage message;
            message.set_total_step_count(info.to_link_initial);
            message.set_completed_step_count(info.to_link_initial_done);
            hrz::client_message_queue::enqueue_viewer_loading_progress_message(
                _message_queue, std::move(message));
        }

        const auto& render_request = hrz::scene::get_render_request(_scene);
        hrz::profiling::end_frame(render_request.get_requested_render_types());

        return true;
    }

    bool frame()
    {
        HRZ_SCOPED_SAMPLE("core frame");

        if (!_my->begin_frame()) return true;

        hrz::Render::CurrentFrame++;

        if (!_viewer_ready)
        {
            if (!work_loading()) return false;
        }
        else
        {
            hrz::profiling::begin_frame();

            int64_t loop_dur_us = hrz::now_us_s64() - _frame_time_timer_us;

            _monitoring.register_cpu_time(
                _events_dur_us, _update_dur_us, _update_gpu_dur_us, _draw_dur_us, _swap_dur_us,
                loop_dur_us);

            int64_t frame_start_us = hrz::now_us_s64();
            hrz::profiling::begin_frame();

            int64_t events_start_us = frame_start_us;
            if (!events()) return false;
            _events_dur_us = hrz::now_us_s64() - events_start_us;

            int64_t update_start_us = hrz::now_us_s64();
            update();
            _update_dur_us = hrz::now_us_s64() - update_start_us;

            int64_t update_gpu_start_us = hrz::now_us_s64();
            update_gpu();
            _update_gpu_dur_us = hrz::now_us_s64() - update_gpu_start_us;

            int64_t draw_start_us = hrz::now_us_s64();
            draw();
            _draw_dur_us = hrz::now_us_s64() - draw_start_us;

            if (!_loading_screen_technique)
            {
                // @Todo(796) is working goes through all systems, improve this!
                bool is_idle = !hrz::scene::is_working(_scene);
                _my->advance_shaders_link(is_idle);
            }
            _my->end_frame();

            const auto& render_request = hrz::scene::get_render_request(_scene);
            hrz::profiling::end_frame(render_request.get_requested_render_types());

            int64_t swap_start_us = hrz::now_us_s64();
            hrz::platform::swap_window(_platform);
            _swap_dur_us = hrz::now_us_s64() - swap_start_us;

            _frame_time_timer_us = hrz::now_us_s64();

            do_attribution_message();
        }

        std::optional<hrz::render::profiling::GpuProfilingData> gpu_profiling_data = std::nullopt;
        if (hrz::render::profiling::is_enabled() && _my->get_info().has_disjoint_time_query)
        {
            gpu_profiling_data = hrz::render::profiling::get_frame_profile(_my.get());

            if (gpu_profiling_data.has_value())
            {
                _monitoring.register_gpu_times(std::move(gpu_profiling_data.value()));
            }
        }

        if (_my->get_info().has_gpu_memory_info)
        {
            _monitoring.register_gpu_memory_info(_my->get_memory_info());
        }

        size_t allocation_report_count = _my->get_resource_allocation_report_count();
        auto allocation_reports = _my->get_resource_allocation_reports();
        for (size_t i = 0; i < allocation_report_count; ++i)
        {
            auto& report = allocation_reports[i];

            if (report.type == my::ResourceAllocationReport::Type::Allocation)
            {
                // @Todo Set resource status to allocated.
            }

            _monitoring.register_gpu_resource_size(report.resource_handle, report.resource_size);

            if (report.type == my::ResourceAllocationReport::Type::Deallocation)
            {
                _monitoring.unregister_gpu_resource(report.resource_handle);
            }
        }
        _my->clear_resource_allocation_reports();

        return true;
    }

    void do_attribution_message()
    {
        static constexpr double kAttributionDelayS = 1;

        if (!_attribution_messages_enabled
            || hrz::now_frame_s() - _last_attribution_message_time_s < kAttributionDelayS)
            return;

        hrz::scene::enqueue_attribution_message(_scene, _message_queue, &_last_attribution_hash);
        _last_attribution_message_time_s = hrz::now_frame_s();
    }

    void capture_next_frame() { _capture_next_frame = true; }

    void capture_and_return_frame()
    {
        assert(_capture_next_frame);
        assert(_viewport_width > 0 && _viewport_height > 0);

        hrz_proto::Image image;

        auto data = std::move(_my->color_texture_download_sync(
                                     my::ResourceHandle::null(), my::Attachment::Color0,
                                     my::Rect{0, 0, _viewport_width, _viewport_height},
                                     my::TextureDownloadFormat::RGBA8Norm)
                                  .data);

        image.set_width(_viewport_width);
        image.set_height(_viewport_height);
        image.set_format(hrz_proto::ImageFormat::SRGBA_8);

        std::unique_ptr<char[]> tmp(new char[_viewport_width * 4]);

        // Flip vertically
        for (int i = 0, j = _viewport_height - 1; i < j; ++i, --j)
        {
            char* iptr = data.get() + i * _viewport_width * 4;
            char* jptr = data.get() + j * _viewport_width * 4;

            memcpy(tmp.get(), iptr, _viewport_width * 4);
            memcpy(iptr, jptr, _viewport_width * 4);
            memcpy(jptr, tmp.get(), _viewport_width * 4);
        }

        image.set_data(data.get(), _viewport_width * _viewport_height * 4);

        hrz::client_message_queue::enqueue_frame_capture_message(_message_queue, std::move(image));
    }

    bool toggle_dev_ui() { return hrz::dev_ui::toggle(_dev_ui); }

    void move_dev_ui_to_cursor() { hrz::dev_ui::move_to_cursor(_dev_ui); }

    void clear_http_cache() { hrz::assets_loader::clear_http_cache(_assets_loader); }

    bool is_viewer_ready() const { return _viewer_ready; }

    void set_user_interactions_enabled(bool value)
    {
        hrz::platform::set_user_interactions_enabled(_platform, value);
    }

    void set_attribution_enabled(bool value)
    {
        _attribution_messages_enabled = value;
        // Forces resending an attribution message as soon as it's enabled.
        _last_attribution_message_time_s = 0;
        _last_attribution_hash = std::nullopt;
    }
};

// @Todo Do we want the API to be fully re-entrant?
hrz::PlatformContext* g_platform;
std::unique_ptr<Core> core;
hrz::ThreadProfiler* g_profiler;
hrz::ThreadMetricsRegistry* g_metrics;
} // namespace

namespace
{
class ViewerServiceImpl : public hrz_proto::IViewerService
{
public:
    void get_configuration(const ::hrz_proto::Void& input, ::hrz_proto::ViewerConfiguration& output)
        override
    {
        core->fill_in_viewer_configuration_message(output);
    }

    void schedule_frame_capture(const ::hrz_proto::Void& input, ::hrz_proto::Void& output) override
    {
        core->capture_next_frame();
    }

    void pick_screen(const ::hrz_proto::PickRequest& input, ::hrz_proto::PickRequestResult& output)
        override
    {
        core->pick_screen(input, output);
    }

    void pick_screen_area(
        const ::hrz_proto::Bboxi& input,
        ::hrz_proto::PickAreaRequestResult& output) override
    {
        core->pick_screen_area(input, output);
    }

    void fetch_raster_data(
        const ::hrz_proto::RasterDataFetchRequest& input,
        ::hrz_proto::RasterDataFetchResult& output) override
    {
        core->fetch_raster_data(input, output);
    }

    void configure_mouse_hover(
        const ::hrz_proto::MouseHoverConfiguration& input,
        ::hrz_proto::Void& output) override
    {
        core->configure_mouse_hover(input);
    }

    void select_features(
        const ::hrz_proto::FeatureReferences& input,
        ::hrz_proto::UInt64Value& output) override
    {
        hrz::flat_hash_map<uint64_t, std::vector<hrz::vector_data::FeatureIdHash>> hashes;
        for (const auto& ref : input.features())
        {
            if (ref.has_feature_id())
            {
                hashes[ref.layer().opaque()].push_back(
                    hrz::vector_data::FeatureId::from_proto(ref.feature_id()).hash());
            }
        }

        size_t count = hrz::scene::select(core->scene(), 0, {nullptr, 0});
        for (const auto& p : hashes)
        {
            count = hrz::scene::select(core->scene(), p.first, p.second);
        }

        output.set_value(count);
    }

    void deselect_features(
        const ::hrz_proto::FeatureReferences& input,
        ::hrz_proto::UInt64Value& output) override
    {
        hrz::flat_hash_map<uint64_t, std::vector<hrz::vector_data::FeatureIdHash>> hashes;
        for (const auto& ref : input.features())
        {
            hashes[ref.layer().opaque()].push_back(
                hrz::vector_data::FeatureId::from_proto(ref.feature_id()).hash());
        }

        size_t count = hrz::scene::deselect(core->scene(), 0, {nullptr, 0});
        for (const auto& p : hashes)
        {
            count = hrz::scene::deselect(core->scene(), p.first, p.second);
        }

        output.set_value(count);
    }

    void deselect_all_features(const ::hrz_proto::Void& input, ::hrz_proto::Void& output) override
    {
        hrz::scene::deselect_all(core->scene());
    }

    void is_working(const ::HrzProtocol::Void& input, ::HrzProtocol::BoolValue& output) override
    {
        bool is_working = !core->is_viewer_ready() || hrz::scene::is_working(core->scene())
            || hrz::actor_runner::is_working(core->actor_runner())
            || hrz::mapbox::is_working(core->mapbox_translation());
        output.set_value(is_working);
    }

    void set_user_interactions_enabled(const ::HrzProtocol::BoolValue& input, ::HrzProtocol::Void&)
        override
    {
        core->set_user_interactions_enabled(input.value());
    }

    void send_events(const ::HrzProtocol::EventsStream& input, ::HrzProtocol::Void& output) override
    {
        core->enqueue_events(input);
    }

    void toggle_dev_ui(const ::HrzProtocol::Void& input, ::HrzProtocol::BoolValue& output) override
    {
        output.set_value(core->toggle_dev_ui());
    }

    void move_dev_ui_to_cursor(const ::HrzProtocol::Void& input, ::HrzProtocol::Void& output)
        override
    {
        core->move_dev_ui_to_cursor();
    }

    void clear_http_cache(const ::HrzProtocol::Void&, ::HrzProtocol::Void&) override
    {
        core->clear_http_cache();
    }

    void set_attribution_enabled(const ::HrzProtocol::BoolValue& input, ::HrzProtocol::Void&)
        override
    {
        core->set_attribution_enabled(input.value());
    }
};

class CameraServiceImpl : public hrz_proto::ICameraService
{
public:
    void set_fixed_target(
        const ::hrz_proto::FixedTargetCameraTransition& input,
        ::hrz_proto::Void& output) override
    {
        hrz::scene::get_camera_service(core->scene())->set_fixed_target(input, output);
    }

    void set_fixed_position(
        const ::hrz_proto::FixedPositionCameraTransition& input,
        ::hrz_proto::Void& output) override
    {
        hrz::scene::get_camera_service(core->scene())->set_fixed_position(input, output);
    }

    void set_orbit(const ::hrz_proto::OrbitCameraTransition& input, ::hrz_proto::Void& output)
        override
    {
        hrz::scene::get_camera_service(core->scene())->set_orbit(input, output);
    }

    void get_camera_pose(const ::hrz_proto::CameraIndexReference& input, ::hrz_proto::Pose& output)
        override
    {
        hrz::scene::get_camera_service(core->scene())->get_camera_pose(input, output);
    }

    void get_camera_angular_viewpoint(
        const ::hrz_proto::CameraIndexReference& input,
        ::hrz_proto::AngularViewpoint& output) override
    {
        hrz::scene::get_camera_service(core->scene())->get_camera_angular_viewpoint(input, output);
    }

    void get_camera_positional_viewpoint(
        const ::hrz_proto::CameraIndexReference& input,
        ::hrz_proto::PositionalViewpoint& output) override
    {
        hrz::scene::get_camera_service(core->scene())
            ->get_camera_positional_viewpoint(input, output);
    }

    void get_scene_view_view_box(
        const ::hrz_proto::SceneViewReference& input,
        ::hrz_proto::GeographicBounds& output) override
    {
        hrz::scene::get_camera_service(core->scene())->get_scene_view_view_box(input, output);
    }

    void get_scene_view_view_polygon(
        const ::hrz_proto::SceneViewReference& input,
        ::hrz_proto::GeographicViewPolygon& output) override
    {
        hrz::scene::get_camera_service(core->scene())->get_scene_view_view_polygon(input, output);
    }

    void get_camera_view_box(
        const ::hrz_proto::CameraIndexReference& input,
        ::hrz_proto::GeographicBounds& output) override
    {
        hrz::scene::get_camera_service(core->scene())->get_camera_view_box(input, output);
    }

    void get_camera_view_polygon(
        const ::hrz_proto::CameraIndexReference& input,
        ::hrz_proto::GeographicViewPolygon& output) override
    {
        hrz::scene::get_camera_service(core->scene())->get_camera_view_polygon(input, output);
    }

    void lat_lon_alt_to_pixel_coords(
        const ::hrz_proto::GeographicPositionInSceneView& input,
        ::hrz_proto::PixelPositionResult& output) override
    {
        hrz::scene::get_camera_service(core->scene())->lat_lon_alt_to_pixel_coords(input, output);
    }

    void move(const ::hrz_proto::CameraMovement& input, ::hrz_proto::Void& output) override
    {
        hrz::scene::get_camera_service(core->scene())->move(input, output);
    }

    void begin_continuous_movement(
        const ::hrz_proto::CameraMovement& input,
        ::hrz_proto::Void& output) override
    {
        hrz::scene::get_camera_service(core->scene())->begin_continuous_movement(input, output);
    }

    void end_continuous_movement(
        const ::hrz_proto::CameraMovement& input,
        ::hrz_proto::Void& output) override
    {
        hrz::scene::get_camera_service(core->scene())->end_continuous_movement(input, output);
    }

    void reset_north(const ::hrz_proto::ResetNorthParams& input, ::hrz_proto::Void& output) override
    {
        hrz::scene::get_camera_service(core->scene())->reset_north(input, output);
    }
};

class ShapeEditorServiceImpl : public hrz_proto::IShapeEditorService
{
public:
    void get_selected_shape(const ::hrz_proto::Void&, ::hrz_proto::ShapeSelection& output) override
    {
        auto selected_shape =
            hrz::editor::get_selected_shape(hrz::scene::get_shape_editor(core->scene()));
        if (selected_shape.has_value())
        {
            output.mutable_layer()->set_opaque(selected_shape.value());
        }
    }

    void select_shape(const ::hrz_proto::ShapeSelection& input, ::hrz_proto::Void&) override
    {
        std::optional<uint64_t> layer =
            input.has_layer() ? std::optional<uint64_t>(input.layer().opaque()) : std::nullopt;

        hrz::editor::select_shape(hrz::scene::get_shape_editor(core->scene()), layer);
    }

    void lock_shape_selection(const ::hrz_proto::Void&, ::hrz_proto::Void&) override
    {
        hrz::editor::lock_shape_selection(hrz::scene::get_shape_editor(core->scene()));
    }

    void unlock_shape_selection(const ::hrz_proto::Void&, ::hrz_proto::Void&) override
    {
        hrz::editor::unlock_shape_selection(hrz::scene::get_shape_editor(core->scene()));
    }

    void get_selected_control_point(
        const ::hrz_proto::Void&,
        ::hrz_proto::ControlPointSelection& output) override
    {
        auto index =
            hrz::editor::get_selected_control_point(hrz::scene::get_shape_editor(core->scene()));
        if (index.has_value())
        {
            output.mutable_control_point_index()->set_value(index.value());
        }
    }

    void select_control_point(const ::hrz_proto::ControlPointSelection& input, ::hrz_proto::Void&)
        override
    {
        std::optional<uint32_t> index = input.has_control_point_index()
            ? std::optional<uint32_t>(input.control_point_index().value())
            : std::nullopt;

        hrz::editor::select_control_point(hrz::scene::get_shape_editor(core->scene()), index);
    }

    void get_current_mode(const ::hrz_proto::Void&, ::hrz_proto::ModeSelection& output) override
    {
        output.set_mode(hrz::editor::get_current_mode(hrz::scene::get_shape_editor(core->scene())));
    }

    void select_mode(const ::hrz_proto::ModeSelection& input, ::hrz_proto::Void&) override
    {
        hrz::editor::set_mode(hrz::scene::get_shape_editor(core->scene()), input.mode());
    }

    void get_shape_information(
        const ::hrz_proto::LayerHandle& input,
        ::hrz_proto::ShapeInformation& output) override
    {
        output.CopyFrom(hrz::editor::get_shape_information(
            hrz::scene::get_shape_editor(core->scene()), input.opaque()));
    }

    void delete_selected_control_point(const ::hrz_proto::Void& input, ::hrz_proto::Void& output)
        override
    {
        hrz::editor::delete_selected_control_point(hrz::scene::get_shape_editor(core->scene()));
    }
};

class MessageQueueServiceImpl : public hrz_proto::IMessageQueueService
{
public:
    void dequeue_messages(
        const ::hrz_proto::DequeueParams& input,
        ::hrz_proto::DequeuedMessages& output) override
    {
        auto queue = core->message_queue();

        size_t message_count = 0;
        while (message_count < input.max_message_count())
        {
            auto message = hrz::client_message_queue::dequeue_message(queue);
            if (!message.has_value())
            {
                break;
            }

            output.mutable_messages()->Add(std::move(message.value()));
            message_count++;
        }

        output.mutable_queue_size()->set_message_count(
            hrz::client_message_queue::get_queue_size(queue));
    }

    void get_queue_size(const ::HrzProtocol::Void&, ::HrzProtocol::QueueSize& output) override
    {
        unsigned int message_count =
            hrz::client_message_queue::get_queue_size(core->message_queue());
        output.set_message_count(message_count);
    }
};

class ClientDataServiceImpl : public hrz_proto::IClientDataService
{
public:
    void provide_vector_data(
        const ::HrzProtocol::VectorDataRequestResponse& input,
        ::HrzProtocol::Void&) override
    {
        hrz::scene::provide_client_vector_data(core->scene(), input);
    }

    void invalidate_vector_data(
        const ::HrzProtocol::VectorDataInvalidation& input,
        ::HrzProtocol::Void&) override
    {
        hrz::scene::invalidate_client_vector_data(core->scene(), input);
    }

    void provide_asset_data(const ::HrzProtocol::AssetRequestResponse& input, ::HrzProtocol::Void&)
        override
    {
        hrz::assets_loader::provide_client_asset_data(
            core->assets_loader(), core->blob_allocator(), input);
    }
};

class LayerServiceImpl : public hrz_proto::ILayerService
{
public:
    void create_layer(
        const ::hrz_proto::LayerCreateInfo& input,
        ::hrz_proto::LayerHandle& layer_handle) override
    {
        auto layer_id = hrz::scene::create_layer(
            core->scene(), input.type(), input.name(), core->actor_runner());
        layer_handle.set_opaque(layer_id);
    }

    void destroy_layer(const ::hrz_proto::LayerHandle& input, ::hrz_proto::Void& output) override
    {
        auto scene = core->scene();
        hrz::scene::destroy_layer(scene, input.opaque());
    }

    void rename_layer(const ::hrz_proto::LayerRenameInfo& input, ::hrz_proto::Void& output) override
    {
        auto scene = core->scene();
        hrz::scene::rename_layer(scene, input.handle().opaque(), input.name());
    }

    void get_layer(const ::hrz_proto::LayerHandle& input, ::hrz_proto::Layer& output) override
    {
        auto scene = core->scene();
        hrz::scene::retrieve_layer_info(scene, input.opaque(), output);
    }

    void get_all_layers(const ::hrz_proto::Void& _, ::hrz_proto::LayerArray& output) override
    {
        auto scene = core->scene();
        hrz::scene::retrieve_all_layers(scene, output);
    }
};

class SceneModelServiceImpl : public hrz_proto::ISceneModelService
{
public:
    void get(const ::hrz_proto::Path& path, ::hrz_proto::SceneModelGet& output) override
    {
        output.set_payload(hrz::scene::get_model(core->scene(), path));
    }

    void set(const ::hrz_proto::SceneModelSet& input, ::hrz_proto::Void& output) override
    {
        hrz::scene::set_model(core->scene(), input.path(), input.payload());
    }

    void add(const ::hrz_proto::SceneModelSet& input, ::hrz_proto::SceneModelArrayCount& output)
        override
    {
        output.set_count(hrz::scene::add_model(core->scene(), input.path(), input.payload()));
    }

    void remove(const ::hrz_proto::Path& path, ::hrz_proto::SceneModelArrayCount& output) override
    {
        output.set_count(hrz::scene::remove_model(core->scene(), path));
    }

    void count(const ::hrz_proto::Path& path, ::hrz_proto::SceneModelArrayCount& output) override
    {
        output.set_count(hrz::scene::count_model(core->scene(), path));
    }
};

class SceneDumpServiceImpl : public hrz_proto::ISceneDumpService
{
public:
    void dump_scene(const ::HrzProtocol::SceneDumpRequest& input, ::HrzProtocol::SceneDump& output)
        override
    {
        hrz::scene::dump_scene(core->scene(), input, output);
    }

    void load_dump(const ::HrzProtocol::SceneLoadRequest& input, ::HrzProtocol::LayerArray& output)
        override
    {
        hrz::scene::load_scene_dump(core->scene(), input, output, core->actor_runner());
    }
};

class MonitoringServiceImpl : public hrz_proto::IMonitoringService
{
public:
    void set_monitoring_server_address(
        const ::HrzProtocol::StringValue& input,
        ::HrzProtocol::Void& output) override
    {
        hrz::monitoring::set_monitoring_server_address(core->remote_monitoring(), input.value());
    }

    void connect_to_monitoring_server(
        const ::HrzProtocol::BoolValue& input,
        ::HrzProtocol::Void& output) override
    {
        if (input.value())
        {
            hrz::monitoring::try_connect(core->remote_monitoring());
        }
        else
        {
            hrz::monitoring::disconnect(core->remote_monitoring());
        }
    }

    void enable_monitoring_messages(
        const ::HrzProtocol::BoolValue& input,
        ::HrzProtocol::Void& output) override
    {
        hrz::monitoring::set_message_queue_sending_enabled(
            core->remote_monitoring(), input.value());
    }

    void enable_profiling(const ::HrzProtocol::BoolValue& input, ::HrzProtocol::Void& output)
        override
    {
        hrz::profiling::set_profiling_enabled(input.value());
    }

    void enable_metrics(const ::HrzProtocol::BoolValue& input, ::HrzProtocol::Void& output) override
    {
        hrz::metrics::set_metrics_registries_enabled(input.value());
    }

    void make_gpu_resources_snapshot(const ::HrzProtocol::Void& input, ::HrzProtocol::Void& output)
        override
    {
        hrz::monitoring::schedule_gpu_snapshot(core->remote_monitoring());
    }

    void make_blob_allocator_snapshot(const ::HrzProtocol::Void& input, ::HrzProtocol::Void& output)
        override
    {
        hrz::monitoring::schedule_blob_allocator_snapshot(core->remote_monitoring());
    }
};

class MapboxServiceImpl : public hrz_proto::IMapboxService
{
    void translate_scene(
        const ::HrzProtocol::MapboxTranslationParams& input,
        ::HrzProtocol::MapboxTranslationTicket& output) override
    {
        auto ticket = hrz::mapbox::begin_translation(
            core->mapbox_translation(), core->assets_loader(), core->message_queue(), input);
        output.set_opaque(ticket);
    }
};

} // namespace

extern "C" unsigned int hrz_init(
    void* wsi_instance,
    void* wsi_window,
    const char* args_data,
    int args_data_size,
    const char* canvas_selector)
{
    hrz::set_epoch();

    hrz_proto::ViewerOptions options;
    options.ParseFromArray(args_data, args_data_size);

    hrz::log::set_log_filter_level((hrz::log::Severity)options.log_filter_level());
    my::set_log_filter_level((my::LogSeverity)options.log_filter_level());
    my::set_log_callback(my_log_adapter);

    hrz::profiling::init_main_thread();
    hrz::metrics::init_main_thread();

    g_profiler = hrz::profiling::create_thread_profiler("Main thread");
    g_metrics = hrz::metrics::create_thread_registry(false);

    HRZ_LOG_INFO("Welcome to Horizon!");
    HRZ_LOG_INFO("Version {}", hrz::Version);

    HRZ_LOG_INFO("Options:");
    HRZ_LOG_INFO("    Worker count: {}", options.worker_count());
    HRZ_LOG_INFO("    Log filter level: {}", options.log_filter_level());
#if HRZ_DESKTOP
    HRZ_LOG_INFO("    User agent: {}", options.user_agent());
    HRZ_LOG_INFO("    HTTP cache size: {}", options.native_http_cache_size());
    HRZ_LOG_INFO("    HTTP referrer: {}", options.native_http_referrer().c_str());
#endif
    HRZ_LOG_INFO("    Pixel ratio override: {}", options.device_pixel_ratio());
    HRZ_LOG_INFO("    Graphics level: {}", hrz_proto::GraphicsLevel_Name(options.graphics_level()));

    HRZ_LOG_INFO("    Graphics settings overrides:");
    const auto& overrides = options.graphics_settings_overrides();

#define HRZ_DEFINE_GRAPHICS_SETTING(PRP, NAME) \
    if (overrides.has_##PRP()) HRZ_LOG_INFO("        " NAME ": {}", overrides.PRP());

    HRZ_GRAPHICS_SETTINGS

#undef HRZ_DEFINE_GRAPHICS_SETTING

    HRZ_LOG_INFO("    Force render: {}", options.force_render());
    HRZ_LOG_INFO("    Force flat overlay render: {}", options.force_flat_overlay_render());
    HRZ_LOG_INFO("    Disable events capture: {}", options.disable_events_capture());
    HRZ_LOG_INFO("    Internal integration: {}", options.internal_integration());
    HRZ_LOG_INFO(
        "    Raster provider default tile cache: {}", options.raster_provider_tile_cache_size());
    HRZ_LOG_INFO("    Blob memory pool size: {}", options.blob_memory_pool_size());
#if HRZ_EMSCRIPTEN
    HRZ_LOG_INFO("    Max WASM memory size: {}", options.max_wasm_memory_size());
#endif
    HRZ_LOG_INFO(
        "    Use system allocator for blobs: {}", options.use_system_allocator_for_blobs());
    HRZ_LOG_INFO("    Max video memory size: {}", options.max_video_memory_size());

    {
        double start = hrz::now_ms();
        hrz_proj::init_global_common_transforms();
        double end = hrz::now_ms();
        HRZ_LOG_INFO("Global transforms initialized in {:.2} ms", end - start);
    }

    {
        double start = hrz::now_ms();
        hrz_shaders::decompress_shaders();
        double end = hrz::now_ms();
        HRZ_LOG_INFO("Shaders decompressed in {:.2} ms", end - start);
    }

    g_platform = hrz::platform::initialize(
        options.disable_events_capture(), wsi_instance, wsi_window, canvas_selector,
        options.device_pixel_ratio());
    auto gl_init_status = hrz::platform::initialize_gl_ctx(g_platform);
    if (gl_init_status != hrz_proto::ViewerInitStatus::INIT_SUCCESS)
    {
        HRZ_LOG_ERROR("Couldn't initialize platform OpenGL context");
        return (unsigned int)gl_init_status;
    }

    hrz::platform::make_gl_ctx_current(g_platform);

    HRZ_LOG_INFO("Platform initialized");

    if (!my::Instance::init(hrz::platform::get_gl_load_fn()))
    {
        HRZ_LOG_ERROR("Couldn't initialize Mycelium");
        return (unsigned int)hrz_proto::ViewerInitStatus::GRAPHICS_BACKEND_INIT_ERROR;
    }

    HRZ_LOG_INFO("Graphics backend initialized");

    double core_init_start_ms = hrz::now_ms();
    core.reset(new Core(g_platform, options));
    double core_init_dur_ms = hrz::now_ms() - core_init_start_ms;

    HRZ_LOG_INFO("Core initialized in {:.2f} ms", core_init_dur_ms);

    hrz::RpcImplementations impls;
    impls.viewer_service = std::make_unique<ViewerServiceImpl>();
    impls.message_queue_service = std::make_unique<MessageQueueServiceImpl>();
    impls.layer_service = std::make_unique<LayerServiceImpl>();
    impls.scene_model_service = std::make_unique<SceneModelServiceImpl>();
    impls.client_data_service = std::make_unique<ClientDataServiceImpl>();
    impls.camera_service = std::make_unique<CameraServiceImpl>();
    impls.shape_editor_service = std::make_unique<ShapeEditorServiceImpl>();
    impls.scene_dump_service = std::make_unique<SceneDumpServiceImpl>();
    impls.monitoring_service = std::make_unique<MonitoringServiceImpl>();
    impls.mapbox_service = std::make_unique<MapboxServiceImpl>();
    hrz::set_rpc_dispatcher_implementations(std::move(impls));

    HRZ_LOG_INFO("API RPC initialized");

    return (unsigned int)hrz_proto::ViewerInitStatus::INIT_SUCCESS;
}

extern "C" uint32_t hrz_frame(void)
{
    HRZ_SCOPED_SAMPLE_ROOT("hrz frame");
    hrz::set_frame_time();
    bool should_continue = core->frame();
    hrz::metrics::finish_thread_registry_frame();
    hrz::metrics::synchronize_thread_registry();
    hrz::profiling::synchronize_thread_profiler();
    return should_continue;
}

extern "C" void hrz_cleanup(void)
{
    core.reset(nullptr);
    hrz::profiling::destroy_thread_profiler(g_profiler);
    hrz::metrics::destroy_thread_registry(g_metrics);
    hrz::profiling::cleanup_main_thread();
    hrz::metrics::cleanup_main_thread();
    hrz::platform::cleanup(g_platform);
}
