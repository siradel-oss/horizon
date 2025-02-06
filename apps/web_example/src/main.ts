import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { HrzCoreBackend } from "@siradel/horizon-core";

async function goToDefaultViewpoint(api: HrzApi.AsyncApi, duration: number) {
    api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        bounds: {
            bounds: {
                west: -5.668945,
                south: 41.804078,
                east: 10.371094,
                north: 52.025459,
            },
            maxAltitude: 0,
            minAltitude: 0,
            tilt: 0,
        },
        goToAnimation: {
            duration: duration,
            easingExponent: 2,
            easingFunction: HrzProtocol.EasingFunctions.EASE_INOUT,
            trajectoryType: HrzProtocol.TrajectoryType.BALLISTIC,
        },
        maxAltitude: 100000000,
        minTilt: 0,
        maxTilt: Math.PI,
    });
}

async function addOrthoLayer(api: HrzApi.AsyncApi) {
    let layerModel: HrzProtocol.IImageryRasterLayer = {
        raster: {
            provider: {
                type: HrzProtocol.RasterProviderType.WMTS_PROVIDER,
                wmts: {
                    url: "https://data.geopf.fr/wmts?SERVICE=WMTS&VERSION=1.0.0&REQUEST=GetCapabilities",
                    attribution: "Institut national de l'information géographique et forestière",
                    layerIdentifier: "ORTHOIMAGERY.ORTHOPHOTOS",
                    imageFormat: HrzProtocol.ImageFormat.SRGBA_8,
                    missingTilePolicy: HrzProtocol.MissingTilePolicy.USE_LOWER_RESOLUTION,
                },
            },
            sampling: {
                filtering: HrzProtocol.TextureFiltering.BILINEAR,
                alphaChannelUsage: HrzProtocol.AlphaChannelUsage.IGNORE_ALPHA_CHANNEL,
                nodataHandling: HrzProtocol.NodataHandling.IGNORE_NODATA,
            },
            blending: {
                opacity: 1,
            },
            displayBounds: {
                east: 180,
                north: 90,
                south: -90,
                west: -180,
            },
        },
        visible: true,
        sceneViews: {
            bits: 1,
        },
    };

    let layerHandle = await api.LayerService.createLayer({
        name: "Plan IGN v2",
        type: HrzProtocol.LayerType.IMAGERY_RASTER,
    });

    HrzApi.ImageryRasterLayerPathBuilder.create(layerHandle).set(api, layerModel);
}

async function addDtmLayer(api: HrzApi.AsyncApi) {
    let layerModel: HrzProtocol.IDtmRasterLayer = {
        raster: {
            provider: {
                type: HrzProtocol.RasterProviderType.TILED_IMAGE_PROVIDER,
                tiledImage: {
                    urlPattern:
                        "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png",
                    imageFormat: HrzProtocol.ImageFormat.MAPZEN_TERRARIUM,
                    missingTilePolicy: HrzProtocol.MissingTilePolicy.USE_EMPTY_TILE,
                    geometry: {
                        projection: {
                            descriptorType: HrzProtocol.SrsDescriptorType.SRID_DESCRIPTOR,
                            descriptor: "EPSG:3857",
                        },
                        tilingScheme: {
                            type: HrzProtocol.TilingSchemeType.GLOBAL,
                            globalTiling: {
                                tileSize: 256,
                                levelZeroTileCountX: 1,
                                levelZeroTileCountY: 1,
                                minLevel: 0,
                                maxLevel: 15,
                                borderTileAspect: HrzProtocol.BorderTileAspect.FULL_SIZED,
                            },
                        },
                    },
                },
            },
            sampling: {
                filtering: HrzProtocol.TextureFiltering.BILINEAR,
                alphaChannelUsage: HrzProtocol.AlphaChannelUsage.IGNORE_ALPHA_CHANNEL,
                nodataHandling: HrzProtocol.NodataHandling.IGNORE_NODATA,
            },
            blending: {
                opacity: 1,
            },
            displayBounds: {
                east: 180,
                north: 90,
                south: -90,
                west: -180,
            },
        },
        visible: true,
    };

    let layerHandle = await api.LayerService.createLayer({
        name: "DTM",
        type: HrzProtocol.LayerType.DTM_RASTER,
    });

    HrzApi.DtmRasterLayerPathBuilder.create(layerHandle).set(api, layerModel);
}

async function onReady(api: HrzApi.AsyncApi) {
    goToDefaultViewpoint(api, 0);
    addOrthoLayer(api);
    addDtmLayer(api);

    let attributionsDiv = document.getElementById("attributions");

    document.getElementById("resetView").onclick = function () {
        goToDefaultViewpoint(api, 2.5);
    };

    document.getElementById("showAttributions").onclick = function () {
        attributionsDiv.style["display"] = "block";
    };

    document.getElementById("closeAttributions").onclick = function () {
        attributionsDiv.style["display"] = "none";
    };
}

document.addEventListener("DOMContentLoaded", function () {
    let app = document.getElementById("app");

    let canvas = document.createElement("canvas") as HTMLCanvasElement;
    canvas.id = "hrz_canvas";
    app.appendChild(canvas);

    let options = new HrzProtocol.ViewerOptions();
    options.showLoadingScreen = true;
    options.keyBindings = {
        bindings: [
            {
                action: HrzProtocol.KeyAction.TOGGLE_DEV_UI,
                key: HrzProtocol.Key.K_P,
            },
        ],
    };

    HrzCoreBackend.init(
        canvas,
        "wasm/",
        options,
        async function (backend: HrzCoreBackend, initStatus: HrzProtocol.ViewerInitStatus) {
            if (initStatus !== HrzProtocol.ViewerInitStatus.INIT_SUCCESS) {
                console.error("Viewer initialization failed");
                return;
            }

            let api = new HrzApi.AsyncApi(backend);

            let readyCheckInterval = setInterval(async () => {
                let msgs = await api.MessageQueueService.dequeueMessages({ maxMessageCount: 100 });
                for (let msg of msgs.messages) {
                    if (msg.viewerReady !== null) {
                        clearInterval(readyCheckInterval);
                        onReady(api);
                    }
                }
            }, 100);
        }
    );
});
