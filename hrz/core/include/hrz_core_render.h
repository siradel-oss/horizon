#pragma once

#include "camera/hrz_core_camera_types.h"
#include "hrz_core_render_request.h"
#include "monitoring/hrz_core_monitoring_gpu.h"

#include <hrz_common_blob_allocator.h>
#include <hrz_common_metadata.h>
#include <hrz_common_monitoring_defs.h>
#include <hrz_common_shader_defines.h>
#include <hrz_fnd_array_view.h>
#include <hrz_fnd_bitset.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_inlined_vector.h>
#include <hrz_fnd_maths.h>
#include <hrz_fnd_meta.h>
#include <hrz_protocol_all.h>

#include <lin_maths.h>
#include <mycelium.h>

#include <optional>
#include <utility>

#define HRZ_CHECK_UBO_SIZE(T) \
    static_assert(sizeof(T) % 16 == 0, "UBO " #T " should be a multiple of 16 bytes")

#define HRZ_UBO_STRUCT_FIELD(T) alignas(16) T

namespace hrz
{
static constexpr size_t SCENE_VIEW_COUNT = (size_t)hrz_proto::SceneViewIndex_ARRAYSIZE;
static constexpr size_t CAMERA_COUNT = (size_t)hrz_proto::CameraIndex_ARRAYSIZE;

using SceneViewBitset = Bitset32<SCENE_VIEW_COUNT>;

struct GpuResourceContext : public my::ResourceContext
{
public:
    my::ResourceHandle alloc(const my::Resource* res) override;
    my::ResourceHandle alloc(
        const my::Resource* res,
        monitoring::systems::Name system,
        uint64_t layer_id = 0,
        std::initializer_list<std::pair<MetadataString, MetadataString>> metadata = {});
    my::ResourceHandle alloc(
        const my::Resource* res,
        const monitoring::ResourceOwner& resource_owner,
        std::initializer_list<std::pair<MetadataString, MetadataString>> metadata = {});
    my::ResourceHandle alloc(
        const my::Resource* res,
        const monitoring::ResourceOwner& resource_owner,
        gsl::span<std::pair<MetadataString, MetadataString>> metadata);

    void dealloc(my::ResourceHandle handle) override { rc->dealloc(handle); }

    void realloc_buffer(my::ResourceHandle handle, const my::BufferResource* res) override
    {
        rc->realloc_buffer(handle, res);
    }

    void update_texture_layout(my::ResourceHandle handle, const my::TextureLayout& layout) override
    {
        rc->update_texture_layout(handle, layout);
    }

    void update_renderbuffer_size(my::ResourceHandle handle, uint32_t width, uint32_t height)
        override
    {
        rc->update_renderbuffer_size(handle, width, height);
    }

    my::ResourceHandle retrieve_shader(const char* name) const override
    {
        return rc->retrieve_shader(name);
    }

    my::ResourceContext* rc;
    monitoring::GpuResourceMonitoring* monitoring;

    monitoring::systems::Name default_system_for_allocs = monitoring::systems::NoSystem;

private:
    void register_texture_metadata(const my::Resource* res, my::ResourceHandle handle);
};

// Contains all structs necessary for rendering that are not view-dependent.
struct Render
{
    my::Instance* my;
    GpuResourceContext* rc;
    my::Renderer* rd;
    my::ResourceBinder* rb;
    my::Renderer::ViewMask main_views;

    inline static uint64_t CurrentFrame = 0;
};

struct RenderView : public Render
{
    my::RenderGraph* rg;

    RenderView(const Render& r, my::RenderGraph* rg) : Render(r), rg(rg) {}
};

struct SceneViewRenderGraphUserData
{
    my::Renderer::ViewId main_view;
    hrz_proto::SceneViewIndex scene_view;
};

enum CommonUbos
{
    UboFrame = 0,
    UboView,
    UboCustomStart,
};

enum CommonSamplers
{
    SamplerCameraHeight = 0,
    SamplerSunColor,
    SamplerSunShadow0,
    SamplerViewshedShadow0 = SamplerSunShadow0 + HRZ_S_MAX_SUN_CASCADES,
    SamplerCustomStart = SamplerViewshedShadow0 + HRZ_S_VIEWSHED_CNT,
};

using bool32 = uint32_t;

struct ClipPlaneUniformData
{
    lm::mat4 matrix;
    lm::vec3 normal;
    float outline_distance;
    lm::vec4 outline_color;
};

HRZ_CHECK_UBO_SIZE(ClipPlaneUniformData);

struct FrameUniformData
{
    enum DebugFlags : uint32_t
    {
        DrawFlatOverlayCascades = 0x00000001,
        DrawHeatmapOobSampling = 0x00000002,
    };

    enum AtmosphereFlags : uint32_t
    {
        DynamicSunLighting = 0x00000001,
        DynamicAmbientLighting = 0x00000002,
    };

    lm::mat4 proj;
    lm::mat4 view;
    lm::mat4 view_cc;
    lm::mat4 pv;
    lm::mat4 pv_cc;
    lm::mat4 view_cc_inv;
    lm::mat4 proj_inv;
    lm::vec4 view_pos_low;
    lm::vec4 view_pos_high;

    lm::vec3 view_sun_direction;
    float view_elevation;

    lm::uvec2 viewport_size;
    float pixel_size_in_meters; // at a distance of 1 meter from the camera
    float device_pixel_ratio;

    lm::mat4 sun_matrix[HRZ_S_MAX_SUN_CASCADES];
    lm::mat4 env_sh[3];
    lm::mat4 vs_pv_matrix[HRZ_S_VIEWSHED_CNT];
    lm::vec4 vs_position_from_main_view[HRZ_S_VIEWSHED_CNT];
    lm::vec4 vs_seen_color[HRZ_S_VIEWSHED_CNT];
    lm::vec4 vs_hidden_color[HRZ_S_VIEWSHED_CNT];
    HRZ_UBO_STRUCT_FIELD(ClipPlaneUniformData) clip_planes[HRZ_S_MAX_CLIP_PLANES];

    bool32 viewsheds_enabled;
    bool32 lighting_enabled;
    bool32 receive_shadows;
    uint32_t atmosphere_flags;

    float shadow_map_far_lin;
    int32_t terrain_clip_id;
    bool32 terrain_lighting_enabled;
    bool32 terrain_receive_shadows;

    lm::vec4 terrain_color_opacity;

    lm::uvec3 quick_highlight_picking_id{0, 0, 0};
    uint32_t merge_groups_bitset;

    lm::vec4 quick_highlight_color;

    lm::vec3 sky_color_linear;
    float sun_strength;

    lm::vec3 underground_color_linear;
    float ambient_strength;

    lm::vec3 sun_color_linear;
    float wrap_lighting;

    uint32_t first_imagery_group;
    uint32_t last_imagery_group;
    uint32_t shadow_map_cascade_count;
    float time;

    float atmosphere_fade_start;
    float atmosphere_fade_end;
    uint32_t debug_flags{0};
    float view_latitude;

    lm::vec2 pixel_size_in_clip;
    float camera_height_to_perceived_distance;
    uint32_t _padding2[1];
};

static_assert(sizeof(FrameUniformData) <= 16384, "Frame UBO is too large!");
HRZ_CHECK_UBO_SIZE(FrameUniformData);

struct AuxViewUniformData
{
    // This is the proj * view matrix starting after the view transform
    // of the main view.
    lm::mat4 pv_from_main_view;
    lm::mat4 pv_cc;
};

// Type of rendering. For instance "Visual" is the normal mode.
// This is used to switch shader in renderable objects.
enum RenderType
{
    RenderVisual,
    RenderPicking,
    RenderPlanetFeedback,
    RenderDepth,
    RenderDecal,
    RenderShadows,
    RenderViewshed,
    RenderSelection,
};

// Bin numbers determine the render order, inside a render pass.
enum RenderBinBit
{
    RenderWorldTransparentBinBit,
    RenderWorldOpaqueBinBit,
    RenderDecalBinBit,
    RenderSymbolicBinBit,
    RenderSymbolicOverlayBinBit,
    RenderPlanetBinBit,
    RenderHeatmapBinBit,
    RenderFlatOverlayBinBit,
    RenderInWorldUiBinBit,
    RenderUiBinBit,
};

// An object can belong to multiple bins, but they may be drawn multiple times.
// A render pass selects the objects it will render based on its bin.
enum RenderBin
{
    RenderWorldOpaqueBin = 1ull << RenderWorldOpaqueBinBit,
    RenderWorldTransparentBin = 1ull << RenderWorldTransparentBinBit,
    RenderDecalBin = 1ull << RenderDecalBinBit,
    RenderSymbolicBin = 1ull << RenderSymbolicBinBit,
    RenderSymbolicOverlayBin = 1ull << RenderSymbolicOverlayBinBit,
    RenderPlanetBin = 1ull << RenderPlanetBinBit,
    RenderHeatmapBin = 1ull << RenderHeatmapBinBit,
    RenderFlatOverlayBin = 1ull << RenderFlatOverlayBinBit,
    RenderInWorldBin = 1ull << RenderInWorldUiBinBit,
    RenderUiBin = 1ull << RenderUiBinBit,

    RenderAllWorldBins = RenderWorldOpaqueBin | RenderWorldTransparentBin | RenderPlanetBin,
    RenderAllPhysicalBins = RenderAllWorldBins | RenderDecalBin | RenderSymbolicBin
        | RenderSymbolicOverlayBin | RenderInWorldBin | RenderUiBin,
};

struct RenderViewInfo
{
    hrz_proto::SceneViewIndex view;
    CameraViewInfo cam_view_info;
    my::Renderer::ViewId view_main;
    my::Renderer::ViewMask all_views;
    double height_above_terrain;
    double perceived_distance;
};

namespace render
{
uint64_t acquire_texture_download_id();
void release_texture_download_id(uint64_t id);

void initialize_ui_blending_params(my::ColorBlendState*);

// Compute a factor to convert a size in device pixels into a size in meters
// necessary for the object at the given position to be viewed as the wanted size in pixels.
double compute_device_pixels_to_meters(
    const lm::dvec3& ecef_pos,
    const hrz::CameraViewInfo& view_info);

// Compute a factor to convert a size in logical pixels into a size in meters
// necessary for the object at the given position to be viewed as the wanted size in pixels.
double compute_logical_pixels_to_meters(
    const lm::dvec3& ecef_pos,
    const hrz::CameraViewInfo& view_info);

// Compute a factor to convert a size in logical pixels to a size in meters for an object at a
// distance of 1.
double compute_logical_pixel_size_in_meters(const hrz::CameraViewInfo& view_info);

template<typename T>
constexpr size_t compute_ubo_stride(size_t ubo_alignment)
{
    HRZ_CHECK_UBO_SIZE(T);
    return ((sizeof(T) + ubo_alignment - 1) / ubo_alignment) * ubo_alignment;
}

namespace profiling
{
uint64_t acquire_time_query_id();
void release_time_query_id(uint64_t id);

void enable_profiling(bool enabled);
bool is_enabled();

void new_frame_start_point();
void new_view_start_point();
void register_frame_time_query(uint64_t id);
void set_render_requests(const RenderRequest&);

struct GpuProfilingData
{
    std::vector<std::vector<std::pair<std::string, float>>> pass_durations;
    RenderRequest render_request;
};

std::optional<GpuProfilingData> get_frame_profile(my::Instance*);

void clear(my::Instance*);
} // namespace profiling

template<typename T>
inline T log_depth(T depth)
{
    return std::log(depth / (T)HRZ_S_NEAR) / std::log((T)HRZ_S_FAR / (T)HRZ_S_NEAR);
}

template<typename T>
inline T undo_log_depth(T log_depth)
{
    return std::exp(log_depth * std::log((T)HRZ_S_FAR / (T)HRZ_S_NEAR)) * (T)HRZ_S_NEAR;
}

template<typename T>
inline T lin_depth(T depth)
{
    return (depth - HRZ_S_NEAR) / (HRZ_S_FAR - HRZ_S_NEAR);
}

class ScreenSpaceError
{
    double _sse_denominator;
    double _viewport_height;

public:
    ScreenSpaceError() : _sse_denominator(1), _viewport_height(0) {}

    ScreenSpaceError(double fov_y, double viewport_height, float device_pixel_ratio) :
        _sse_denominator(2.0 * std::tan(0.5 * fov_y) * device_pixel_ratio),
        _viewport_height(viewport_height)
    {
    }

    /**
     * Each tile has a "geometric error" property. This value is a length. It gives
     * the size of the smallest detail in the tile. (For example, if the tile consists
     * of just a textured plane, this is the size of a texel in world-space.)
     * Using the distance to the tile from the camera, we compute the size (in metres)
     * of a viewport pixel on a plane parallel to the camera plane, tangent to the
     * tile geometry.
     * The size of a pixel is compared to the geometric error of the tile, giving us
     * an error ratio, the screen-space error.
     * If the value is less than one, it means that one screen pixel covers more the
     * the smallest detail of the tile, or conversely, that the smallest detail spans
     * less than one pixel. The tile has therefore enough details and does not need to
     * be refined. (On the contrary, it may need to be unrefined.)
     * If the value is more than one, a screen pixel covers less than the smallest
     * detail of the tile, so the tile does not have enough details relatively to the
     * distance at which it is viewed and should be refined.
     * Because different tilesets may have opted for different ways of computing what
     * their geometry error value is, a parameter named "max screen-space error" is
     * passed along with the tileset. The screen-space error is divided by this
     * parameter before being used by the rest of the system.
     *
     * The equation for the screen-space error is:
     *              tile.geometric_error * viewport_height
     *     error = ----------------------------------------
     *               cam_tile_distance * 2 * tan(fovy / 2)
     * The value `2 * tan(fovy / 2)` is the same for every tile, and is computed in
     * advance and passed as a parameter to this function.
     */
    inline double compute_screen_space_size(double world_size, double distance) const
    {
        distance = std::max(distance, 0.0000001); // Avoid dividing by 0.
        return (world_size * _viewport_height) / (distance * _sse_denominator);
    }

    inline double compute_screen_space_error(
        double geometric_error,
        double distance,
        double max_screen_space_error) const
    {
        return compute_screen_space_size(geometric_error, distance) / max_screen_space_error;
    }

    bool operator==(const ScreenSpaceError& other) const
    {
        return _sse_denominator == other._sse_denominator
            && _viewport_height == other._viewport_height;
    }

    bool operator!=(const ScreenSpaceError& other) const { return !(*this == other); }
};

struct LightingSettings
{
    bool lighting_enabled{};
    bool cast_shadows{};
    bool receive_shadows{};

    constexpr bool operator==(const LightingSettings& other) const
    {
        return lighting_enabled == other.lighting_enabled && cast_shadows == other.cast_shadows
            && receive_shadows == other.receive_shadows;
    }

    constexpr bool operator!=(const LightingSettings& other) const { return !(*this == other); }
};

static LightingSettings from_proto(const hrz_proto::LightingSettings& proto)
{
    return {proto.enable_lighting(), proto.cast_shadows(), proto.receive_shadows()};
}

class TimedRenderPass : public my::RenderPass
{
    std::string _name;

public:
    explicit TimedRenderPass(const char* name) : _name(name) {}

    void execute(const my::RenderGraph::ExecutionContext& ctx) override final;
    virtual void execute_timed(const my::RenderGraph::ExecutionContext&) = 0;
};

// Builds a vertex input resource and its associated vertex buffer by appending blobs of data.
// The resulting layout is an SoA.
struct VertexInputBuilder
{
    struct ToUpload
    {
        const void* data;
        size_t offset;
        size_t size;
    };

    size_t full_size = 0;

    // Necessary to not move the blob data before the upload, since a pointer to the data is kept in
    // the to_upload array.
    hrz::InlinedVector<blobs::BlobData, 8> pinned_blobs;

    hrz::InlinedVector<ToUpload, 8> to_upload;
    hrz::InlinedVector<my::VertexInputStream, 8> vertex_input_streams;

    void add_input_stream_raw(
        int index,
        gsl::span<const std::byte> data,
        my::VertexFormat format,
        my::VertexRate rate);

    void add_input_stream(
        int index,
        blobs::BlobHandle blob,
        my::VertexFormat format,
        my::VertexRate rate);

    template<typename T>
    inline void add_input_stream(
        int index,
        const T& v,
        my::VertexFormat format,
        my::VertexRate rate = my::VertexRate::Constant)
    {
        add_input_stream_raw(index, hrz::as_bytes(gsl::span<const T>(&v, 1)), format, rate);
    }

    template<typename T>
    inline void add_input_stream(
        int index,
        gsl::span<const T> data,
        my::VertexFormat format,
        my::VertexRate rate)
    {
        add_input_stream_raw(index, hrz::as_bytes(data), format, rate);
    }

    template<typename T>
    void add_input_stream(
        int index,
        const std::variant<blobs::BlobHandle, T>& data,
        my::VertexFormat format,
        my::VertexRate rate)
    {
        std::visit(
            [this, index, format, rate](const auto& arg)
            {
                using ArgType = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<ArgType, blobs::BlobHandle>)
                {
                    add_input_stream(index, arg, format, rate);
                }
                else if constexpr (std::is_same_v<ArgType, T>)
                {
                    add_input_stream(index, gsl::span<const T>(&arg, 1), format, rate);
                }
                else
                {
                    static_assert(hrz::always_false<ArgType>, "Unexpected type");
                }
            },
            data);
    }

    // First is the buffer, second is the vertex input
    std::pair<my::ResourceHandle, my::ResourceHandle> build(
        hrz::Render* render,
        monitoring::systems::Name system,
        uint64_t layer_id = 0,
        std::initializer_list<std::pair<MetadataString, MetadataString>> metadata = {});
};

template<typename T>
class DoubleBufferedUniformBuffer
{
    struct Buffer
    {
        int dirty_offset_begin = std::numeric_limits<int>::max();
        int dirty_offset_end = std::numeric_limits<int>::lowest();

        my::ResourceHandle handle;

        void invalidate_span(int begin, int end)
        {
            dirty_offset_begin = std::min(dirty_offset_begin, begin);
            dirty_offset_end = std::max(dirty_offset_end, end);
        }

        void reset_dirty_span()
        {
            dirty_offset_begin = std::numeric_limits<int>::max();
            dirty_offset_end = std::numeric_limits<int>::lowest();
        }

        constexpr bool has_dirty_span() const { return dirty_offset_begin < dirty_offset_end; }
    };

    int _count;
    int _stride = sizeof(T);
    std::unique_ptr<unsigned char[]> _data_raw;
    ArrayView<T> _data_view;
    Buffer _buffers[2];

    mutable int _current_writable = 0;
    mutable uint64_t _last_flip_frame = 0;

public:
    explicit DoubleBufferedUniformBuffer(int count = 1) : _count(count) {}

    void initialize(
        int count,
        hrz::Render* render,
        hrz::monitoring::systems::Name system,
        std::initializer_list<std::pair<MetadataString, MetadataString>> metadata)
    {
        _count = count;
        initialize(render, system, metadata);
    }

    void initialize(
        hrz::Render* render,
        hrz::monitoring::systems::Name system,
        std::initializer_list<std::pair<MetadataString, MetadataString>> metadata)
    {
        static constexpr size_t Size = sizeof(T);
        static constexpr size_t Alignment = alignof(T);

        _stride = _count > 1
            ? compute_ubo_stride<T>(render->my->get_uniform_buffer_offset_alignment())
            : Size;

        size_t unaligned_size = _stride * _count + Alignment - 1;
        _data_raw.reset(new unsigned char[unaligned_size]);

        void* unaligned_ptr = _data_raw.get();
        size_t aligned_size = unaligned_size;
        void* aligned_ptr = std::align(Alignment, Size, unaligned_ptr, aligned_size);
        assert(aligned_ptr);
        assert(aligned_size >= _stride * _count);
        _data_view = ArrayView<T>((T*)aligned_ptr, _count, _stride);

        for (size_t i = 0; i < _count; ++i)
        {
            new (&_data_view[i]) T{};
        }

        my::BufferResource res(my::BufferResource::BufferType::Uniform);
        res.size = _stride * _count;
        res.usage = my::UsageHint::Updatable;
        res.data = nullptr;

        _buffers[0].handle = render->rc->alloc(&res, system, metadata);
        _buffers[1].handle = render->rc->alloc(&res, system, metadata);
    }

    void destroy(my::ResourceContext* rc)
    {
        rc->dealloc(_buffers[0].handle);
        rc->dealloc(_buffers[1].handle);
    }

    void destroy(hrz::Render* render) { destroy(render->rc); }

    void dirty_one(size_t i)
    {
        assert(i < _count);
        int begin = i * _stride;
        int end = begin + sizeof(T);
        _buffers[0].invalidate_span(begin, end);
        _buffers[1].invalidate_span(begin, end);
    }

    void dirty_all()
    {
        _buffers[0].invalidate_span(0, _stride * _count);
        _buffers[1].invalidate_span(0, _stride * _count);
    }

    void set(size_t i, const T& value)
    {
        assert(i < _count);
        _data_view[i] = value;
        dirty_one(i);
    }

    constexpr size_t offset(size_t i) const { return _stride * i; }

    const T& get(size_t i = 0) const
    {
        assert(i < _count);
        return _data_view[i];
    }

    T& get_mutable(size_t i = 0)
    {
        assert(i < _count);
        dirty_one(i);
        return _data_view[i];
    }

    bool update(my::RenderContext* r)
    {
        auto& buffer = _buffers[_current_writable];

        if (!buffer.has_dirty_span()) return false;

        r->update_buffer(
            buffer.handle, buffer.dirty_offset_begin,
            buffer.dirty_offset_end - buffer.dirty_offset_begin,
            (char*)_data_view.data() + buffer.dirty_offset_begin);

        buffer.reset_dirty_span();
        return true;
    }

    // Update must have been called before this!
    my::ResourceHandle get_for_gpu() const
    {
        if (_last_flip_frame != hrz::Render::CurrentFrame)
        {
            _current_writable = 1 - _current_writable;
            _last_flip_frame = hrz::Render::CurrentFrame;
        }

        const auto& buffer = _buffers[1 - _current_writable];
        assert(!buffer.has_dirty_span());
        return buffer.handle;
    }
};

} // namespace render
} // namespace hrz

namespace std
{
template<>
struct hash<hrz::render::LightingSettings>
{
    size_t operator()(const hrz::render::LightingSettings& s) const
    {
        return hrz::hash_values(s.lighting_enabled, s.cast_shadows, s.receive_shadows);
    }
};

} // namespace std
