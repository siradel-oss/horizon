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
import { applySceneTemplate, applyDefaultOrthoBaseLayer } from "@/utils/scenes";
import { MessageHandler } from "@/utils/messages";
import { ref, watch } from "vue";

const GIZMO_ID = 0;

const INITIAL_POSITION: HrzProtocol.GeographicPosition.$Properties = {
    latitude: 48.10960945,
    longitude: -1.679085731,
    altitude: 40.5,
};
const INITIAL_BEARING = -0.25;
const INITIAL_TILT = -0.38;

// q = Rz(-bearing) × Rx(tilt)  — bearing is clockwise; math rotations are counter-clockwise
function bearingTiltToQuat(b: number, t: number): HrzProtocol.Quat.$Properties {
    const cb = Math.cos(b / 2),
        sb = Math.sin(b / 2);
    const ct = Math.cos(t / 2),
        st = Math.sin(t / 2);
    return { x: cb * st, y: -sb * st, z: -sb * ct, w: cb * ct };
}

function quatToBearingTilt(q: HrzProtocol.Quat.$Properties): { bearing: number; tilt: number } {
    const x = q.x || 0;
    const y = q.y || 0;
    const z = q.z || 0;
    const w = q.w || 0;
    const bearing = -Math.atan2(2 * (x * y + w * z), w * w + x * x - y * y - z * z);
    const tilt = Math.asin(2 * (y * z + w * x));
    return { bearing, tilt };
}

const settings = ref<HrzProtocol.ViewshedSettings.$Properties>({
    enableViewshed: true,
    enableWireframe: true,
    drawWireframeFromPosition: false,
    position: { ...INITIAL_POSITION },
    bearing: INITIAL_BEARING,
    tilt: INITIAL_TILT,
    hfov: 70,
    aspectRatio: 1.5,
    start: 0.02,
    maxDistance: 300,
    visibleColor: { r: 0.5, g: 1.0, b: 0.5, a: 0.5 },
    hiddenColor: { r: 1.0, g: 0.5, b: 0.5, a: 0.5 },
});

let api: HrzApi.AsyncApi;
let gizmoHandle: HrzProtocol.LayerHandle | undefined;

watch(
    settings,
    async (s) => {
        if (!api) return;
        await HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
            .viewshed()
            .set(api, s);
    },
    { deep: true }
);

