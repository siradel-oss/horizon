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
import { applyDefaultOrthoBaseLayer, applySceneTemplate, getLayerByName } from "@/utils/scenes";
import { ref, watch } from "vue";

const mode = ref<"split" | "duplicate">("split");
const direction = ref<"vertical" | "horizontal">("vertical");
const position = ref(0.5);
const cameraSync = ref(true);

let api: HrzApi.AsyncApi;

async function updateViewports() {
    if (!api) return;
    const splits = [0, position.value, 1];
    const cameras = [
        HrzProtocol.CameraIndex.CAMERA_0,
        cameraSync.value ? HrzProtocol.CameraIndex.CAMERA_0 : HrzProtocol.CameraIndex.CAMERA_1,
    ];

    for (let i = 0; i < 2; i++) {
        const rect: HrzProtocol.Bbox.$Shape = { xMin: 0, xMax: 1, yMin: 0, yMax: 1 };
        if (direction.value === "horizontal") {
            rect.yMin = splits[i];
            rect.yMax = splits[i + 1];
        } else {
            rect.xMin = splits[i];
            rect.xMax = splits[i + 1];
        }

        const viewportSettings: HrzProtocol.ViewportSettings.$Shape =
            mode.value === "split"
                ? { viewport: { xMin: 0, xMax: 1, yMin: 0, yMax: 1 }, scissor: rect }
                : { viewport: rect, scissor: { xMin: 0, xMax: 1, yMin: 0, yMax: 1 } };

        await HrzApi.SceneViewSettingsPathBuilder.create(
            HrzProtocol.SceneViewIndex.SCENE_VIEW_0 + i
        )
            .viewport()
            .set(api, viewportSettings);

        await HrzApi.SceneViewSettingsPathBuilder.create(
            HrzProtocol.SceneViewIndex.SCENE_VIEW_0 + i
        )
            .camera()
            .set(api, cameras[i]);
    }
}

