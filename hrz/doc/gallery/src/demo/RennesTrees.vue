<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import {
    applyDefaultOrthoBaseLayer,
    applySceneTemplate,
    applyScene,
    getLayerByName,
} from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import { ref, computed, watch } from "vue";
import { debounce } from "@/utils/utils";

const api = ref<HrzApi.AsyncApi | null>(null);
const layerTrees = ref<HrzProtocol.ILayerHandle | undefined>(undefined);

const rotationSpread = ref(360);
const scaleSpread = ref(3);
const hueSpread = ref(20);
const lightnessSpread = ref(0.4);
const redTreesProportion = ref(5);

const stylingScript = computed(() => {
    const rotationSpreadRadians = (rotationSpread.value * Math.PI) / 180 / 2;
    return `if (rand_unif_i(0, ${redTreesProportion.value}) == 0) {
    set "color" = hsl(rand_norm_f(20, ${hueSpread.value.toFixed(1)}), 0.63, 0.38);
} else {
    set "color" = hsl(rand_norm_f(110, ${hueSpread.value.toFixed(
        1
    )}), 0.63, rand_unif_f(0.2, ${lightnessSpread.value.toFixed(2)}));
}
set "rotation_z" = rand_unif_f(-${rotationSpreadRadians.toFixed(
        3
    )}, ${rotationSpreadRadians.toFixed(3)});
set "scale_x" = rand_norm_f(10, ${scaleSpread.value.toFixed(1)});
set "scale_y" = prp("scale_x");
set "scale_z" = prp("scale_x");
emit rand_unif_u(0, 2);`;
});

watch(
    [stylingScript, layerTrees, api],
    debounce(() => {
        if (api.value && layerTrees.value) {
            HrzApi.VectorTilesLayerPathBuilder.create(layerTrees.value)
                .style()
                .stylingScript()
                .set(api.value, stylingScript.value);
        }
    }, 200)
);

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api.value = api_;

    layerTrees.value = await applyScene(api.value, "rennes_trees").then(async function () {
        return await getLayerByName(api.value!, "Rennes trees");
    });

    await applySceneTemplate(api.value, "rennes_buildings", false);
    await applyDefaultOrthoBaseLayer(api.value!);
}

async function retrieveTreesLayerModel(): Promise<any> {
    if (!layerTrees.value) {
        return {};
    }
    return (
        await HrzApi.VectorTilesLayerPathBuilder.create(layerTrees.value).get(api.value!)
    ).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Lots of trees</h1>
                <p>
                    In this demo, a large number of trees are loaded as a point dataset, and
                    displayed as instanced 3D models, using one of two glTF models.
                </p>
                <p>
                    In order to reduce visual repetition, the scale, rotation, and color of each
                    tree is randomized using the styling script. You can play with those parameters
                    below to see how they affect the appearance of the scene.
                </p>
                <p>
                    Additionally, impostor rendering is enabled to increase performance. This can be
                    noticed by the fact that trees far away from the camera lose their shadow.
                    Notice however that they keep a similar appearance to the real model, including
                    the randomized properties, under all viewing angles.
                </p>
                <hr />
                <p>
                    <label>Scale spread ({{ $filters.formatNumber(scaleSpread, 2) }})</label>
                    <input type="range" min="0" max="6" step="any" v-model.number="scaleSpread" />
                </p>
                <p>
                    <label>Rotation spread ({{ $filters.formatNumber(rotationSpread, 0) }}°)</label>
                    <input
                        type="range"
                        min="0"
                        max="360"
                        step="any"
                        v-model.number="rotationSpread"
                    />
                </p>
                <p>
                    <label>Hue spread ({{ $filters.formatNumber(hueSpread, 1) }}°)</label>
                    <input type="range" min="0" max="40" step="any" v-model.number="hueSpread" />
                </p>
                <p>
                    <label
                        >Lightness spread ({{ $filters.formatNumber(lightnessSpread, 2) }})</label
                    >
                    <input
                        type="range"
                        min="0"
                        max="1"
                        step="any"
                        v-model.number="lightnessSpread"
                    />
                </p>
                <p>
                    <label
                        >Red trees proportion (1 in
                        {{ $filters.formatNumber(redTreesProportion, 0) }})</label
                    >
                    <input
                        type="range"
                        min="2"
                        max="20"
                        step="1"
                        v-model.number="redTreesProportion"
                    />
                </p>
                <p><label>Styling script</label></p>
                <pre>{{ stylingScript.trim() }}</pre>
                <p>
                    <FullscreenSceneModel
                        text="View trees layer definition"
                        :retrieveData="retrieveTreesLayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
