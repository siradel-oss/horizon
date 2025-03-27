<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applySceneTemplate } from "@/utils/scenes";
import { ref, computed, watch } from "vue";

const MIN_SCALE_SIZE = 150;
const MAX_SCALE_SIZE = 300;
const MID_SCALE_SIZE = (MIN_SCALE_SIZE + MAX_SCALE_SIZE) / 2;
const ACCEPTABLE_SCALES = [1, 2, 3, 5];

let scaleBar = ref<SVGElement | null>(null);
let altitude = ref<number>(0);
let oneMeterSizeInPixels = ref<number>(0);

let scaleReferenceLength = ref<number>(0);

watch(oneMeterSizeInPixels, () => {
    if (oneMeterSizeInPixels.value <= 0) {
        scaleReferenceLength.value = 0;
        return;
    }

    // We first calculate what would be the reference length for a scale of a given size.
    let firstGuessReferenceLength = MID_SCALE_SIZE / oneMeterSizeInPixels.value;

    // Then we find the powers of 10 above and below that value. If this was 150, we want 10 and 1000.
    // And we multiply them by the acceptable scales. These will be the reference lengths we will test.
    let exponent = Math.floor(Math.log10(firstGuessReferenceLength));

    let referenceLengths = [];
    for (let i = -1; i <= 1; ++i) {
        for (let scale of ACCEPTABLE_SCALES) {
            referenceLengths.push(Math.pow(10, exponent + i) * scale);
        }
    }

    // Finally we can try every eligible reference length until we find
    // one that results in a scale that has the minimum size.
    for (let referenceLength of referenceLengths) {
        let scaleSize = oneMeterSizeInPixels.value * referenceLength;
        if (scaleSize >= MIN_SCALE_SIZE) {
            scaleReferenceLength.value = referenceLength;
            return;
        }
    }
});

watch([oneMeterSizeInPixels, scaleReferenceLength], () => {
    if (!scaleBar.value) {
        return;
    }

    let text = scaleReferenceLength.value.toFixed(0) + " m";
    if (scaleReferenceLength.value >= 1000) {
        text = (scaleReferenceLength.value / 1000).toFixed(0) + " km";
    }

    scaleBar.value.innerHTML = `
        <path
            d="M5,25 v5 h${scaleReferenceLength.value * oneMeterSizeInPixels.value} v-5"
            stroke="#222"
            fill="none"
            stroke-width="6px"
            stroke-linecap="square"
            stroke-linejoin="miter"
        />
        <path
            d="M5,25 v5 h${scaleReferenceLength.value * oneMeterSizeInPixels.value} v-5"
            stroke="#fff"
            fill="none"
            stroke-width="2px"
            stroke-linecap="square"
            stroke-linejoin="miter"
        />
        <text
            x="13"
            y="20"
            stroke="#222"
            fill="#fff"
            font-size="16px"
            stroke-width="4px"
            paint-order="stroke"
            font-family="Roboto"
        >${text}</text>
    `;
});

let altitudeFormatted = computed<string>(() => {
    if (altitude.value < 10000) {
        return altitude.value.toFixed(0) + " m";
    } else {
        return (altitude.value / 1000).toFixed(1) + " km";
    }
});

function onHorizonReady(api: HrzApi.AsyncApi) {
    applyDefaultOrthoBaseLayer(api);
    applySceneTemplate(api, "initial_viewpoint_france");

    setInterval(async () => {
        let result = await api.CameraService.getSceneViewScaleAndAltitude({
            sceneView: HrzProtocol.SceneViewIndex.SCENE_VIEW_0,
        });
        altitude.value = result.altitudeRelativeToTerrain;
        oneMeterSizeInPixels.value = result.oneMeterSizeInPixels;
    }, 30);
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Map scale</h1>
                <p>
                    In this example, we use the
                    <a href="../HrzProtocol.CameraService.html#method-GetSceneViewScaleAndAltitude"
                        ><code>GetSceneViewScaleAndAltitude</code></a
                    >
                    method from <code>CameraService</code> to fetch information about the current
                    view.
                </p>
                <p>
                    One piece of information we retrieve is the scale of the map, which is an
                    approximate value for the numbers of pixels per meters at the focus point of the
                    view. This takes the terrain into account. Using this information, we display
                    scale bar at the bottom-left of the screen.
                </p>
                <p>
                    This method can also be used to fetch the current altitude, relative to the
                    terrain or to the sea level, of the camera for the given scene view. For
                    instance, the current altitude is {{ altitudeFormatted }}.
                </p>
                <p><FullscreenSource file="source/MapScale.vue" /></p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
            <svg
                ref="scaleBar"
                class="absolute bottom-4 left-4 pointer-events-none"
                width="400"
                height="34"
            ></svg>
        </template>
    </SplitView>
</template>
