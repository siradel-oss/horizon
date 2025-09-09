<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { applyDefaultSymbolicBaseLayer, applyScene, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import { ref, watch, computed } from "vue";
import { MessageHandler } from "@/utils/messages";
import FullscreenSource from "@/component/FullscreenSource.vue";
import { debounce } from "@/utils/utils";

let msgHandler: MessageHandler;

let api = ref<HrzApi.AsyncApi | null>(null);
let layer = ref<HrzProtocol.ILayerHandle | null>(null);
let radius = ref(40);
let offset = ref(60);
let terrainOpacity = ref(0.5);
let isUnderground = ref(false);
let filterBefore = ref(2025);

const stylingScript = computed(() => {
    return `
// The date attribute values are is the "yyyy-mm-dd" format.
// Strings comparisons here are lexicographic, so they work as expected.
if (attr("date") > "${filterBefore.value}-12-31") {
    discard;
}
set "radius" = ${radius.value};
set "offset" = ${isUnderground.value ? -offset.value : offset.value};
set "color" = attr("color");
emit 0;
    `;
});

watch(
    [stylingScript, layer, api],
    debounce(async () => {
        if (layer.value) {
            await HrzApi.VectorTilesLayerPathBuilder.create(layer.value)
                .style()
                .stylingScript()
                .set(api.value!, stylingScript.value);
        }
    }, 100)
);

watch([api, isUnderground, terrainOpacity], async () => {
    const opacity = isUnderground.value ? terrainOpacity.value : 1;
    if (api.value) {
        HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
            .terrain()
            .terrainOpacity()
            .set(api.value, opacity);
    }
});

watch([layer, isUnderground], async () => {
    // We disable shadows casting and received on cylinders when they are underground
    // otherwise they are always in the shadow of the terrain.
    const lightingSettings: HrzProtocol.ILightingSettings = {
        castShadows: !isUnderground.value,
        receiveShadows: !isUnderground.value,
        enableLighting: true,
    };
    if (layer.value) {
        await HrzApi.VectorTilesLayerPathBuilder.create(layer.value)
            .lighting()
            .set(api.value!, lightingSettings);
    }
});

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler_: MessageHandler) {
    api.value = api_;
    msgHandler = msgHandler_;
    await applyScene(api.value, "paris_metro_cylinders").then(async function () {
        layer.value = (await getLayerByName(api.value!, "Metro")) || {};
    });
    await applyDefaultSymbolicBaseLayer(api.value);
}

async function retrieveVectorTilesLayerModel(): Promise<any> {
    if (!layer.value) {
        return {};
    }
    return (await HrzApi.VectorTilesLayerPathBuilder.create(layer.value).get(api.value!)).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Paris metro</h1>
                <p>
                    This demo shows the evolution of the Paris metro network over time. Representing
                    lines as cylinders. The cylinders are generated on the fly from the polylines
                    data.
                </p>
                <p>
                    Some display properties of the cylinders can be changed, including their
                    vertical offset, allowing them to be displayed underground. In this case, the
                    terrain can be made transparent to see the cylinders.
                </p>
                <p>
                    Additionally metro line segments can be filtered according to their
                    commissioning date.
                </p>
                <hr />
                <p>
                    <label>Cylinders radius ({{ $filters.formatNumber(radius) }} m)</label>
                    <input type="range" min="0" max="100" step="1" v-model.number="radius" />
                </p>
                <p>
                    <label>Vertical offset ({{ $filters.formatNumber(offset) }} m)</label>
                    <input type="range" min="0" max="100" step="1" v-model.number="offset" />
                </p>
                <p>
                    <label>
                        <input type="checkbox" v-model="isUnderground" />
                        Display underground
                    </label>
                </p>
                <p v-if="isUnderground">
                    <label
                        >Terrain opacity ({{
                            $filters.formatNumber(terrainOpacity * 100)
                        }}
                        %)</label
                    >
                    <input
                        type="range"
                        min="0"
                        max="1"
                        step="any"
                        v-model.number="terrainOpacity"
                    />
                </p>
                <hr />
                <p><label>Styling script</label></p>
                <pre>{{ stylingScript.trim() }}</pre>
                <p>
                    <FullscreenSource text="View demo source" file="source/ParisMetro.vue" />
                    <FullscreenSceneModel
                        text="View cylinders layer model"
                        :retrieveData="retrieveVectorTilesLayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
            <div
                class="absolute left-4 bottom-4 bg-surfaceContainerLow text-onSurface rounded-lg shadow-lg p-3 w-[40%] min-w-64"
            >
                <label>Show metro network after {{ filterBefore }}</label>
                <input type="range" min="1900" max="2025" step="5" v-model.number="filterBefore" />
            </div>
        </template>
    </SplitView>
</template>
