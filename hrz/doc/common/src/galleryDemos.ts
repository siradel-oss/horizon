export interface Definition {
    title: string;
    tags: string[];
    thumbnailFile: string;
}

export const DEFINITIONS: { [id: string]: Definition } = {
    heatmapEarthquakes: {
        title: "Earthquakes",
        tags: ["dataviz", "heatmap"],
        thumbnailFile: "heatmap_earthquakes.webp",
    },
    loadingIndicator: {
        title: "Loading indicator",
        tags: ["integration"],
        thumbnailFile: "loading_indicator.webp",
    },
    minimap: {
        title: "Minimap",
        tags: ["camera"],
        thumbnailFile: "minimap.webp",
    },
    heatmapBusCoverage: {
        title: "Public transport coverage",
        tags: ["dataviz", "heatmap"],
        thumbnailFile: "heatmap_bus_coverage.webp",
    },
    mapScale: {
        title: "Map scale",
        tags: ["integration", "camera"],
        thumbnailFile: "scale.webp",
    },
    mapbox: {
        title: "Loading a Mapbox scene",
        tags: ["mapbox", "integration", "vector"],
        thumbnailFile: "mapbox.webp",
    },
    ambiance: {
        title: "Ambiance",
        tags: ["customization"],
        thumbnailFile: "ambiance.webp",
    },
    singleImageRaster: {
        title: "Untiled raster",
        tags: ["raster"],
        thumbnailFile: "single_image_raster.webp",
    },
    tiledImageRaster: {
        title: "Tiled raster",
        tags: ["raster"],
        thumbnailFile: "tiled_image_raster.webp",
    },
    tmsRaster: {
        title: "TileMapService raster",
        tags: ["raster"],
        thumbnailFile: "tms_raster.webp",
    },
    tileJsonRaster: {
        title: "TileJSON raster",
        tags: ["raster"],
        thumbnailFile: "tilejson_raster.webp",
    },
    wmtsRaster: {
        title: "WMTS raster",
        tags: ["raster"],
        thumbnailFile: "wmts_raster.webp",
    },
    wmsRaster: {
        title: "WMS raster",
        tags: ["raster"],
        thumbnailFile: "wms_raster.webp",
    },
    arcGisRaster: {
        title: "ArcGIS raster",
        tags: ["raster"],
        thumbnailFile: "arcgis_raster.webp",
    },
    pmTilesRaster: {
        title: "PMTiles raster",
        tags: ["raster"],
        thumbnailFile: "pmtiles_raster.webp",
    },
    palettizedRaster: {
        title: "Raster with palette",
        tags: ["raster", "dataviz"],
        thumbnailFile: "data_raster.webp",
    },
    dtmLod1: {
        title: "DTM & LOD 1 buildings",
        tags: ["terrain", "dtm", "raster", "vector", "lod1"],
        thumbnailFile: "dtm_lod1.webp",
    },
    terrainSettings: {
        title: "Terrain settings",
        tags: ["raster", "terrain"],
        thumbnailFile: "terrain_settings.webp",
    },
};
