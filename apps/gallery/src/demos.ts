// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

import definitionsData from "./galleryDemos.json";

export interface DemoDefinition {
    title: string;
    tags: string[];
    thumbnailFile: string;
}

export interface Demo {
    id: string;
    def: DemoDefinition;
    component: any;
}

export let all: Demo[] = [];
export let byId: { [id: string]: Demo } = {};

function registerDemo(name: string, comp: any): Demo {
    let demo: Demo = {
        id: name,
        def: (definitionsData as Record<string, DemoDefinition>)[name],
        component: comp,
    };

    if (byId.hasOwnProperty(demo.id)) {
        throw new Error(`Duplicate demo id: ${demo.id}`);
    }
    all.push(demo);
    byId[demo.id] = demo;

    return demo;
}

import Minimap from "@/demo/Minimap.vue";
registerDemo("minimap", Minimap);

import HeatmapBusCoverage from "./demo/HeatmapBusCoverage.vue";
registerDemo("heatmapBusCoverage", HeatmapBusCoverage);

import HeatmapEarthquakes from "./demo/HeatmapEarthquakes.vue";
registerDemo("heatmapEarthquakes", HeatmapEarthquakes);

import LoadingIndicator from "./demo/LoadingIndicator.vue";
registerDemo("loadingIndicator", LoadingIndicator);

import MapScale from "./demo/MapScale.vue";
registerDemo("mapScale", MapScale);

import Mapbox from "./demo/Mapbox.vue";
registerDemo("mapbox", Mapbox);

import Ambiance from "./demo/Ambiance.vue";
registerDemo("ambiance", Ambiance);

import UntiledRaster from "./demo/UntiledRaster.vue";
registerDemo("untiledRaster", UntiledRaster);

import TiledImageRaster from "./demo/TiledImageRaster.vue";
registerDemo("tiledImageRaster", TiledImageRaster);

import TmsRaster from "./demo/TmsRaster.vue";
registerDemo("tmsRaster", TmsRaster);

import TileJsonRaster from "./demo/TileJsonRaster.vue";
registerDemo("tileJsonRaster", TileJsonRaster);

import WmtsRaster from "./demo/WmtsRaster.vue";
registerDemo("wmtsRaster", WmtsRaster);

import WmsRaster from "./demo/WmsRaster.vue";
registerDemo("wmsRaster", WmsRaster);

import ArcGisRaster from "./demo/ArcGisRaster.vue";
registerDemo("arcGisRaster", ArcGisRaster);

import PmTilesRaster from "./demo/PmTilesRaster.vue";
registerDemo("pmTilesRaster", PmTilesRaster);

import PalettizedRaster from "./demo/PalettizedRaster.vue";
registerDemo("palettizedRaster", PalettizedRaster);

import DtmLod1 from "./demo/DtmLod1.vue";
registerDemo("dtmLod1", DtmLod1);

import TerrainSettings from "./demo/TerrainSettings.vue";
registerDemo("terrainSettings", TerrainSettings);

import IgnSrtm from "./demo/IgnSrtm.vue";
registerDemo("ignSrtm", IgnSrtm);

import PalettizedTerrain from "./demo/PalettizedTerrain.vue";
registerDemo("palettizedTerrain", PalettizedTerrain);

import NonRealistic from "./demo/NonRealistic.vue";
registerDemo("nonRealistic", NonRealistic);

import Markers from "./demo/Markers.vue";
registerDemo("markers", Markers);

import LiveData from "./demo/LiveData.vue";
registerDemo("liveData", LiveData);

import CsvData from "./demo/CsvData.vue";
registerDemo("csvData", CsvData);

import ParisMetro from "./demo/ParisMetro.vue";
registerDemo("parisMetro", ParisMetro);

import LabelPalette from "./demo/LabelPalette.vue";
registerDemo("labelPalette", LabelPalette);

import RennesTrees from "./demo/RennesTrees.vue";
registerDemo("rennesTrees", RennesTrees);

import Localization from "./demo/Localization.vue";
registerDemo("localization", Localization);

import LesArcs from "./demo/LesArcs.vue";
registerDemo("lesArcs", LesArcs);

import PolylineAnimation from "./demo/PolylineAnimation.vue";
registerDemo("polylineAnimation", PolylineAnimation);

import LoadingScreen from "./demo/LoadingScreen.vue";
registerDemo("loadingScreen", LoadingScreen);

import ModelAnimations from "./demo/ModelAnimations.vue";
registerDemo("modelAnimations", ModelAnimations);

import BikeShareSymbols from "./demo/BikeShareSymbols.vue";
registerDemo("bikeShareSymbols", BikeShareSymbols);

import SeaCurrentArrows from "./demo/SeaCurrentArrows.vue";
registerDemo("seaCurrentArrows", SeaCurrentArrows);

import SceneEditor from "./demo/SceneEditor.vue";
registerDemo("sceneEditor", SceneEditor);

import ThreeDTilesStyling from "./demo/ThreeDTilesStyling.vue";
registerDemo("threeDTilesStyling", ThreeDTilesStyling);

import DynamicMultiTexturing from "./demo/DynamicMultiTexturing.vue";
registerDemo("dynamicMultiTexturing", DynamicMultiTexturing);

import ThreeDTiles from "./demo/ThreeDTiles.vue";
registerDemo("threeDTiles", ThreeDTiles);

import ThreeDTilesVectorOverlay from "./demo/ThreeDTilesVectorOverlay.vue";
registerDemo("threeDTilesVectorOverlay", ThreeDTilesVectorOverlay);

import VisibilityConstraints from "./demo/VisibilityConstraints.vue";
registerDemo("visibilityConstraints", VisibilityConstraints);

import ScreenCapture from "./demo/ScreenCapture.vue";
registerDemo("screenCapture", ScreenCapture);

import LocalAssets from "./demo/LocalAssets.vue";
registerDemo("localAssets", LocalAssets);

import ProceduralTiles from "./demo/ProceduralTiles.vue";
registerDemo("proceduralTiles", ProceduralTiles);

import ClippingPlane from "./demo/ClippingPlane.vue";
registerDemo("clippingPlane", ClippingPlane);

import Viewshed from "./demo/Viewshed.vue";
registerDemo("viewshed", Viewshed);

import CameraTransitions from "./demo/CameraTransitions.vue";
registerDemo("cameraTransitions", CameraTransitions);

import CameraModes from "./demo/CameraModes.vue";
registerDemo("cameraModes", CameraModes);

import CameraMovement from "./demo/CameraMovement.vue";
registerDemo("cameraMovement", CameraMovement);

import Multiview from "./demo/Multiview.vue";
registerDemo("multiview", Multiview);

import Rulers from "./demo/Rulers.vue";
registerDemo("rulers", Rulers);

import VectorEditor from "./demo/VectorEditor.vue";
registerDemo("vectorEditor", VectorEditor);
