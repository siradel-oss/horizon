#include "hrz/common/monitoring_defs.h"

namespace hrz::monitoring
{
namespace systems
{
const char* to_string(Name name)
{
    switch (name)
    {
        case NoSystem: return "No system";
        case Camera: return "Camera";
        case CameraHeight: return "Camera height";
        case ClippingPlanes: return "Clipping planes";
        case DevTools: return "Dev tools";
        case DevUi: return "Dev UI";
        case Gizmos: return "Gizmos";
        case Grid: return "Grid";
        case InMemoryVectorData: return "In-memory vector data";
        case LoadingScreen: return "Loading screen";
        case Models: return "Models";
        case Picking: return "Picking";
        case Presentation: return "Presentation";
        case Resources: return "Resources";
        case Scene: return "Scene";
        case SceneView: return "Scene view";
        case Selection: return "Selection";
        case Shadows: return "Shadows";
        case ShapeEditor: return "Shape editor";
        case SingleModelLayers: return "Single model layers";
        case Sky: return "Sky";
        case Styling: return "Styling";
        case ThreeDTilesLayers: return "3D Tiles layers";
        case VectorDataLoader: return "Vector data loader";
        case VectorTiles: return "Vector tiles";
        case Viewsheds: return "Viewsheds";
        case PlanetGeometry: return "Planet geometry";
        case PlanetSurface: return "Planet surface";
        case PlanetRasters: return "Planet rasters";
        case Cylinders: return "Cylinders";
        case ExtrudedVectors: return "Extruded vectors";
        case FlatOverlays: return "Flat overlays";
        case Heatmaps: return "Heatmaps";
        case Impostors: return "Impostors";
        case InstancedModels: return "Instanced models";
        case Symbols: return "Symbols";
        case PointCloud: return "Point cloud";
        default: assert(false && "Unhandled case"); return "";
    }
}
} // namespace systems
} // namespace hrz::monitoring
