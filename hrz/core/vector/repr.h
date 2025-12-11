#pragma once

#include "hrz/common/blob_array.h"
#include "hrz/common/picking_types.h"
#include "hrz/common/style/styled_features.h"
#include "hrz/common/tile_coords.h"
#include "hrz/common/vector_data/feature_ids.h"
#include "hrz/common/vector_data/tile_geometry.h"
#include "hrz/core/channel.h"
#include "hrz/core/render/lighting_settings.h"
#include "hrz/core/render_request.h"
#include "hrz/core/style/script.h"
#include "hrz/core/vector/symbol/culling.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/intern_string.h"
#include "hrz/protocol/vector/clamping.pb.h"
#include "hrz/protocol/vector/representation.pb.h"

#include <lin_maths.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <variant>

namespace hrz
{
struct AssetsLoader;
struct BlobAllocator;
struct FontRasterizer;
struct ImageDecoder;
struct JobScheduler;
struct Render;
struct RenderViewInfo;
struct HeatmapReprRegistry;
struct SymbolCullingSystem;
struct AttributionRegistry;

namespace camera
{
class Camera;
}

namespace vt
{
struct ImageLoader;

// Data model of the geometry of a vector tile that needs to be passed down to
// a representation system.
struct ReprGeometry
{
    vector_data::VectorTileGeometry geometry;

    // The Z component of the anchors in
    // `features` is the average of the
    // source points Z values.
    // They do not take clamping into account.
    hrz_proto::VectorClamping clamping;
    hrz::BlobArray<float> clamps;
};

namespace repr::messages
{
// Registers a config into the system.
// The `layer_id` is only for allocation identification purposes. See `ResourceOwner`.
struct RegisterStyle
{
    uint64_t style_id;
    hrz_proto::VectorRepr repr;
    uint64_t layer_id;
};

// Unregisters a representation style.
struct UnregisterStyle
{
    uint64_t style_id;
    uint64_t layer_id;
};

// Add a displayable tile given a known style.
struct AddTile
{
    uint64_t tile_id;
    uint64_t style_id;
    TileCoords coords;
    uint64_t layer_id;
    picking::ObjectReference object_ref;
    picking::FeatureReference feature_ref;
    hrz::vector_data::FeatureIds feature_ids;
    ReprGeometry geometry;
    style::StyledFeatures style;
    double min_elevation;
    double max_elevation;
};

// Marks a tile to be destroyed as soon as possible.
struct RemoveTile
{
    uint64_t tile_id;
};

// Informs about the min and max elevation of the tile,
// using the DTM as source.
struct UpdateTileElevation
{
    uint64_t tile_id;
    double min_elevation;
    double max_elevation;
};

// @Todo Allow passing spans to the selection storage, then use an
//       inlined vector in this message.
struct UpdateSelection
{
    uint64_t tile_id;
    hrz::flat_hash_set<vector_data::FeatureIdHash> selected_objects;
};

struct UpdateClipId
{
    uint64_t tile_id;
    int32_t clip_id;
};

struct UpdateLighting
{
    uint64_t tile_id;
    render::LightingSettings lighting;
};

struct StyleRegistrationResult
{
    struct RegisteredProperty
    {
        uint64_t prp_id;
        hrz::vector_data::OwnedAttributeValue default_value;
    };

    uint64_t style_id;
    bool success;
    hrz::InlinedVector<RegisteredProperty, 16> registered_properties;
};

struct TileStatusUpdate
{
    uint64_t tile_id;
    bool is_ready;
};
} // namespace repr::messages

using ToReprMessage = std::variant<
    repr::messages::RegisterStyle,
    repr::messages::UnregisterStyle,
    repr::messages::AddTile,
    repr::messages::RemoveTile,
    repr::messages::UpdateTileElevation,
    repr::messages::UpdateSelection,
    repr::messages::UpdateClipId,
    repr::messages::UpdateLighting>;

using FromReprMessage =
    std::variant<repr::messages::StyleRegistrationResult, repr::messages::TileStatusUpdate>;

class ReprRegistry;

// Defines a system able to display a stylized vector tile.
class ReprSystem
{
public:
    struct WorkCtx
    {
        AssetsLoader* al;
        BlobAllocator* ba;
        ImageDecoder* imgdec;
        ImageLoader* il;
        JobScheduler* js;
        FontRasterizer* fr;
        HeatmapReprRegistry* heatreg;
        ReprRegistry* repr_reg;
        SymbolCullingSystem* symbol_culling;
        AttributionRegistry* attributions;
        std::span<const RenderViewInfo> views_info;
    };

