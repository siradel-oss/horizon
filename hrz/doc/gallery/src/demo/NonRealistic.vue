<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { applyScene, applySceneTemplate, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";

let api: HrzApi.AsyncApi;

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;
    applySceneTemplate(api, "non_realistic_ambiance");
    applySceneTemplate(api, "plan_ign");
    applySceneTemplate(api, "ign_srtm_dtm");
    applySceneTemplate(api, "initial_viewpoint_france_far");
}

async function retrieveAmbientSettings(): Promise<any> {
    return (await HrzApi.SceneViewSettingsPathBuilder.create(0).ambient().get(api)).toJSON();
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
                    <FullscreenSceneModel
                        text="View ambient settings data"
                        :retrieveData="retrieveAmbientSettings"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
