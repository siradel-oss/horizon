<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applyScene, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";

let api: HrzApi.AsyncApi;
let layer: HrzProtocol.ILayerHandle | undefined;

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;
    applyScene(api, "cassini_single_image_raster").then(async function () {
        layer = await getLayerByName(api, "Cassini Paris");
    });
    applyDefaultOrthoBaseLayer(api);
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
                <h1>Untiled raster</h1>
                <p>
                    In this scene, a simple image showing Paris on the
                    <a href="https://en.wikipedia.org/wiki/Cassini_map">Cassini map</a>
                    is loaded as a raster.
                </p>
                <p>
                    Unlike most rasters, this one is not tiled. Such rasters are useful for covering
                    areas with small images without the complication of tiling it as a preprocess
                    step.
                </p>
                <p>
                    Because this raster type has no attached descriptor, it is necessary to provide
                    the full geometry information (projection, bounds, etc.).
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