function handleGizmoUpdate(update: HrzProtocol.GizmoUpdateMessage.$Shape) {
    if (!gizmoHandle) return;

    if (update.geoPos) {
        HrzApi.GizmoLayerPathBuilder.create(gizmoHandle).position().set(api, update.geoPos);
        settings.value.position = update.geoPos;
    }

    if (update.rotation) {
        HrzApi.GizmoLayerPathBuilder.create(gizmoHandle).rotation().set(api, update.rotation);
        const { bearing, tilt } = quatToBearingTilt(update.rotation);
        settings.value.bearing = bearing;
        settings.value.tilt = tilt;
    }
}

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler: MessageHandler) {
    api = api_;

    await applySceneTemplate(api, "rennes_buildings");
    await applyDefaultOrthoBaseLayer(api);

    await HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
        .viewshed()
        .set(api, settings.value);

    gizmoHandle = await api.LayerService.createLayer({
        type: HrzProtocol.LayerType.GIZMO,
    });
    await HrzApi.GizmoLayerPathBuilder.create(gizmoHandle).set(api, {
        visible: true,
        id: GIZMO_ID,
        size: 120,
        sizeUnit: HrzProtocol.UiSizeUnit.UI_SIZE_IN_PIXELS,
        bboxPadding: 0,
        position: { ...INITIAL_POSITION },
        rotation: bearingTiltToQuat(INITIAL_BEARING, INITIAL_TILT),
        components: [
            {
                type: HrzProtocol.GizmoComponentType.GIZMO_ORIGIN,
                referenceFrame: HrzProtocol.GizmoReferenceFrame.WORLD_REFERENCE,
            },
            {
                type: HrzProtocol.GizmoComponentType.GIZMO_ROTATION_RING_Z,
                referenceFrame: HrzProtocol.GizmoReferenceFrame.WORLD_REFERENCE,
            },
            {
                type: HrzProtocol.GizmoComponentType.GIZMO_ROTATION_RING_X,
                referenceFrame: HrzProtocol.GizmoReferenceFrame.OBJECT_REFERENCE,
            },
            {
                type: HrzProtocol.GizmoComponentType.GIZMO_TRANSLATION_PLANE_Z,
                referenceFrame: HrzProtocol.GizmoReferenceFrame.WORLD_REFERENCE,
            },
            {
                type: HrzProtocol.GizmoComponentType.GIZMO_TRANSLATION_AXIS_Z,
                referenceFrame: HrzProtocol.GizmoReferenceFrame.WORLD_REFERENCE,
            },
        ],
        sceneViews: { bits: 1 },
        grid: {},
        line: {
            extent: 200,
            width: 2,
            color: { r: 1, g: 1, b: 1, a: 1 },
            sceneViews: { bits: 1 },
        },
    });

    msgHandler.watch((msg) => {
        if (msg.payload === "gizmoUpdate" && msg.gizmoUpdate?.id === GIZMO_ID) {
            handleGizmoUpdate(msg.gizmoUpdate!);
        }
        return false;
    });
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Viewshed</h1>
                <p>
                    A viewshed cone reveals which parts of the scene are visible from a given point.
                    Visible areas are shown in green, occluded areas in red. Use the gizmo to move
                    and aim the cone.
                </p>
                <p>
                    The viewshed is part of the scene view settings, not a layer. Its position and
                    orientation are controlled through
                    <code>SceneViewSettingsPathBuilder</code>.
                </p>
                <p>
                    <label>Horizontal field of view: {{ settings.hfov?.toFixed(0) }}°</label>
                    <input
                        type="range"
                        min="5"
                        max="170"
                        step="1"
                        v-model.number="settings.hfov"
                        class="block w-full"
                    />
                </p>
                <p>
                    <label>Aspect ratio: {{ settings.aspectRatio?.toFixed(2) }}</label>
                    <input
                        type="range"
                        min="0.25"
                        max="4"
                        step="0.05"
                        v-model.number="settings.aspectRatio"
                        class="block w-full"
                    />
                </p>
                <p>
                    <label>Start: {{ settings.start?.toFixed(3) }}</label>
                    <input
                        type="range"
                        min="0"
                        max="1"
                        step="0.001"
                        v-model.number="settings.start"
                        class="block w-full"
                    />
                </p>
                <p>
                    <label>Max distance: {{ settings.maxDistance?.toFixed(0) }} m</label>
                    <input
                        type="range"
                        min="10"
                        max="2000"
                        step="10"
                        v-model.number="settings.maxDistance"
                        class="block w-full"
                    />
                </p>
                <p>
                    <label>Visible opacity: {{ settings.visibleColor?.a?.toFixed(2) }}</label>
                    <input
                        type="range"
                        min="0"
                        max="1"
                        step="0.01"
                        v-model.number="settings.visibleColor!.a"
                        class="block w-full"
                    />
                </p>
                <p>
                    <label>Hidden opacity: {{ settings.hiddenColor?.a?.toFixed(2) }}</label>
                    <input
                        type="range"
                        min="0"
                        max="1"
                        step="0.01"
                        v-model.number="settings.hiddenColor!.a"
                        class="block w-full"
                    />
                </p>
                <p>
                    <label>
                        <input type="checkbox" v-model="settings.enableWireframe" />
                        Wireframe
                    </label>
                </p>
                <p>
                    <label>
                        <input type="checkbox" v-model="settings.drawWireframeFromPosition" />
                        Draw wireframe from position
                    </label>
                </p>
                <p>
                    <FullscreenSource file="source/Viewshed.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer :messageHandlerIntervalMs="50" @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
