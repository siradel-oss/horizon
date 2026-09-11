<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applyScene, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import { ref, computed, watch, shallowRef } from "vue";
import { debounce } from "@/utils/utils";
import FullscreenSource from "@/component/FullscreenSource.vue";

let api = shallowRef<HrzApi.AsyncApi | null>(null);
let layerData = shallowRef<HrzProtocol.LayerHandle | null>(null);
let layer = shallowRef<HrzProtocol.LayerHandle | null>(null);

const styleScriptTemplate = `
set "color" = colorize("speed_pal", attr("speed_ms_DATE_INDEX"));
set "scale_x" = min(1, add(mul(attr("speed_ms_DATE_INDEX"), 3), 0.25));
set "scale_y" = prp("scale_x");
set "angle_z" = mul(div(attr("dir_deg_DATE_INDEX"), 180), 3.1415);
fork { emit "arrow"; }
`;

const selectedDateIndex = ref(0);
const selectedDate = computed(() => DATES[selectedDateIndex.value]);
const stylingScript = computed(() => {
    return styleScriptTemplate.replace(/DATE_INDEX/g, selectedDateIndex.value.toString());
});

const DATES = [
    new Date(2024, 5, 29),
    new Date(2024, 6, 1),
    new Date(2024, 6, 4),
    new Date(2024, 6, 7),
    new Date(2024, 6, 10),
    new Date(2024, 6, 13),
    new Date(2024, 6, 16),
    new Date(2024, 6, 19),
    new Date(2024, 6, 22),
    new Date(2024, 6, 25),
];

watch(
    [stylingScript, layer, api],
    debounce(async () => {
        if (layer.value) {
            await HrzApi.VectorTilesLayerPathBuilder.create(layer.value)
                .style()
                .stylingScript()
                .set(api.value!, stylingScript.value);
            console.log(stylingScript.value);
        }
    }, 300),
    { immediate: true }
);

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api.value = api_;
    applyScene(api.value, "sea_current_arrows").then(async function () {
        layerData.value = (await getLayerByName(api.value!, "Vector data 1")) || null;
        layer.value = (await getLayerByName(api.value!, "Vector tiles 1")) || null;
    });
    applyDefaultOrthoBaseLayer(api.value);
}

async function retrieveDataLayerModel(): Promise<any> {
    if (!layerData.value || !api.value) {
        return {};
    }
    return (
        await HrzApi.VectorDataLayerPathBuilder.create(layerData.value).get(api.value)
    ).toJSON();
}

async function retrieveLayerModel(): Promise<any> {
    if (!layer.value || !api.value) {
        return {};
    }
    return (await HrzApi.VectorTilesLayerPathBuilder.create(layer.value).get(api.value)).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Sea current vectors</h1>
                <p>
                    This demo displays sea current vectors as arrows, using symbols. The date can be
                    changed using the slider at the bottom right, which updates the styling script
                    to use different attributes for speed and direction.
                </p>
                <p>
                    The arrows are kept flush with the ground by aligning the anchor using cardinal
                    instead of screen directions. The anchor is also rotated according to the
                    current direction, and a transform element scales the arrows based on the
                    current speed.
                </p>
                <hr />
                <p>
                    <FullscreenSceneModel
                        text="View current vectors data layer definition"
                        :retrieveData="retrieveDataLayerModel"
                    />
                    <FullscreenSceneModel
                        text="View current vectors layer definition"
                        :retrieveData="retrieveLayerModel"
                    />
                </p>
                <p><FullscreenSource file="source/SeaCurrentArrows.vue" /></p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
            <div
                class="absolute left-4 bottom-4 bg-surfaceContainerLow text-onSurface rounded-lg shadow-lg p-3 w-[40%] min-w-64"
            >
                <label>Selected date ({{ selectedDate.toDateString() }})</label>
                <input
                    type="range"
                    min="0"
                    :max="DATES.length - 1"
                    step="1"
                    v-model.number="selectedDateIndex"
                />
            </div>
        </template>
    </SplitView>
</template>
