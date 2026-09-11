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
let layerDtm: HrzProtocol.LayerHandle | undefined;
let layerLod1Data: HrzProtocol.LayerHandle | undefined;
let layerLod1: HrzProtocol.LayerHandle | undefined;

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;
    applyScene(api, "dinan_dtm_lod1").then(async function () {
        layerDtm = await getLayerByName(api, "Dinan DTM");
        layerLod1Data = await getLayerByName(api, "Dinan buildings data");
        layerLod1 = await getLayerByName(api, "Dinan buildings");
    });
    applyDefaultOrthoBaseLayer(api);
}

async function retrieveDtmRasterLayerModel(): Promise<any> {
    if (!layerDtm) {
        return {};
    }
    return (await HrzApi.DtmRasterLayerPathBuilder.create(layerDtm).get(api)).toJSON();
}

async function retrieveLod1DataLayerModel(): Promise<any> {
    if (!layerLod1Data) {
        return {};
    }
    return (await HrzApi.VectorDataLayerPathBuilder.create(layerLod1Data).get(api)).toJSON();
}

async function retrieveLod1LayerModel(): Promise<any> {
    if (!layerLod1) {
        return {};
    }
    return (await HrzApi.VectorTilesLayerPathBuilder.create(layerLod1).get(api)).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>DTM & LOD 1 buildings</h1>
                <p>
                    This demo shows a DTM raster layer and a LOD 1 buildings layer in Dinan, France.
                </p>
                <p>
                    The DTM layer gives the terrain its shape. For the purpose of the gallery being
                    lightweight, this raster is small and fairly coarse. However, in a real-world
                    scenario, the DTM raster would be much larger and more detailed.
                </p>
                <p>
                    Additionally, LOD 1 buildings are displayed by loading polygons from a vector
                    dataset containing an attribute giving their height. The buildings are extruded
                    using the "extruded geometry" vector representation. They are dynamically
                    clamped on the terrain.
                </p>
                <p>
                    <FullscreenSceneModel
                        text="View DTM layer definition"
                        :retrieveData="retrieveDtmRasterLayerModel"
                    />
                    <FullscreenSceneModel
                        text="View LOD 1 data layer definition"
                        :retrieveData="retrieveLod1DataLayerModel"
                    />
                    <FullscreenSceneModel
                        text="View LOD 1 layer definition"
                        :retrieveData="retrieveLod1LayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
