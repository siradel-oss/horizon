<!--
    SPDX-FileCopyrightText: Copyright 2025 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applyScene } from "@/utils/scenes";
import FullscreenSource from "@/component/FullscreenSource.vue";
import { ref, shallowRef, watch } from "vue";
import { debounce } from "@/utils/utils";

let layers: {
    handle: HrzProtocol.LayerHandle;
    speedSpreadFactor: number;
    phaseSpreadFactor: number;
}[] = [];

let api = shallowRef<HrzApi.AsyncApi | null>(null);
let baseSpeed = ref(1.0);
let speedSpread = ref(0.2);
let phaseSpread = ref(1.0);

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api.value = api_;
    await applyScene(api.value, "wind_turbines").then(async () => {
        const allLayers = await api.value!.LayerService.getAllLayers();
        layers = allLayers.layers
            .filter((layer) => layer.type == HrzProtocol.LayerType.SINGLE_MODEL && layer.handle)
            .map((layer) => ({
                handle: HrzProtocol.LayerHandle.create(layer.handle || {}),
                speedSpreadFactor: Math.random(),
                phaseSpreadFactor: Math.random(),
            }));
    });
    applyDefaultOrthoBaseLayer(api.value);
}

watch(
    [api, baseSpeed, speedSpread, phaseSpread],
    debounce(async () => {
        if (!api.value) return;
        await Promise.all(
            layers.flatMap((layerInfo) => {
                const speed = baseSpeed.value + speedSpread.value * layerInfo.speedSpreadFactor;
                const phase = phaseSpread.value * layerInfo.phaseSpreadFactor;

                let speedPromise = HrzApi.SingleModelLayerPathBuilder.create(layerInfo.handle)
                    .animation()
                    .speed()
                    .set(api.value!, speed);
                let phasePromise = HrzApi.SingleModelLayerPathBuilder.create(layerInfo.handle)
                    .animation()
                    .phase()
                    .set(api.value!, phase);

                return [speedPromise, phasePromise];
            })
        );
    }, 200),
    { immediate: true }
);
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Model animations</h1>
                <p>The scene shows glTF models with animations being played.</p>
                <p>
                    The controls below allow you to play with the speed and phase of the animation
                    of each model.
                </p>
                <hr />
                <p>
                    <label>Base speed ({{ $filters.formatNumber(baseSpeed, 1) }})</label>
                    <input type="range" min="-4" max="4" step="any" v-model.number="baseSpeed" />
                </p>
                <p>
                    <label>Speed random spread ({{ $filters.formatNumber(speedSpread, 1) }})</label>
                    <input type="range" min="0" max="2" step="any" v-model.number="speedSpread" />
                </p>
                <p>
                    <label>Phase random spread ({{ $filters.formatNumber(phaseSpread, 1) }})</label>
                    <input type="range" min="0" max="2" step="any" v-model.number="phaseSpread" />
                </p>
                <hr />
                <p>
                    <FullscreenSource file="source/ModelAnimations.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
