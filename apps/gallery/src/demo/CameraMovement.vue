<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import Icon from "@/component/Icon.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applySceneTemplate } from "@/utils/scenes";
let api: HrzApi.AsyncApi;

function translate(angleFactorX: number, angleFactorY: number) {
    if (!api) return;
    api.CameraService.move({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        translation: {
            frame: HrzProtocol.CameraFrame.CAMERA_FRAME_TANGENTIAL,
            angleFactorX,
            angleFactorY,
        },
    });
}

function rotate(tilt: number, bearing: number) {
    if (!api) return;
    api.CameraService.move({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        rotation: { tilt, bearing },
    });
}

function zoom(ratio: number) {
    if (!api) return;
    api.CameraService.move({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        zoom: { ratio },
    });
}

function resetNorth() {
    if (!api) return;
    api.CameraService.resetNorth({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        animationOptions: {
            duration: 0.5,
            easingFunction: HrzProtocol.EasingFunctions.EASE_INOUT,
            easingExponent: 2,
        },
        resetTilt: false,
    });
}

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;

    await applyDefaultOrthoBaseLayer(api);
    await applySceneTemplate(api, "rennes_buildings", false);

    await api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        angularViewpoint: {
            target: { latitude: 48.1082, longitude: -1.6807, altitude: 0 },
            bearing: 0.37,
            tilt: 0.9,
            distance: 450,
        },
        altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
        maxAltitude: 1e6,
        minTilt: 0,
        maxTilt: Math.PI,
        goToAnimation: { duration: 0 },
    });
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Camera movement</h1>
                <p>
                    Move the camera programmatically using
                    <code>CameraService.move()</code>. Each call applies a discrete step — zoom
                    ratio, rotation angles, or a translation in the tangential plane.
                </p>
                <p>
                    <span class="flex gap-6 flex-wrap mt-1">
                        <!-- Translate D-pad -->
                        <span>
                            <label>Translate</label>
                            <span class="grid grid-cols-3 gap-1 mt-1">
                                <span />
                                <button class="ctrl-btn w-10" @click="translate(0, 1)">
                                    <Icon icon="keyboard_arrow_up" />
                                </button>
                                <span />
                                <button class="ctrl-btn w-10" @click="translate(-1, 0)">
                                    <Icon icon="keyboard_arrow_left" />
                                </button>
                                <button class="ctrl-btn w-10" @click="translate(0, -1)">
                                    <Icon icon="keyboard_arrow_down" />
                                </button>
                                <button class="ctrl-btn w-10" @click="translate(1, 0)">
                                    <Icon icon="keyboard_arrow_right" />
                                </button>
                            </span>
                        </span>
                        <!-- Look D-pad — positive tilt = look up toward sky -->
                        <span>
                            <label>Look</label>
                            <span class="grid grid-cols-3 gap-1 mt-1">
                                <span />
                                <button class="ctrl-btn w-10" @click="rotate(0.3, 0)">
                                    <Icon icon="keyboard_arrow_up" />
                                </button>
                                <span />
                                <button class="ctrl-btn w-10" @click="rotate(0, -0.3)">
                                    <Icon icon="keyboard_arrow_left" />
                                </button>
                                <button class="ctrl-btn w-10" @click="rotate(-0.3, 0)">
                                    <Icon icon="keyboard_arrow_down" />
                                </button>
                                <button class="ctrl-btn w-10" @click="rotate(0, 0.3)">
                                    <Icon icon="keyboard_arrow_right" />
                                </button>
                            </span>
                        </span>
                        <!-- Zoom -->
                        <span>
                            <label>Zoom</label>
                            <span class="flex gap-1 mt-1">
                                <button class="ctrl-btn w-10" @click="zoom(1.5)">
                                    <Icon icon="zoom_in" />
                                </button>
                                <button class="ctrl-btn w-10" @click="zoom(1 / 1.5)">
                                    <Icon icon="zoom_out" />
                                </button>
                            </span>
                        </span>
                    </span>
                </p>
                <p>
                    <button class="ctrl-btn px-3 gap-2" @click="resetNorth">
                        <Icon icon="north" />
                        Reset north
                    </button>
                </p>
                <p>
                    <FullscreenSource file="source/CameraMovement.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" :withCameraControls="false" />
        </template>
    </SplitView>
</template>

<style scoped>
@reference "../style.css";
.ctrl-btn {
    @apply h-10 rounded-lg flex items-center justify-center
           bg-secondaryContainer text-onSecondaryContainer
           text-mLabelLarge select-none
           transition-shadow hover:shadow-md active:brightness-90;
}
</style>
