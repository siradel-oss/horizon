<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applyDefaultSymbolicBaseLayer } from "@/utils/scenes";
import { ref, watch, computed } from "vue";
import { debounce } from "@/utils/utils";

let api: HrzApi.AsyncApi;
let orthoLayer: HrzProtocol.LayerHandle | undefined;
let symbolicLayer: HrzProtocol.LayerHandle | undefined;

let altitudeThreshold = ref<number>(15000);
let altitude = ref<number>(0);

let altitudeFormatted = computed<string>(() => {
    if (altitude.value < 10000) {
        return altitude.value.toFixed(0) + " m";
    } else {
        return (altitude.value / 1000).toFixed(1) + " km";
    }
});

async function applyConstraints(threshold: number) {
    if (!orthoLayer || !symbolicLayer || !api) {
        return;
    }
    await HrzApi.ImageryRasterLayerPathBuilder.create(orthoLayer)
        .visibilityConstraints()
        .set(
            api,
            HrzProtocol.LayerVisibilityConstraintList.fromObject({
                constraints: [
                    {
                        altitude: {
                            relativePosition: HrzProtocol.RelativePositionQualifier.ABOVE,
                            altitude: threshold,
                        },
                    },
                ],
            })
        );
    await HrzApi.ImageryRasterLayerPathBuilder.create(symbolicLayer)
        .visibilityConstraints()
        .set(
            api,
            HrzProtocol.LayerVisibilityConstraintList.fromObject({
                constraints: [
                    {
                        altitude: {
                            relativePosition: HrzProtocol.RelativePositionQualifier.BELOW,
                            altitude: threshold,
                        },
                    },
                ],
            })
        );
}

watch(altitudeThreshold, debounce(applyConstraints, 200));

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;
    orthoLayer = await applyDefaultOrthoBaseLayer(api);
    symbolicLayer = await applyDefaultSymbolicBaseLayer(api);
    api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        pose: {
            position: { latitude: 45.75, longitude: 4.83, altitude: 50000 },
            bearing: 0,
            tilt: -Math.PI / 2,
        },
        maxAltitude: 1e8,
        minTilt: 0,
        maxTilt: Math.PI,
    });
    await applyConstraints(altitudeThreshold.value);
    setInterval(async () => {
        const result = await api.CameraService.getSceneViewScaleAndAltitude({
            sceneView: HrzProtocol.SceneViewIndex.SCENE_VIEW_0,
        });
        altitude.value = result.altitudeAbsolute;
    }, 30);
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Visibility constraints</h1>
                <p>
                    Layers can be made visible or hidden based on the camera's altitude above the
                    ellipsoid. In this demo, the ortho (satellite) layer is shown when the camera is
                    <strong>above</strong> the threshold, and the symbolic (map) layer is shown when
                    it is <strong>below</strong>. Zoom in or out to see the switch, or adjust the
                    threshold with the slider.
                </p>
                <p>
                    <label
                        >Altitude threshold ({{
                            $filters.formatNumber(altitudeThreshold / 1000, 1)
                        }}
                        km)</label
                    >
                    <input
                        type="range"
                        min="1000"
                        max="100000"
                        step="1000"
                        v-model.number="altitudeThreshold"
                    />
                </p>
                <p>
                    <FullscreenSource file="source/VisibilityConstraints.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
            <div
                class="absolute bottom-4 left-4 pointer-events-none bg-secondaryContainer text-onSecondaryContainer rounded-lg shadow-lg p-3 text-mLabelMedium"
            >
                <div class="font-bold text-onSurfaceVariant">Camera altitude</div>
                {{ altitudeFormatted }}
            </div>
        </template>
    </SplitView>
</template>
