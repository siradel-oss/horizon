<!--
    SPDX-FileCopyrightText: Copyright 2025 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applySceneTemplate } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import { ref, watch, shallowRef } from "vue";

enum SkyMode {
    ATMOSPHERE_AND_SPACE = "Atmosphere and space",
    ATMOSPHERE_ONLY = "Atmosphere only",
    NO_SKY = "No sky",
}

let api = shallowRef<HrzApi.AsyncApi | null>(null);
let atmosphereColor = ref<HrzProtocol.Color.$Properties | null>(null);
let spaceColor = ref<HrzProtocol.Color.$Properties | null>(null);
let transitionDistance = ref(0);

let skyMode = ref<SkyMode>(SkyMode.ATMOSPHERE_AND_SPACE);

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api.value = api_;
    applySceneTemplate(api.value, "non_realistic_ambiance").then(async function () {
        atmosphereColor.value = await HrzApi.SceneViewSettingsPathBuilder.create(
            HrzProtocol.SceneViewIndex.SCENE_VIEW_0
        )
            .ambient()
            .sky()
            .staticAtmosphereColor()
            .get(api.value!);
        spaceColor.value = await HrzApi.SceneViewSettingsPathBuilder.create(
            HrzProtocol.SceneViewIndex.SCENE_VIEW_0
        )
            .ambient()
            .sky()
            .staticSpaceColor()
            .get(api.value!);
        transitionDistance.value = await HrzApi.SceneViewSettingsPathBuilder.create(
            HrzProtocol.SceneViewIndex.SCENE_VIEW_0
        )
            .ambient()
            .sky()
            .staticColorTransitionEndDistance()
            .get(api.value!);
    });
    applySceneTemplate(api.value, "plan_ign");
    applySceneTemplate(api.value, "ign_srtm_dtm");
    applySceneTemplate(api.value, "initial_viewpoint_france_far");
}

async function setAtmosphereColor(color: HrzProtocol.Color.$Properties) {
    if (api.value) {
        await HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
            .ambient()
            .sky()
            .staticAtmosphereColor()
            .set(api.value, color);
    }
}

async function setSpaceColor(color: HrzProtocol.Color.$Properties) {
    if (api.value) {
        await HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
            .ambient()
            .sky()
            .staticSpaceColor()
            .set(api.value, color);
    }
}

watch([skyMode], async function () {
    if (!api.value || !atmosphereColor.value || !spaceColor.value) {
        return;
    }

    switch (skyMode.value) {
        case SkyMode.ATMOSPHERE_AND_SPACE:
            await setAtmosphereColor(atmosphereColor.value);
            await setSpaceColor(spaceColor.value);
            break;
        case SkyMode.ATMOSPHERE_ONLY:
            await setAtmosphereColor(atmosphereColor.value);
            await setSpaceColor({ r: 0, g: 0, b: 0, a: 0 });
            break;
        case SkyMode.NO_SKY:
            await setAtmosphereColor({ r: 0, g: 0, b: 0, a: 0 });
            await setSpaceColor({ r: 0, g: 0, b: 0, a: 0 });
            break;
    }
});

watch([transitionDistance], async function () {
    if (!api.value) {
        return;
    }

    await HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
        .ambient()
        .sky()
        .staticColorTransitionEndDistance()
        .set(api.value, transitionDistance.value);
});

async function retrieveAmbientSettings(): Promise<any> {
    if (api.value) {
        return (
            await HrzApi.SceneViewSettingsPathBuilder.create(0).ambient().get(api.value)
        ).toJSON();
    }
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Non-realistic rendering</h1>
                <p>
                    Some scenes represent abstract data, such as maps or plans, and make no attempt
                    at creating a realistic representation of the world. Ambient settings can be
                    configured so that the layer data displayed is displayed faithfully, with no
                    lighting or shadows, no atmospheric effects, and no fog. This makes the user
                    experience more similar to a 2D map viewer, while still benefiting from the 3D
                    capabilities of Horizon.
                </p>
                <p>
                    One example of the usefulness of these ambient settings is to display raster
                    with baked-in hillshading.
                </p>
                <p>
                    Additionally, by setting static sky colours to transparent colours, the scene
                    can be integrated more seamlessly with the host web page: the elements below the
                    canvas that contains the scene will be visible through the transparent sky. This
                    allows for example to display a simple sphere, reminiscent of a globe.
                </p>
                <p>
                    <label>Sky configuration</label>
                    <select v-model.number="skyMode">
                        <option v-for="value in SkyMode" :value="value" :key="value">
                            {{ value }}
                        </option>
                    </select>
                </p>
                <p>
                    <label
                        >Transition distance ({{
                            $filters.formatNumber(transitionDistance)
                        }}
                        px)</label
                    >
                    <input
                        type="range"
                        min="0"
                        max="512"
                        step="1"
                        v-model.number="transitionDistance"
                        :disabled="skyMode === SkyMode.NO_SKY"
                    />
                </p>
                <p>
                    <FullscreenSceneModel
                        text="View ambient settings data"
                        :retrieveData="retrieveAmbientSettings"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer
                :viewer-options="{
                    loadingScreenBackgroundColor: { r: 1, g: 1, b: 1, a: 1 },
                }"
                @ready="onHorizonReady"
            />
        </template>
    </SplitView>
</template>
