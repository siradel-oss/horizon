<!--
    SPDX-FileCopyrightText: Copyright 2025 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applySceneTemplate, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";

let api: HrzApi.AsyncApi;
let layerOrtho: HrzProtocol.LayerHandle | undefined;
let layerRoutes: HrzProtocol.LayerHandle | undefined;

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;
    layerOrtho = await applyDefaultOrthoBaseLayer(api);
    layerRoutes = await applySceneTemplate(api, "ign_routes").then(async () => {
        return getLayerByName(api, "IGN Routes");
    });
    applySceneTemplate(api, "initial_viewpoint_france");
}

async function retrieveRasterLayerModel(handle?: HrzProtocol.LayerHandle): Promise<any> {
    if (!handle) {
        return {};
    }
    return (await HrzApi.ImageryRasterLayerPathBuilder.create(handle).get(api)).toJSON();
}

async function retrieveRoutesRasterLayerModel(): Promise<any> {
    return retrieveRasterLayerModel(layerRoutes);
}

async function retrieveOrthoRasterLayerModel(): Promise<any> {
    return retrieveRasterLayerModel(layerOrtho);
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>WMTS raster</h1>
                <p>In this example, two rasters served from a WMTS service are displayed.</p>
                <p>
                    <FullscreenSceneModel
                        text="View ortho raster layer data"
                        :retrieveData="retrieveOrthoRasterLayerModel"
                    />
                    <FullscreenSceneModel
                        text="View roads raster layer data"
                        :retrieveData="retrieveRoutesRasterLayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
