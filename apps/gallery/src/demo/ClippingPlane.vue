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
import { applyDefaultOrthoBaseLayer } from "@/utils/scenes";
import { MessageHandler } from "@/utils/messages";
import { ref, watch } from "vue";

type Direction = "X" | "-X" | "Y" | "-Y" | "Z";

const DIRECTIONS: Direction[] = ["X", "-X", "Y", "-Y", "Z"];

const CLIP_ID = 0;
const GIZMO_ID = 0;
const SQRT2_OVER_2 = Math.SQRT2 / 2;

const HOUSE_GEO: HrzProtocol.GeographicPosition.$Properties = {
    latitude: 48.158766,
    longitude: -1.678623,
    altitude: 4.0,
};

const NORMALS: Record<Direction, HrzProtocol.Vec3f.$Properties> = {
    X: { x: -1, y: 0, z: 0 },
    "-X": { x: 1, y: 0, z: 0 },
    Y: { x: 0, y: -1, z: 0 },
    "-Y": { x: 0, y: 1, z: 0 },
    Z: { x: 0, y: 0, z: -1 },
};

const GIZMO_ROTATIONS: Record<Direction, HrzProtocol.Quat.$Properties> = {
    X: { x: 0, y: SQRT2_OVER_2, z: 0, w: SQRT2_OVER_2 },
    "-X": { x: 0, y: -SQRT2_OVER_2, z: 0, w: SQRT2_OVER_2 },
    Y: { x: -SQRT2_OVER_2, y: 0, z: 0, w: SQRT2_OVER_2 },
    "-Y": { x: SQRT2_OVER_2, y: 0, z: 0, w: SQRT2_OVER_2 },
    Z: { x: 0, y: 0, z: 0, w: 1 },
};

const activeDirection = ref<Direction>("Z");
const gridVisible = ref(true);

let api: HrzApi.AsyncApi;
let clipHandle: HrzProtocol.LayerHandle | undefined;
let gizmoHandle: HrzProtocol.LayerHandle | undefined;

watch(activeDirection, async (dir) => {
    if (!api || !clipHandle || !gizmoHandle) return;

    HrzApi.ClippingPlaneLayerPathBuilder.create(clipHandle).normal().set(api, NORMALS[dir]);

    const gizmoPath = HrzApi.GizmoLayerPathBuilder.create(gizmoHandle);
    const gizmoState = await gizmoPath.clone().get(api);
    gizmoState.components = buildComponents();
    gizmoState.rotation = GIZMO_ROTATIONS[dir];
    gizmoPath.clone().set(api, gizmoState);
});

watch(gridVisible, (visible) => {
    if (!api || !clipHandle) return;
    HrzApi.ClippingPlaneLayerPathBuilder.create(clipHandle).showPlane().set(api, visible);
});

// Always AXIS_Z + OBJECT_REFERENCE — direction is encoded in the gizmo rotation.
function buildComponents(): HrzProtocol.GizmoComponent.$Properties[] {
    return [
        {
            type: HrzProtocol.GizmoComponentType.GIZMO_ORIGIN,
            referenceFrame: HrzProtocol.GizmoReferenceFrame.OBJECT_REFERENCE,
        },
        {
            type: HrzProtocol.GizmoComponentType.GIZMO_TRANSLATION_AXIS_Z,
            referenceFrame: HrzProtocol.GizmoReferenceFrame.OBJECT_REFERENCE,
        },
    ];
}

