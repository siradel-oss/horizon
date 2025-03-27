<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { applyScene, applySceneTemplate, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";

let api: HrzApi.AsyncApi;
let layer: HrzProtocol.ILayerHandle | undefined;

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;
    applyScene(api, "osm_rennes_tiled_raster").then(async function () {
        layer = await getLayerByName(api, "OpenStreetMap");
    });
    applySceneTemplate(api, "ign_bd_ortho");
}

async function retrieveRasterLayerModel(): Promise<any> {
    if (!layer) {
        return {};
    }
    return (await HrzApi.ImageryRasterLayerPathBuilder.create(layer).get(api)).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Tiled raster</h1>
                <p>
                    This example uses a raster that has been tiled into a
                    <a href="https://en.wikipedia.org/wiki/Tiled_web_map">tiles pyramid</a>
                    as a preprocess step. This creates a set of images, whose name is parameterized
                    by the zoom level and the tile coordinates.
                </p>
                <p>
                    In other to load them into Horizon. The URL pattern of the tiles must be
                    provided. See the layer data below. Additionally, the geometry information
                    (projection, bounds, etc.) must be provided for the raster to be displayed at
                    the correct location.
                </p>
                <p>
                    <FullscreenSceneModel
                        text="View raster layer data"
                        :retrieveData="retrieveRasterLayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
