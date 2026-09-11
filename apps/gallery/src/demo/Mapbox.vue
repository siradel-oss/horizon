<!--
    SPDX-FileCopyrightText: Copyright 2025 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { MessageHandler } from "@/utils/messages";

const scenePath = "assets/demo/hrz_basemap_mapbox.json";

async function onHorizonReady(api: HrzApi.AsyncApi, msgHandler: MessageHandler) {
    api.CameraService.setOrbit({
        pose: {
            bearing: 0,
            tilt: -Math.PI / 2,
            position: {
                altitude: 2500000,
                longitude: 0.134,
                latitude: 48.76,
            },
        },
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        maxAltitude: 9999999,
        minTilt: 0,
        maxTilt: Math.PI,
        goToAnimation: {
            duration: 0,
        },
    });

    const scene = await fetch(scenePath).then((res) => res.text());

    const params: HrzProtocol.MapboxTranslationParams.$Shape = {
        styleJson: scene,
        destroyExistingLayers: true,
        ignoreCameraParams: true,
    };

    const ticket = await api.MapboxService.translateScene(params);

    msgHandler.watch((msg) => {
        if (
            msg.payload != "mapboxTranslation" ||
            msg.mapboxTranslation.ticket?.opaque != ticket.opaque
        ) {
            return false;
        }

        if (
            msg.mapboxTranslation.result?.status ==
            HrzProtocol.MapboxTranslationStatus.MAPBOX_TRANSLATION_SUCCESS
        ) {
            console.log("Mapbox scene loaded successfully");
        } else {
            console.error("Failed to load Mapbox scene");
        }

        return true;
    });
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Loading a Mapbox scene</h1>
                <p>
                    In this example, a very simple Mapbox scene is translated into a Horizon scene
                    using Horizon's <code>MapboxService</code>. Once the scene has been loaded and
                    translated, a message of type <code>MAPBOX_TRANSLATION_MESSAGE</code> is sent
                    through the messages queue to indicate that the scene is ready, as well as some
                    information about the layers that have been created, and the IDs that have been
                    assigned.
                </p>
                <p>
                    Please refer to <a href="../doc/mapbox_scenes.html">the documentation</a> for
                    details about the subset of Mapbox features that are supported, and the detailed
                    documentation for the API.
                </p>
                <p>
                    <FullscreenSource file="source/Mapbox.vue" />
                    <FullscreenSource :file="scenePath" text="View Mapbox file" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
