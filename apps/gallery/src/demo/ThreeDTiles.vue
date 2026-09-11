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
    await applyScene(api, "rennes_3dtiles").then(async function () {
        layer = await getLayerByName(api, "Rennes 3D Tiles");
    });
    applyDefaultOrthoBaseLayer(api);
}

async function retrieveLayerModel(): Promise<any> {
    if (!layer) {
        return {};
    }
    return (await HrzApi.ThreeDTilesLayerPathBuilder.create(layer).get(api)).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>3D Tiles</h1>
                <p>
                    In this example, a simple 3D Tiles layer is displayed. 3D Tiles is a format for
                    streaming massive heterogeneous 3D geospatial datasets.
                </p>
                <p>
                    Due to size constraints, the 3D Tiles dataset used in this example is fairly
                    small and low resolution. But the same approach can be used to display much
                    larger and more detailed datasets.
                </p>
                <p>
                    <FullscreenSceneModel
                        text="View layer data"
                        :retrieveData="retrieveLayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
