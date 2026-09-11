<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import {
    applyDefaultOrthoBaseLayer,
    applyScene,
    applySceneTemplate,
    getLayerByName,
} from "@/utils/scenes";
import FullscreenSource from "@/component/FullscreenSource.vue";
import { ref, computed, watch, shallowRef } from "vue";
import { debounce } from "@/utils/utils";

const api = shallowRef<HrzApi.AsyncApi | null>(null);
const metroLayer = shallowRef<HrzProtocol.LayerHandle | null>(null);
const buildingsLayer = shallowRef<HrzProtocol.LayerHandle | null>(null);

const drawUnder = ref(true);
const opacity = ref(0.4);

const stylingScript = computed(
    () => `
    set "color" = alpha(attr("color"), ${opacity.value});
    emit 0;
`
);

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api.value = api_;

    await applyScene(api_, "rennes_3dtiles").then(async function () {
        buildingsLayer.value = (await getLayerByName(api_, "Rennes 3D Tiles")) || null;
    });

    applySceneTemplate(api_, "rennes_metro").then(async function () {
        metroLayer.value = (await getLayerByName(api_, "Metro vector tiles")) || null;
    });

    applyDefaultOrthoBaseLayer(api_);
}

watch([drawUnder, api, buildingsLayer], async () => {
    if (!api.value || !buildingsLayer.value) return;
    await HrzApi.ThreeDTilesLayerPathBuilder.create(buildingsLayer.value)
        .drawUnderFlatOverlays()
        .set(api.value, drawUnder.value);
});

watch(
    [stylingScript, api, metroLayer],
    debounce(async () => {
        if (!api.value || !metroLayer.value) return;
        await HrzApi.VectorTilesLayerPathBuilder.create(metroLayer.value)
            .style()
            .stylingScript()
            .set(api.value, stylingScript.value);
    }, 200)
);
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>3D Tiles vector overlay</h1>
                <p>
                    This demo shows a 3D Tiles tileset of Rennes with metro line vector tiles
                    rendered on top. The <code>drawUnderFlatOverlays</code>
                    property of the 3D Tiles layer controls the rendering order: when enabled, the
                    3D Tiles are drawn behind the flat vector overlays, keeping the metro lines
                    visible on the surface.
                </p>
                <p>
                    Toggle the checkbox to switch this rendering order on and off, and use the
                    opacity slider to control the transparency of the metro lines.
                </p>
                <hr />
                <p>
                    <label>
                        <input type="checkbox" v-model="drawUnder" />
                        Draw metro lines over 3D Tiles
                    </label>
                </p>
                <p>
                    <label>Metro lines opacity ({{ $filters.formatNumber(opacity * 100) }}%)</label>
                    <input type="range" min="0" max="1" step="any" v-model.number="opacity" />
                </p>
                <hr />
                <p>
                    <FullscreenSource file="source/ThreeDTilesVectorOverlay.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