    struct WorkGpuCtx
    {
        Render* render;
        BlobAllocator* ba;
        ImageLoader* il;
        SymbolCullingSystem* symbol_culling;
    };

    struct DrawCtx
    {
        Render* render;
        AttributionRegistry* attributions;
    };

    virtual ~ReprSystem() = default;

    virtual void deinit(WorkCtx&, Render* render) = 0;

    // Initializes all global render resources necessary.
    // Called once at initialization.
    virtual void init_render(Render* render) = 0;

    // Destroys all global render resources.
    virtual void deinit_render(Render* render) = 0;

    // If false, there is no need for clamping geometry positions on the terrain.
    virtual bool uses_z_coordinates() const { return true; }

    // Schedule a tile to be draw in a future frame, not necessarily now.
    // Returns whether this tile can be drawn now or not.
    // The channel ID is here to make the tile ID unambiguous.
    virtual bool schedule_draw_soon(
        uint64_t channel_id,
        uint64_t tile_id,
        uint32_t views_bitset,
        SymbolCullingSystem*)
    {
        assert(always_schedule_instantly());
        return true;
    }

    // Indicates that schedule_draw_soon does not need to be called, and thus tiles don't need any
    // work prior to be drawn.
    virtual bool always_schedule_instantly() const { return true; }

    // Returns the frame number of the latest frame that is ready to be displayed.
    // Usually for representations that have "always_schedule_instantly" = true this will be
    // the current frame number.
    virtual uint64_t next_displayable_frame() const;

    // Schedule a tile to be drawn this frame.
    // This needs to be called at every frame because the system
    // must clear the list of tiles to draw every frame.
    // The channel ID is here to make the tile ID unambiguous.
    virtual void schedule_draw_now(
        uint64_t channel_id,
        uint64_t tile_id,
        uint32_t views_bitset) = 0;

    virtual RenderRequest work(WorkCtx&) = 0;

    virtual RenderRequest work_gpu(WorkGpuCtx&) = 0;

    virtual void draw(DrawCtx&) = 0;

    using Channel = hrz::Channel<ToReprMessage, FromReprMessage>;

    // Returns a new channel to communicate with the representation system,
    // and a unique ID to identify it.
    // The ID is used when making synchronous calls to draw the tiles added
    // through the channel.
    virtual std::pair<uint64_t, Channel> create_channel() = 0;
};

class ReprRegistry
{
    struct Property
    {
        uint32_t ref_count;
    };

    InternString _intern;

    hrz::flat_hash_map<hrz_proto::VectorReprType, std::unique_ptr<ReprSystem>> _reprs;
    hrz::flat_hash_map<uint64_t, hrz::flat_hash_map<uintptr_t, Property>> _layer_ids_to_prps;

public:
    ~ReprRegistry() = default;

    void destroy(
        AssetsLoader*,
        BlobAllocator*,
        JobScheduler*,
        HeatmapReprRegistry*,
        ImageLoader*,
        FontRasterizer*,
        SymbolCullingSystem*,
        Render*);

    uint64_t register_property(uint64_t layer_id, std::string_view name);
    void unregister_property(uint64_t layer_id, uint64_t prp_id);
    void register_properties(uint64_t layer_id, style::Parser&);
    void register_repr(hrz_proto::VectorReprType type, std::unique_ptr<ReprSystem> repr);
    void init_render(Render* render);
    void deinit_render(Render* render);

    RenderRequest work(
        AssetsLoader*,
        BlobAllocator*,
        JobScheduler*,
        HeatmapReprRegistry*,
        ImageDecoder*,
        ImageLoader*,
        FontRasterizer*,
        SymbolCullingSystem*,
        AttributionRegistry*,
        std::span<const RenderViewInfo> views_info);

    RenderRequest work_gpu(Render* render, BlobAllocator*, ImageLoader*, SymbolCullingSystem*);

    void draw(Render* render, AttributionRegistry*);

    ReprSystem& get(hrz_proto::VectorReprType type);
};

std::unique_ptr<ReprSystem> create_null_repr_system();
std::unique_ptr<ReprSystem> create_extruded_repr_system();
std::unique_ptr<ReprSystem> create_flat_overlay_repr_system();
std::unique_ptr<ReprSystem> create_cylinder_repr_system();
std::unique_ptr<ReprSystem> create_model_repr_system();
std::unique_ptr<ReprSystem> create_heatmap_repr_system();
std::unique_ptr<ReprSystem> create_symbol_repr_system();
} // namespace vt
} // namespace hrz
