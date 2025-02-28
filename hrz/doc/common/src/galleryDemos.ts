export interface Definition {
    title: string;
    tags: string[];
    thumbnailFile: string;
}

export const DEFINITIONS: { [id: string]: Definition } = {
    heatmapEarthquakes: {
        title: "Earthquakes",
        tags: ["dataviz", "heatmap"],
        thumbnailFile: "heatmap_earthquakes.png",
    },
    loadingIndicator: {
        title: "Loading indicator",
        tags: ["integration"],
        thumbnailFile: "loading_indicator.png",
    },
    minimap: {
        title: "Minimap",
        tags: ["camera"],
        thumbnailFile: "minimap.png",
    },
    heatmapBusCoverage: {
        title: "Public transport coverage",
        tags: ["dataviz", "heatmap"],
        thumbnailFile: "heatmap_bus_coverage.png",
    },
    mapScale: {
        title: "Map scale",
        tags: ["integration", "camera"],
        thumbnailFile: "scale.png",
    },
    mapbox: {
        title: "Loading a Mapbox scene",
        tags: ["mapbox", "integration"],
        thumbnailFile: "mapbox.png",
    },
    ambiance: {
        title: "Ambiance",
        tags: ["customization"],
        thumbnailFile: "ambiance.png",
    },
};
