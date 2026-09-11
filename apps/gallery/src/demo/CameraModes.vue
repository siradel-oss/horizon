<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import TabButton from "@/component/TabButton.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applySceneTemplate } from "@/utils/scenes";
import { ref, watch } from "vue";
import { MessageHandler } from "@/utils/messages";

type CameraMode = "orbit" | "fixedPosition" | "fixedTarget";

const activeMode = ref<CameraMode>("orbit");
const inertia = ref(0);

let api: HrzApi.AsyncApi;
let msgHandler: MessageHandler;

watch(inertia, (v) => {
    if (!api) return;
    HrzApi.CameraSettingsPathBuilder.create(HrzProtocol.CameraIndex.CAMERA_0)
        .userControlsInertia()
        .set(api, v);
    HrzApi.CameraSettingsPathBuilder.create(HrzProtocol.CameraIndex.CAMERA_0)
        .movementsInertia()
        .set(api, v);
});

function goOrbit(duration: number = 1.2) {
    activeMode.value = "orbit";
    if (!api) return;
    api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        angularViewpoint: {
            target: { latitude: 48.1082, longitude: -1.6807, altitude: 0 },
            bearing: 0.37,
            tilt: 1.0,
            distance: 400,
        },
        altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
        maxAltitude: 1e6,
        minTilt: 0,
        maxTilt: Math.PI,
        goToAnimation: {
            duration,
            easingExponent: 2,
            trajectoryType: HrzProtocol.TrajectoryType.INTERPOLATED,
            easingFunction: HrzProtocol.EasingFunctions.EASE_INOUT,
        },
        correctionAnimation: { duration: 0 },
        isInterruptible: true,
    });
}

function goFixedPosition(duration: number = 1.2) {
    activeMode.value = "fixedPosition";
    if (!api) return;
    // setFixedPosition: camera is placed at the target, distance ~ 0
    api.CameraService.setFixedPosition({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        pose: {
            position: { latitude: 48.10602958, longitude: -1.67663457, altitude: 2 },
            bearing: 2.5,
            tilt: 0.2,
        },
        altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
        minTilt: 0.5,
        maxTilt: 2.5,
        goToAnimation: {
            duration,
            easingExponent: 2,
            trajectoryType: HrzProtocol.TrajectoryType.INTERPOLATED,
            easingFunction: HrzProtocol.EasingFunctions.EASE_INOUT,
        },
    });
}

async function goFixedTarget(duration: number = 1.2) {
    activeMode.value = "fixedTarget";
    if (!api) return;
    await api.CameraService.setFixedTarget({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        angularViewpoint: {
            target: { latitude: 48.11498791, longitude: -1.680405124, altitude: 20 },
            bearing: 0,
            tilt: 0.75,
            distance: 150,
        },
        altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
        minDistance: 50,
        maxDistance: 600,
        minTilt: 0,
        maxTilt: 1.5,
        goToAnimation: {
            duration,
            easingExponent: 2,
            trajectoryType: HrzProtocol.TrajectoryType.INTERPOLATED,
            easingFunction: HrzProtocol.EasingFunctions.EASE_INOUT,
        },
    });
}

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHander_: MessageHandler) {
    api = api_;
    msgHandler = msgHander_;

    await applyDefaultOrthoBaseLayer(api);
    await applySceneTemplate(api, "rennes_buildings", false);

    inertia.value = await HrzApi.CameraSettingsPathBuilder.create(HrzProtocol.CameraIndex.CAMERA_0)
        .userControlsInertia()
        .get(api);

    goOrbit(0);

    msgHandler.watchForever((msg) => {
        if (
            msg.cameraNotification?.animationEnded !== undefined &&
            activeMode.value === "fixedTarget"
        ) {
            api.CameraService.beginContinuousMovement({
                cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
                continuousMovementInterruption:
                    HrzProtocol.CameraContinuousMovementInterruption
                        .CONTINUOUS_MOVEMENT_INTERRUPTIBLE_THEN_RESUME,
                rotation: {
                    bearing: 0.1,
                },
            });
        }
    });
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Camera modes</h1>
                <p>
                    Horizon supports three camera modes. Switch between them to feel the difference
                    in navigation behaviour.
                </p>
                <p>
                    <label>Mode</label>
                    <span class="flex gap-1 flex-wrap mt-1">
                        <TabButton :active="activeMode === 'orbit'" @click="goOrbit()"
                            >Orbit</TabButton
                        >
                        <TabButton
                            :active="activeMode === 'fixedPosition'"
                            @click="goFixedPosition()"
                            >Fixed position</TabButton
                        >
                        <TabButton :active="activeMode === 'fixedTarget'" @click="goFixedTarget()"
                            >Fixed target</TabButton
                        >
                    </span>
                </p>
                <p v-if="activeMode === 'orbit'">
                    <strong>Orbit</strong>: Free navigation. Scroll zooms in/out, drag orbits around
                    a ground target. The target point shifts when panning.
                </p>
                <p v-else-if="activeMode === 'fixedPosition'">
                    <strong>Fixed position</strong>: Camera is fixed at a position in the world.
                    Drag rotates the view in place (look around).
                </p>
                <p v-else-if="activeMode === 'fixedTarget'">
                    <strong>Fixed target</strong>: Camera always looks at a locked target point.
                    Drag orbits around it, scroll changes the distance. Min/max distance and tilt
                    constraints limit the orbit range.
                </p>
                <p>
                    <label>Movement inertia: {{ inertia.toFixed(2) }}</label>
                    <input
                        type="range"
                        min="0"
                        max="1"
                        step="0.01"
                        v-model.number="inertia"
                        class="block w-full"
                    />
                </p>
                <p>
                    <FullscreenSource file="source/CameraModes.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
