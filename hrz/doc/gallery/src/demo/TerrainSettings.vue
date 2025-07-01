<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applyScene, getLayerByName } from "@/utils/scenes";
import { ref, reactive, watch } from "vue";
import { debounce, deepAssign } from "@/utils/utils";
import ColorInput from "@/component/ColorInput.vue";

let api: HrzApi.AsyncApi;
let layerOrtho: HrzProtocol.ILayerHandle | undefined;

let rasterOpacity = ref<number>(1);
let terrainSettings = reactive<HrzProtocol.ITerrainSettings>({});
let rasterSettings = reactive<HrzProtocol.IRasterSettings>({});

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    await applyScene(api_, "dinan_dtm_lod1");
    layerOrtho = await applyDefaultOrthoBaseLayer(api_);

    let sceneTerrainSettings = await HrzApi.SceneViewSettingsPathBuilder.create(
        HrzProtocol.SceneViewIndex.SCENE_VIEW_0
    )
        .terrain()
        .get(api_);
    deepAssign(sceneTerrainSettings, terrainSettings);

    let sceneRasterSettings = await HrzApi.SceneSettingsPathBuilder.create().raster().get(api_);
    deepAssign(sceneRasterSettings, rasterSettings);

    api = api_;
}

watch(
    rasterOpacity,
    debounce(async (value) => {
        if (!layerOrtho || !api) {
            return;
        }
        await HrzApi.ImageryRasterLayerPathBuilder.create(layerOrtho)
            .raster()
            .blending()
            .opacity()
            .set(api, value);
    }, 200)
);

watch(terrainSettings, async (value) => {
    if (!api) {
        return;
    }
    await HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
        .terrain()
        .set(api, value);
});

watch(rasterSettings, async (value) => {
    if (!api) {
        return;
    }
    await HrzApi.SceneSettingsPathBuilder.create().raster().set(api, value);
});
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Terrain & raster settings</h1>
                <p>
                    This demo is a sandbox to play around with global raster and terrain settings.
                </p>
                <p>
                    The ortho raster opacity parameter changes the opacity of the imagery raster
                    layer, and the terrain opacity parameter (which is part of the terrain settings)
                    changes the opacity of the terrain geometry globally. This can be used to show
                    things under the terrain. When the raster is not opaque, it is possible to see
                    the terrain color, which is the "background" color of all rasters. Making the
                    terrain geometry itself transparent exposes the underground color, which is also
                    part of the ambient settings (although not exposed in this demo).
                </p>
                <p>
                    <label
                        >Ortho raster opacity ({{
                            $filters.formatNumber(rasterOpacity * 100)
                        }}%)</label
                    >
                    <input type="range" min="0" max="1" step="any" v-model.number="rasterOpacity" />
                </p>
                <p v-if="terrainSettings.terrainColor">
                    <label>Terrain color</label>
                    <ColorInput v-model="terrainSettings.terrainColor" />
                </p>
                <p v-if="terrainSettings.terrainOpacity !== undefined">
                    <label
                        >Terrain opacity ({{
                            $filters.formatNumber((terrainSettings.terrainOpacity || 0) * 100)
                        }}%)</label
                    >
                    <input
                        type="range"
                        min="0"
                        max="1"
                        step="any"
                        v-model.number="terrainSettings.terrainOpacity"
                    />
                </p>
                <p>
                    The following parameters are more subtle, and define how the imagery rasters are
                    applied to the terrain. See
                    <a href="../HrzProtocol.RasterSettings.html">the documentation</a>
                    for more information about what they do.
                </p>
                <p v-if="rasterSettings.compensateInclination !== undefined">
                    <label>
                        <input type="checkbox" v-model="rasterSettings.compensateInclination" />
                        Compensate inclination
                    </label>
                </p>
                <p v-if="rasterSettings.mixLods !== undefined">
                    <label>
                        <input type="checkbox" v-model="rasterSettings.mixLods" />
                        Smooth level of detail transition
                    </label>
                </p>
                <p v-if="rasterSettings.maxScreenSpaceError !== undefined">
                    <label
                        >Max screen space error ({{
                            $filters.formatNumber(rasterSettings.maxScreenSpaceError || 0, 1)
                        }})</label
                    >
                    <input
                        type="range"
                        min="1"
                        max="8"
                        step="any"
                        v-model.number="rasterSettings.maxScreenSpaceError"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