watch([mode, direction, cameraSync], () => updateViewports());
watch(position, () => updateViewports());

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;

    // Activate two scene views
    await HrzApi.SceneSettingsPathBuilder.create().set(api, {
        activeViews: { bits: 3 },
        mainView: HrzProtocol.SceneViewIndex.SCENE_VIEW_0,
    });

    // Rennes buildings template (sets CAMERA_0, adds buildings layer)
    await applySceneTemplate(api, "rennes_buildings");

    // Initialize CAMERA_1 to same viewpoint as CAMERA_0
    await api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_1,
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
        goToAnimation: { duration: 0 },
    });

    // Ortho base layer for view 0
    const orthoHandle = await applyDefaultOrthoBaseLayer(api);
    const orthoModel = await HrzApi.ImageryRasterLayerPathBuilder.create(orthoHandle).get(api);
    orthoModel.sceneViews = { bits: 1 };
    await HrzApi.ImageryRasterLayerPathBuilder.create(orthoHandle).set(api, orthoModel);

    // Symbolic base layer for view 1 — terrain color set on SCENE_VIEW_1
    HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_1)
        .terrain()
        .terrainColor()
        .set(api, { r: 0.667, g: 0.835, b: 0.914, a: 1 });
    await applySceneTemplate(api, "plan_ign");
    const planIgnLayer = await getLayerByName(api, "Plan IGN");
    const planIgnHandle = HrzProtocol.LayerHandle.create(planIgnLayer);
    const planIgnModel = await HrzApi.ImageryRasterLayerPathBuilder.create(planIgnHandle).get(api);
    planIgnModel.sceneViews = { bits: 2 };
    await HrzApi.ImageryRasterLayerPathBuilder.create(planIgnHandle).set(api, planIgnModel);

    // Style the buildings layer with two per-view representations
    const buildingsLayer = await getLayerByName(api, "Rennes buildings");
    const buildingsHandle = HrzProtocol.LayerHandle.create(buildingsLayer);
    const existingStyle = await HrzApi.VectorTilesLayerPathBuilder.create(buildingsHandle)
        .style()
        .get(api);

    await HrzApi.VectorTilesLayerPathBuilder.create(buildingsHandle)
        .style()
        .set(api, {
            ...existingStyle,
            stylingScript: `set "extrusion" = attr("height");
fork { emit "r0"; }
set "color" = colorize("height_palette", attr("height"));
set "bottom_color" = darken(prp("color"), 0.4);
emit "r1";`,
            palettes: [
                {
                    name: "height_palette",
                    numeric: {
                        nanColor: { r: 0.6, g: 0.6, b: 0.6, a: 1 },
                        colorStops: [
                            {
                                firstColor: { r: 104 / 255, g: 177 / 255, b: 246 / 255, a: 1 },
                                secondColor: { r: 104 / 255, g: 177 / 255, b: 246 / 255, a: 1 },
                            },
                            {
                                value: 20,
                                firstColor: { r: 104 / 255, g: 210 / 255, b: 130 / 255, a: 1 },
                                secondColor: { r: 235 / 255, g: 210 / 255, b: 80 / 255, a: 1 },
                            },
                            {
                                value: 50,
                                firstColor: { r: 235 / 255, g: 210 / 255, b: 80 / 255, a: 1 },
                                secondColor: { r: 235 / 255, g: 110 / 255, b: 70 / 255, a: 1 },
                            },
                        ],
                    },
                },
            ],
            representations: [
                {
                    id: 0,
                    name: "r0",
                    sceneViews: { bits: 1 },
                    extrudedGeometry: {
                        lighting: {
                            enableLighting: true,
                            castShadows: true,
                            receiveShadows: true,
                        },
                        extrusion: { name: "extrusion", defaultValue: 0 },
                        altitudeOffset: { name: "altitude_offset", defaultValue: 1 },
                        upperColor: {
                            name: "upper_color",
                            defaultValue: { r: 0.78, g: 0.78, b: 0.78, a: 1 },
                        },
                        lowerColor: {
                            name: "lower_color",
                            defaultValue: { r: 0.38, g: 0.38, b: 0.38, a: 1 },
                        },
                        roofColor: {
                            name: "upper_color",
                            defaultValue: { r: 0.85, g: 0.85, b: 0.85, a: 1 },
                        },
                    },
                },
                {
                    id: 1,
                    name: "r1",
                    sceneViews: { bits: 2 },
                    extrudedGeometry: {
                        lighting: {
                            enableLighting: true,
                            castShadows: true,
                            receiveShadows: true,
                        },
                        extrusion: { name: "extrusion", defaultValue: 0 },
                        altitudeOffset: { name: "altitude_offset", defaultValue: 1 },
                        upperColor: {
                            name: "color",
                            defaultValue: { r: 0.5, g: 0.5, b: 0.5, a: 1 },
                        },
                        lowerColor: {
                            name: "bottom_color",
                            defaultValue: { r: 0.3, g: 0.3, b: 0.3, a: 1 },
                        },
                        roofColor: {
                            name: "color",
                            defaultValue: { r: 0.5, g: 0.5, b: 0.5, a: 1 },
                        },
                    },
                },
            ],
        });

    await updateViewports();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Multiview</h1>
                <p>
                    Two scene views rendered side by side on the same canvas. The left view uses the
                    ortho raster and neutral gray buildings; the right uses the symbolic raster and
                    buildings coloured by height.
                </p>
                <p>
                    <label>Mode</label>
                    <select v-model="mode" class="block w-full mt-1">
                        <option value="split">Split</option>
                        <option value="duplicate">Duplicate</option>
                    </select>
                </p>
                <p>
                    <label>Direction</label>
                    <select v-model="direction" class="block w-full mt-1">
                        <option value="vertical">Vertical</option>
                        <option value="horizontal">Horizontal</option>
                    </select>
                </p>
                <p>
                    <label>Position: {{ (position * 100).toFixed(0) }} %</label>
                    <input
                        type="range"
                        min="0.05"
                        max="0.95"
                        step="0.01"
                        v-model.number="position"
                        class="block w-full"
                    />
                </p>
                <p>
                    <label>Camera</label>
                    <select v-model="cameraSync" class="block w-full mt-1">
                        <option :value="true">Synchronised</option>
                        <option :value="false">Independent</option>
                    </select>
                </p>
                <p>
                    <FullscreenSource file="source/Multiview.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" :withCameraControls="false" />
        </template>
    </SplitView>
</template>
