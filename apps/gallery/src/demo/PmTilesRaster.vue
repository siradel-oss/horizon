<!--
    SPDX-FileCopyrightText: Copyright 2025 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applyScene, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";

let api: HrzApi.AsyncApi;
let layer: HrzProtocol.LayerHandle | undefined;

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;
    applyScene(api, "paris_pmtiles").then(async function () {
        layer = await getLayerByName(api, "Paris PMTiles");
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
                <h1>PMTiles raster</h1>
                <p>
                    In this example, a
                    <a href="https://github.com/protomaps/PMTiles/tree/main">PMTiles</a>
                    file is loaded, which contains a tile pyramid along with all necessary
                    georeferencing information. This file format is served using HTTP range
                    requests.
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