function handleGizmoUpdate(update: HrzProtocol.GizmoUpdateMessage.$Shape) {
    if (!update.geoPos || !clipHandle || !gizmoHandle) return;
    HrzApi.ClippingPlaneLayerPathBuilder.create(clipHandle)
        .originPosition()
        .set(api, update.geoPos);
    HrzApi.GizmoLayerPathBuilder.create(gizmoHandle).position().set(api, update.geoPos);
}

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler: MessageHandler) {
    api = api_;

    await applyDefaultOrthoBaseLayer(api);

    await api.CameraService.setFixedTarget({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        angularViewpoint: {
            target: HOUSE_GEO,
            bearing: 0,
            tilt: 0.8,
            distance: 30,
        },
        minTilt: 0,
        maxTilt: 1.5,
        minDistance: 1,
        maxDistance: 500,
        goToAnimation: { duration: 0 },
    });

    const modelHandle = await api.LayerService.createLayer({
        type: HrzProtocol.LayerType.SINGLE_MODEL,
    });
    await HrzApi.SingleModelLayerPathBuilder.create(modelHandle).set(api, {
        visible: true,
        clipId: CLIP_ID,
        sceneViews: { bits: 1 },
        url: "assets/demo/house_dynamic_materials/house.gltf",
        geographic: {
            latitude: 48.158766,
            longitude: -1.678623,
            altitude: 0.05,
        },
        color: { r: 1, g: 1, b: 1, a: 1 },
        transform: {
            offset: {},
            scale: { x: 1, y: 1, z: 1 },
            rotation: { x: 0, y: 0, z: 1, w: 0 },
            frame: {
                up: HrzProtocol.Axis.POS_Y,
                front: HrzProtocol.Axis.NEG_Z,
                handedness: HrzProtocol.Handedness.RIGHT,
            },
        },
        lighting: {
            enableLighting: true,
            castShadows: true,
            receiveShadows: true,
        },
    });

    clipHandle = await api.LayerService.createLayer({
        type: HrzProtocol.LayerType.CLIPPING_PLANE,
    });
    await HrzApi.ClippingPlaneLayerPathBuilder.create(clipHandle).set(api, {
        clipId: CLIP_ID,
        originPosition: { ...HOUSE_GEO },
        normal: NORMALS[activeDirection.value],
        outlineColor: { r: 0.9, g: 0.9, b: 0.9, a: 1 },
        outlineDistance: 0.15,
        showPlane: gridVisible.value,
        grid: {
            extent: 30,
            cellSize: 4,
            color: { r: 0.9, g: 0.9, b: 0.9, a: 0 },
            sceneViews: { bits: 1 },
        },
    });

    gizmoHandle = await api.LayerService.createLayer({
        type: HrzProtocol.LayerType.GIZMO,
    });
    await HrzApi.GizmoLayerPathBuilder.create(gizmoHandle).set(api, {
        visible: true,
        id: GIZMO_ID,
        size: 160,
        sizeUnit: HrzProtocol.UiSizeUnit.UI_SIZE_IN_PIXELS,
        bboxPadding: 0,
        position: { ...HOUSE_GEO },
        rotation: GIZMO_ROTATIONS[activeDirection.value],
        components: buildComponents(),
        sceneViews: { bits: 1 },
        grid: {},
        line: {
            extent: 50,
            width: 2,
            color: { r: 1, g: 1, b: 1, a: 1 },
            sceneViews: { bits: 1 },
        },
    });

    msgHandler.watch((msg) => {
        if (msg.payload == "gizmoUpdate" && msg.gizmoUpdate?.id === GIZMO_ID) {
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
                <h1>Clipping plane</h1>
                <p>
                    A clipping plane slices through the scene, hiding everything on one side. Drag
                    the gizmo handle to slide the plane along its normal direction and reveal the
                    interior of the house model.
                </p>
                <p>
                    The clipping plane is a dedicated <code>CLIPPING_PLANE</code> layer. It is
                    linked to target layers via a shared <code>clipId</code>. Only layers that
                    reference that ID are affected by the cut.
                </p>
                <p>
                    <label>Direction</label>
                    <span class="flex gap-1 flex-wrap mt-1">
                        <TabButton
                            v-for="d in DIRECTIONS"
                            :key="d"
                            :active="activeDirection === d"
                            @click="activeDirection = d"
                            >{{ d }}</TabButton
                        >
                    </span>
                </p>
                <p>
                    <label>
                        <input type="checkbox" v-model="gridVisible" />
                        Show clipping plane grid
                    </label>
                </p>
                <p>
                    <FullscreenSource file="source/ClippingPlane.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer :messageHandlerIntervalMs="50" @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
