#pragma once

#include <cassert>
#include <cstdint>

namespace hrz::monitoring
{
namespace systems
{
enum Name : uint8_t
{
    NoSystem = 0,

    Camera,
    CameraHeight,
    ClippingPlanes,
    DevTools,
    DevUi,
    Gizmos,
    Grid,
    InMemoryVectorData,
    LoadingScreen,
    Models,
    Picking,
    Presentation,
    Resources,
    Scene,
    SceneView,
    Selection,
    Shadows,
    ShapeEditor,
    SingleModelLayers,
    Sky,
    Styling,
    ThreeDTilesLayers,
    VectorDataLoader,
    VectorTiles,
    Viewsheds,

    // Planet
    PlanetGeometry,
    PlanetSurface,
    PlanetRasters,

    // Vector representations
    Cylinders,
    ExtrudedVectors,
    FlatOverlays,
    Heatmaps,
    Impostors,
    InstancedModels,
    Symbols,

    PointCloud,
};

const char* to_string(Name name);
} // namespace systems

constexpr uint64_t NoLayer = 0;

struct ResourceOwner
{
    monitoring::systems::Name system{monitoring::systems::NoSystem};
    uint64_t layer_id{NoLayer};

    ResourceOwner() = default;

    ResourceOwner(monitoring::systems::Name system) : system(system) {}

    ResourceOwner(monitoring::systems::Name system, uint64_t layer_id) :
        system(system), layer_id(layer_id)
    {
    }
};
} // namespace hrz::monitoring
