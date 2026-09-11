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
let layerDtm: HrzProtocol.LayerHandle | undefined;

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;
    applySceneTemplate(api, "initial_viewpoint_france");
    applySceneTemplate(api, "ign_srtm_dtm").then(async function () {
        layerDtm = await getLayerByName(api, "IGN SRTM DTM");
    });
    applyDefaultOrthoBaseLayer(api);
}

async function retrieveDtmRasterLayerModel(): Promise<any> {
    if (!layerDtm) {
        return {};
    }
    return (await HrzApi.DtmRasterLayerPathBuilder.create(layerDtm).get(api)).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>IGN SRTM DTM</h1>
                <p>
                    This demo is a simple scene containing a DTM layer processed from data collected
                    from the SRTM mission by NASA, and some imagery, both served from IGN's WMTS
                    service.
                </p>
                <p>
                    The DTM layer uses the custom <code>image/x.raw</code> MIME type
                    <a href="../doc/raster_image_formats.html">described in the documentation</a>
                    to decode BIL data.
                </p>
                <p>
                    <FullscreenSceneModel
                        text="View DTM layer definition"
                        :retrieveData="retrieveDtmRasterLayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
