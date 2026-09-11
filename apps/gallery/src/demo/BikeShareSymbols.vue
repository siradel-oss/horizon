<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyDefaultSymbolicBaseLayer, applyScene, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";

let api: HrzApi.AsyncApi;
let layerData: HrzProtocol.LayerHandle | undefined;
let layer: HrzProtocol.LayerHandle | undefined;

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;
    applyScene(api, "velostar").then(async function () {
        layerData = await getLayerByName(api, "Velostar data");
        layer = await getLayerByName(api, "Velostar");
    });
    applyDefaultSymbolicBaseLayer(api);
}

async function retrieveDataLayerModel(): Promise<any> {
    if (!layerData) {
        return {};
    }
    return (await HrzApi.VectorDataLayerPathBuilder.create(layerData).get(api)).toJSON();
}

async function retrieveLayerModel(): Promise<any> {
    if (!layer) {
        return {};
    }
    return (await HrzApi.VectorTilesLayerPathBuilder.create(layer).get(api)).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Bike share symbols</h1>
                <p>
                    This demo shows complex symbols with text, icons, backgrounds, leader lines, and
                    multiple anchors. Each symbol is a point in a GeoJSON file and the information
                    displayed in the symbols comes from attributes on those points.
                </p>
                <p>
                    The symbol model is fairly complex. It is composed of two anchors: one for the
                    screen-space label itself (including the leader line so that it is attached to
                    the label), and one for the world-space icon, which is a star shape on the
                    ground at the location of the point.
                </p>
                <hr />
                <p>
                    <FullscreenSceneModel
                        text="View bike share data layer definition"
                        :retrieveData="retrieveDataLayerModel"
                    />
                    <FullscreenSceneModel
                        text="View bike share layer definition"
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
