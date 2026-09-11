<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import ColorButton from "@/component/ColorButton.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applySceneTemplate } from "@/utils/scenes";
import { MessageHandler } from "@/utils/messages";
import { ref } from "vue";

let api: HrzApi.AsyncApi;

let captureDataUrl = ref<string | null>(null);
let flashVisible = ref(false);

function displayCapture(capture: HrzProtocol.Image.$Shape) {
    let canvas = document.createElement("canvas");
    canvas.width = capture.width || 0;
    canvas.height = capture.height || 0;
    let ctx = canvas.getContext("2d")!;
    let imageData = ctx.createImageData(capture.width || 0, capture.height || 0);
    imageData.data.set(new Uint8ClampedArray(capture.data as Uint8Array));
    ctx.putImageData(imageData, 0, 0);
    captureDataUrl.value = canvas.toDataURL("image/png");
}

async function takeCapture() {
    if (!api) return;
    flashVisible.value = true;
    setTimeout(() => {
        flashVisible.value = false;
    }, 50);
    await api.ViewerService.scheduleFrameCapture();
}

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler: MessageHandler) {
    api = api_;
    await applyDefaultOrthoBaseLayer(api);
    await applySceneTemplate(api, "rennes_buildings");
    api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        pose: {
            position: { latitude: 48.113, longitude: -1.674, altitude: 800 },
            bearing: 0.5,
            tilt: -1.1,
        },
        maxAltitude: 1e8,
        minTilt: 0,
        maxTilt: Math.PI,
    });
    msgHandler.watch((msg) => {
        if (msg.payload === "frameCapture") {
            displayCapture(msg.frameCapture);
        }
        return false;
    });
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Screen capture</h1>
                <p>
                    The current view can be captured at any time using
                    <code>ViewerService.scheduleFrameCapture()</code>. The result is returned
                    asynchronously via the message queue as a raw RGBA image, which can then be
                    converted to a data URL and displayed or downloaded.
                </p>
                <p>
                    <ColorButton @click="takeCapture">Capture current view</ColorButton>
                </p>
                <div v-if="captureDataUrl" class="mt-4">
                    <img :src="captureDataUrl" alt="Screen capture" class="w-full block" />
                </div>
                <p>
                    <FullscreenSource file="source/ScreenCapture.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <div class="relative w-full h-full">
                <Viewer @ready="onHorizonReady" />
                <Transition name="flash">
                    <div
                        v-if="flashVisible"
                        class="absolute inset-0 bg-white opacity-50 pointer-events-none"
                    />
                </Transition>
            </div>
        </template>
    </SplitView>
</template>

<style scoped>
.flash-enter-active {
    transition: opacity 0s;
}
.flash-leave-active {
    transition: opacity 0.3s ease-out;
}
.flash-enter-from,
.flash-leave-from {
    opacity: 0.5;
}
.flash-leave-to {
    opacity: 0;
}
</style>
