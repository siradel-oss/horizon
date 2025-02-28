<script setup lang="ts">
import { ref, watch } from "vue";
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { applyScene, applySceneTemplate, getLayerByName } from "@/utils/scenes";
import FullscreenSource from "@/component/FullscreenSource.vue";

let styleSource = ref<string>("");
let accumulationMode = ref<HrzProtocol.HeatmapAccumulationMode>(
    HrzProtocol.HeatmapAccumulationMode.HEATMAP_WEIGHTED_BLENDED
);

let api: HrzApi.AsyncApi;
let layer: HrzProtocol.ILayerHandle | undefined;

watch(accumulationMode, async function () {
    console.log(accumulationMode.value);
    if (layer && api) {
        await HrzApi.VectorTilesLayerPathBuilder.create(layer)
            .style()
            .representations(0)
            .heatmap()
            .accumulation()
            .set(api, accumulationMode.value);
    }
});

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;

    applyScene(api, "heatmap_earthquakes").then(async function () {
        layer = await getLayerByName(api, "Earthquake");
        if (layer) {
            styleSource.value = await HrzApi.VectorTilesLayerPathBuilder.create(layer)
                .style()
                .stylingScript()
                .get(api);
        }
    });

    applySceneTemplate(api, "hrz_basemap_dark_nonames");
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Earthquakes</h1>
                <p>
                    This demonstration shows earthquakes as a heatmap. Each point corresponds to a
                    recorded earthquake, and the size and value of each point represent the
                    magnitude of the earthquake.
                </p>
                <p>
                    We use a palette that goes from red to white for values ranging from 0 to 1.
                    Hence the magnitudes are normalized. This allows us to use the weighted blended
                    accumulation mode to improve readability in areas with many earthquakes. This
                    mode can be changed to visualize its impact.
                </p>
            </div>
            <div class="my-6">
                <label>Accumulation mode</label><br />
                <select v-model="accumulationMode">
                    <option v-for="mode in HrzProtocol.HeatmapAccumulationMode" :value="mode">
                        {{ HrzProtocol.HeatmapAccumulationMode[mode] }}
                    </option>
                </select>
            </div>
            <div class="typography-normal">
                <p>Below is the styling script used for this map.</p>
                <pre>{{ styleSource }}</pre>
                <FullscreenSource file="source/HeatmapEarthquakes.vue" />
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
