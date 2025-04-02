import { HrzDemos } from "@siradel/horizon-doc-common";

export interface Demo {
    id: string;
    def: HrzDemos.Definition;
    component: any;
}

export let all: Demo[] = [];
export let byId: { [id: string]: Demo } = {};

function registerDemo(name: string, comp: any): Demo {
    let demo: Demo = {
        id: name,
        def: HrzDemos.DEFINITIONS[name],
        component: comp,
    };

    if (byId.hasOwnProperty(demo.id)) {
        throw new Error(`Duplicate demo id: ${demo.id}`);
    }
    all.push(demo);
    byId[demo.id] = demo;

    return demo;
}

// The demo definitions must be defined in //hrz/doc/common/src/galleryDemos.ts
// This is because they must be visible from the documentation for the demo cards
// to be displayed correctly.

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
