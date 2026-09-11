<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import TextButton from "@/component/TextButton.vue";
import TabButton from "@/component/TabButton.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyDefaultSymbolicBaseLayer } from "@/utils/scenes";
import { MessageHandler } from "@/utils/messages";
import { ref } from "vue";

interface Destination {
    name: string;
    viewpoint: HrzProtocol.OrbitCameraTransition.$Shape;
}

const DESTINATIONS: Destination[] = [
    {
        name: "France",
        viewpoint: {
            bounds: {
                bounds: { west: -5.5, south: 41.3, east: 9.6, north: 51.2 },
            },
            altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
            maxAltitude: 1e7,
            minTilt: 0,
            maxTilt: Math.PI,
        },
    },
    {
        name: "Paris",
        viewpoint: {
            angularViewpoint: {
                target: { latitude: 48.8566, longitude: 2.3522, altitude: 0 },
                bearing: 0,
                tilt: 0.4,
                distance: 12000,
            },
            altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
            maxAltitude: 1e6,
            minTilt: 0,
            maxTilt: Math.PI,
        },
    },
    {
        name: "Rennes",
        viewpoint: {
            angularViewpoint: {
                target: { latitude: 48.1173, longitude: -1.6778, altitude: 0 },
                bearing: 0,
                tilt: 0.4,
                distance: 8000,
            },
            altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
            maxAltitude: 1e6,
            minTilt: 0,
            maxTilt: Math.PI,
        },
    },
    {
        name: "Bordeaux",
        viewpoint: {
            angularViewpoint: {
                target: { latitude: 44.8378, longitude: -0.5792, altitude: 0 },
                bearing: 0,
                tilt: 0.4,
                distance: 8000,
            },
            altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
            maxAltitude: 1e6,
            minTilt: 0,
            maxTilt: Math.PI,
        },
    },
    {
        name: "Nice",
        viewpoint: {
            angularViewpoint: {
                target: { latitude: 43.7102, longitude: 7.262, altitude: 0 },
                bearing: 0,
                tilt: 0.4,
                distance: 8000,
            },
            altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
            maxAltitude: 1e6,
            minTilt: 0,
            maxTilt: Math.PI,
        },
    },
    {
        name: "Strasbourg",
        viewpoint: {
            angularViewpoint: {
                target: { latitude: 48.5734, longitude: 7.7521, altitude: 0 },
                bearing: 0,
                tilt: 0.4,
                distance: 8000,
            },
            altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
            maxAltitude: 1e6,
            minTilt: 0,
            maxTilt: Math.PI,
        },
    },
];

const EASING_OPTIONS = [
    { label: "Ease in", value: HrzProtocol.EasingFunctions.EASE_IN },
    { label: "Ease out", value: HrzProtocol.EasingFunctions.EASE_OUT },
    { label: "Ease in/out", value: HrzProtocol.EasingFunctions.EASE_INOUT },
];

const TRAJECTORY_OPTIONS = [
    { label: "Ballistic", value: HrzProtocol.TrajectoryType.BALLISTIC },
    { label: "Interpolated", value: HrzProtocol.TrajectoryType.INTERPOLATED },
];

const duration = ref(2.0);
const easingFunction = ref(HrzProtocol.EasingFunctions.EASE_INOUT);
const easingExponent = ref(2.0);
const trajectoryType = ref(HrzProtocol.TrajectoryType.BALLISTIC);

let api: HrzApi.AsyncApi;
let msgHandler: MessageHandler;

function buildAnimation(): HrzProtocol.CameraAnimationOptions.$Shape {
    return {
        duration: duration.value,
        easingFunction: easingFunction.value,
        easingExponent: easingExponent.value,
        trajectoryType: trajectoryType.value,
    };
}

function goTo(destination: Destination) {
    if (!api) return;
    api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        ...destination.viewpoint,
        goToAnimation: buildAnimation(),
        correctionAnimation: { duration: 0 },
        isInterruptible: true,
    });
}

async function onCanvasClick(x: number, y: number) {
    if (!api) return;
    const result = await api.ViewerService.pickScreen({ coords: { x, y }, includedRasters: [] });
    if (!result.hasATicket || !result.ticket) return;
    msgHandler.awaitPickResult(result.ticket, (pickResult) => {
        if (!pickResult.position) return;
        api.CameraService.setOrbit({
            cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
            angularViewpoint: {
                target: pickResult.position,
                bearing: 0,
                tilt: 0.9,
                distance: 5000,
            },
            altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
            maxAltitude: 1e7,
            minTilt: 0,
            maxTilt: Math.PI,
            goToAnimation: buildAnimation(),
            correctionAnimation: { duration: 0 },
            isInterruptible: true,
        });
    });
}

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler_: MessageHandler) {
    api = api_;
    msgHandler = msgHandler_;
    await applyDefaultSymbolicBaseLayer(api);
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Camera transitions</h1>
                <p>
                    Fly to a preset destination or click anywhere on the map to set a custom
                    viewpoint. Adjust the animation parameters below to feel the difference.
                </p>
                <p>
                    The "France" destination uses a geographic
                    <code>bounds</code> viewpoint to fit the whole country in view; the others use
                    an <code>angularViewpoint</code> with an explicit target, bearing and distance.
                </p>
                <p>
                    <label>Destinations</label>
                    <span class="flex gap-1 flex-wrap mt-1">
                        <TextButton
                            v-for="dest in DESTINATIONS"
                            :key="dest.name"
                            color="onSurface"
                            @click="goTo(dest)"
                            >{{ dest.name }}</TextButton
                        >
                    </span>
                </p>
                <p>
                    <label>Duration: {{ duration.toFixed(1) }} s</label>
                    <input
                        type="range"
                        min="0"
                        max="5"
                        step="0.1"
                        v-model.number="duration"
                        class="block w-full"
                    />
                </p>
                <p>
                    <label>Easing</label>
                    <span class="flex gap-1 flex-wrap mt-1">
                        <TabButton
                            v-for="opt in EASING_OPTIONS"
                            :key="opt.value"
                            :active="easingFunction === opt.value"
                            @click="easingFunction = opt.value"
                            >{{ opt.label }}</TabButton
                        >
                    </span>
                </p>
                <p>
                    <label>Easing exponent: {{ easingExponent.toFixed(1) }}</label>
                    <input
                        type="range"
                        min="1"
                        max="6"
                        step="0.1"
                        v-model.number="easingExponent"
                        class="block w-full"
                    />
                </p>
                <p>
                    <label>Trajectory</label>
                    <span class="flex gap-1 flex-wrap mt-1">
                        <TabButton
                            v-for="opt in TRAJECTORY_OPTIONS"
                            :key="opt.value"
                            :active="trajectoryType === opt.value"
                            @click="trajectoryType = opt.value"
                            >{{ opt.label }}</TabButton
                        >
                    </span>
                </p>
                <p>
                    <FullscreenSource file="source/CameraTransitions.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer
                :messageHandlerIntervalMs="50"
                @ready="onHorizonReady"
                @clickAt="onCanvasClick"
            />
        </template>
    </SplitView>
</template>
