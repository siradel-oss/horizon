<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applyScene, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";

let api: HrzApi.AsyncApi;
let layer: HrzProtocol.ILayerHandle | undefined;

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;
    applyScene(api, "osm_rennes_tilejson").then(async function () {
        layer = await getLayerByName(api, "OpenStreetMap");
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
                <h1>TileJSON raster</h1>
                <p>
                    In this example, a
                    <a href="https://github.com/mapbox/tilejson-spec">TileJSON</a> file is loaded,
                    which describes the structure of a tiled raster. Because of this, the tiled
                    raster can be displayed by giving only the URL of this file to the engine.
                </p>
                <p>
                    <FullscreenSceneModel
                        text="View raster layer data"
                        :retrieveData="retrieveRasterLayerModel"
                    />
                    <FullscreenSource
                        file="assets/demo/osm_rennes_raster_tiles/tileset.json"
                        text="View TileJSON file"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
