<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import {
    applyScene,
    findVectorRepresentation,
    getLayerByName,
    applyDefaultSymbolicBaseLayer,
} from "@/utils/scenes";
import { ref, watch } from "vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import { MessageHandler } from "@/utils/messages";

let popup = ref<HTMLElement | null>(null);
let heatmapOpacity = ref<number>(0);
let acceptableRange = ref<number>(0);
let pickedBusStops = ref<number | null>(null);

let msgHandler: MessageHandler;
let api: HrzApi.AsyncApi;
let heatmapLayer: HrzProtocol.ILayerHandle;
let heatmapReprIndex: number;
let heatmapPalette: HrzProtocol.INumericPalette;

// Updates the opacity of each color of the heatmap palette.
watch(heatmapOpacity, async function () {
    for (let colorPoint of heatmapPalette.colorPoints || []) {
        if (colorPoint.firstColor) {
            colorPoint.firstColor.a = heatmapOpacity.value;
        }
        if (colorPoint.secondColor) {
            colorPoint.secondColor.a = heatmapOpacity.value;
        }
    }
    await HrzApi.VectorTilesLayerPathBuilder.create(heatmapLayer)
        .style()
        .representations(heatmapReprIndex)
        .heatmap()
        .numericPalette()
        .set(api, heatmapPalette);
});

// Updates the disc radius of the heatmap.
watch(acceptableRange, async function () {
    await HrzApi.VectorTilesLayerPathBuilder.create(heatmapLayer)
        .style()
        .representations(heatmapReprIndex)
        .heatmap()
        .discRadius()
        .defaultValue()
        .set(api, acceptableRange.value);
});

function displayPickResult(results: HrzProtocol.IPickResults) {
    let result = results.results?.at(0)?.vector?.heatmaps?.at(0)?.value;
    if (typeof result === "number") {
        pickedBusStops.value = result;
    } else {
        pickedBusStops.value = null;
    }
}

async function schedulePick(x: number, y: number) {
    if (!api) return;

    let request: HrzProtocol.IPickRequest = {
        coords: { x: x, y: y },
        includedRasters: [],
    };

    let requestResult = await api.ViewerService.pickScreen(request);
    if (requestResult.hasATicket && requestResult.ticket) {
        msgHandler.awaitPickResult(requestResult.ticket, displayPickResult);
    }
}

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler_: MessageHandler) {
    api = api_;
    msgHandler = msgHandler_;

    heatmapLayer =
        (await applyScene(api, "heatmap_bus_coverage").then(() => {
            return getLayerByName(api, "Bus");
        })) || {};

    applyDefaultSymbolicBaseLayer(api);

    heatmapReprIndex =
        (await findVectorRepresentation(
            api,
            heatmapLayer,
            HrzProtocol.VectorReprType.HEATMAP_VECTOR_REPR
        )) || 0;

    let repr = await HrzApi.VectorTilesLayerPathBuilder.create(heatmapLayer)
        .style()
        .representations(heatmapReprIndex)
        .heatmap()
        .get(api);

    heatmapPalette = repr.numericPalette || {};
    acceptableRange.value = repr.discRadius?.defaultValue || 0;
    heatmapOpacity.value = 0.4;
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Public transport coverage</h1>
                <p>
                    This demo shows the public transport coverage in a city using a heatmap. Each
                    bus stop is represented by a point with a defined radius (the acceptable range)
                    and a value of 1 in the heatmap. The radius is defined in meters. The palette is
                    red where there is no bus stop in the area of influence of each point, and has a
                    gradient otherwise.
                </p>
                <p>
                    This allows this map to represent areas with no acceptable coverage, and a
                    gradient to represent the coverage quality otherwise.
                </p>
                <p>
                    Additionally, using picking, it is possible to click on the map and see the
                    number of bus stops in the acceptable range at any location. This is achieved
                    thanks to the additive accumulation mode.
                </p>
                <div class="my-6">
                    <label
                        >Coverage opacity: {{ $filters.formatNumber(heatmapOpacity * 100) }}%</label
                    ><br />
                    <input
                        type="range"
                        min="0"
                        max="1"
                        step="any"
                        v-model.number.lazy="heatmapOpacity"
                    />
                </div>
                <div class="my-6">
                    <label>Acceptable range: {{ $filters.formatNumber(acceptableRange) }}m</label
                    ><br />
                    <input
                        type="range"
                        min="0"
                        max="1000"
                        step="any"
                        v-model.number.lazy="acceptableRange"
                    />
                </div>
                <FullscreenSource file="source/HeatmapBusCoverage.vue" />
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" @clickAt="schedulePick" />
            <div
                ref="popup"
                class="absolute w-72 left-4 top-4 shadow-lg rounded-xl p-4 text-mBodyMedium bg-secondaryContainer text-onSecondaryContainer"
            >
                <span v-if="pickedBusStops !== null"
                    >Bus stops in the acceptable range: <strong>{{ pickedBusStops }}</strong></span
                >
                <span v-else
                    >Click anywhere to query the number of bus stops in the acceptable range.</span
                >
            </div>
        </template>
    </SplitView>
</template>
