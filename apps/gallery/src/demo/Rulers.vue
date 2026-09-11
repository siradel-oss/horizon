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
import { applyDefaultSymbolicBaseLayer } from "@/utils/scenes";
import { MessageHandler } from "@/utils/messages";
import { ref, watch } from "vue";

// ── Reactive state ──────────────────────────────────────────────────────────

type Tool = "distance" | "area";
type LineKind = "geodesic" | "rhumb";
type EditorMode = "append" | "selection";

const tool = ref<Tool>("distance");
const lineKind = ref<LineKind>("geodesic");
const editorMode = ref<EditorMode>("append");

const pointCount = ref(0);
const length = ref(0); // metres — total length (polyline) or perimeter (polygon)
const area = ref(0); // square metres

// ── API handles ─────────────────────────────────────────────────────────────

let api: HrzApi.AsyncApi;
let shapeHandle: HrzProtocol.LayerHandle;

// ── Helpers ──────────────────────────────────────────────────────────────────

function toGeometryType(t: Tool) {
    return t === "distance"
        ? HrzProtocol.EditableShapeGeometryType.EDITABLE_POLYLINE
        : HrzProtocol.EditableShapeGeometryType.EDITABLE_POLYGON;
}

function toLineType(k: LineKind) {
    return k === "geodesic"
        ? HrzProtocol.EditableShapeLineType.EDITABLE_SHAPE_LINE_GEODESIC
        : HrzProtocol.EditableShapeLineType.EDITABLE_SHAPE_LINE_RHUMB_LINE;
}

function formatLength(m: number): string {
    if (m === 0) return "—";
    if (m < 1000) return `${m.toFixed(0)} m`;
    if (m < 100_000) return `${(m / 1000).toFixed(2)} km`;
    return `${(m / 1000).toFixed(0)} km`;
}

function formatArea(sqm: number): string {
    if (sqm === 0) return "—";
    if (sqm < 10_000) return `${sqm.toFixed(0)} m²`;
    if (sqm < 1_000_000) return `${(sqm / 10_000).toFixed(2)} ha`;
    return `${(sqm / 1_000_000).toFixed(2)} km²`;
}

// ── Shape management ─────────────────────────────────────────────────────────

async function clearShape() {
    if (!api) return;
    pointCount.value = 0;
    length.value = 0;
    area.value = 0;
    await HrzApi.EditableShapeLayerPathBuilder.create(shapeHandle)
        .geometry()
        .set(api, {
            type: toGeometryType(tool.value),
            lineType: toLineType(lineKind.value),
            coords: [],
            linestringSizes: [],
        });
    await api.ShapeEditorService.selectShape({ layer: shapeHandle });
    await api.ShapeEditorService.selectMode({
        mode: HrzProtocol.ShapeEditorMode.APPEND_MODE,
    });
    editorMode.value = "append";
}

// ── Watchers ─────────────────────────────────────────────────────────────────

watch(tool, async () => {
    if (!api) return;
    await clearShape();
});

watch(lineKind, async (k) => {
    if (!api) return;
    await HrzApi.EditableShapeLayerPathBuilder.create(shapeHandle)
        .geometry()
        .lineType()
        .set(api, toLineType(k));
});

watch(editorMode, async (m) => {
    if (!api) return;
    await api.ShapeEditorService.selectMode({
        mode:
            m === "append"
                ? HrzProtocol.ShapeEditorMode.APPEND_MODE
                : HrzProtocol.ShapeEditorMode.SELECTION_MODE,
    });
});

// ── Viewer init ───────────────────────────────────────────────────────────────

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler: MessageHandler) {
    api = api_;

    await applyDefaultSymbolicBaseLayer(api);

    await api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        bounds: {
            bounds: { west: -5.5, south: 41.3, east: 9.6, north: 51.2 },
        },
        altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
        maxAltitude: 1e7,
        minTilt: 0,
        maxTilt: Math.PI,
        goToAnimation: { duration: 0 },
        correctionAnimation: { duration: 0 },
    });

    // Create shape layer
    shapeHandle = await api.LayerService.createLayer({
        type: HrzProtocol.LayerType.EDITABLE_SHAPE,
    });

    await HrzApi.EditableShapeLayerPathBuilder.create(shapeHandle).set(api, {
        geometry: {
            type: HrzProtocol.EditableShapeGeometryType.EDITABLE_POLYLINE,
            lineType: HrzProtocol.EditableShapeLineType.EDITABLE_SHAPE_LINE_GEODESIC,
            coords: [],
        },
        strokeColor: { r: 0.18, g: 0.53, b: 0.98, a: 1 },
        fillColor: { r: 0.18, g: 0.53, b: 0.98, a: 0.2 },
        selectedStrokeColor: { r: 0.18, g: 0.53, b: 0.98, a: 1 },
        selectedFillColor: { r: 0.18, g: 0.53, b: 0.98, a: 0.2 },
        strokeWidth: 3,
        selectedStrokeWidth: 3,
        controlPointSize: 8,
        controlPointColor: { r: 1, g: 1, b: 1, a: 1 },
        midpointControlPointColor: { r: 0.7, g: 0.85, b: 1.0, a: 0.8 },
        selectedControlPointColor: { r: 1, g: 0.8, b: 0, a: 1 },
        showMidpointControlPoints: true,
        zIndex: 0,
        sceneViews: { bits: 1 },
        visible: true,
        modelUpdateFrequency: HrzProtocol.EditableShapeModelUpdateFrequency.APPEND_MODEL_UPDATES,
    });

    // Listen for shape geometry updates
    msgHandler.watchForever(async (msg) => {
        if (msg.payload != "shapeEditorUpdate") return;

        const update = msg.shapeEditorUpdate;

        if (update.payload == "shapeGeometryUpdate") {
            const [geometry, info] = await Promise.all([
                HrzApi.EditableShapeLayerPathBuilder.create(shapeHandle).geometry().get(api),
                api.ShapeEditorService.getShapeInformation(shapeHandle),
            ]);
            pointCount.value = geometry.coords.length / 2;
            length.value = info.length;
            area.value = info.area;
        }

        if (update.payload == "modeSwitch") {
            editorMode.value =
                update.modeSwitch === HrzProtocol.ShapeEditorMode.APPEND_MODE
                    ? "append"
                    : "selection";
        }
    });

    // Select shape and start in append mode
    await api.ShapeEditorService.selectShape({ layer: shapeHandle });
    await api.ShapeEditorService.lockShapeSelection();
    await api.ShapeEditorService.selectMode({
        mode: HrzProtocol.ShapeEditorMode.APPEND_MODE,
    });
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Rulers</h1>
                <p>
                    Click on the map to add measurement points. Switch to <strong>Edit</strong> mode
                    to drag existing points. Press <kbd>Delete</kbd> to remove a selected point.
                </p>

                <!-- Live measurement display -->
                <div class="measurement-card mt-2 mb-2">
                    <template v-if="tool === 'distance'">
                        <p class="measurement-value">{{ formatLength(length) }}</p>
                        <p class="measurement-label">
                            {{
                                pointCount <= 1
                                    ? "Add at least 2 points to measure"
                                    : `${pointCount} points`
                            }}
                        </p>
                    </template>
                    <template v-else>
                        <p class="measurement-value">{{ formatArea(area) }}</p>
                        <p class="measurement-label">
                            <template v-if="pointCount < 3">Add at least 3 points</template>
                            <template v-else>Perimeter: {{ formatLength(length) }}</template>
                        </p>
                    </template>
                </div>

                <p>
                    <label>Tool</label>
                    <select v-model="tool" class="block w-full mt-1">
                        <option value="distance">Distance</option>
                        <option value="area">Area</option>
                    </select>
                </p>
                <p>
                    <label>Line type</label>
                    <select v-model="lineKind" class="block w-full mt-1">
                        <option value="geodesic">Geodesic (great circle)</option>
                        <option value="rhumb">Rhumb line (constant bearing)</option>
                    </select>
                </p>
                <p>
                    <label>Mode</label>
                    <select v-model="editorMode" class="block w-full mt-1">
                        <option value="append">Add points</option>
                        <option value="selection">Edit points</option>
                    </select>
                </p>
                <p>
                    <button class="clear-btn" @click="clearShape">Clear</button>
                </p>
                <p>
                    <FullscreenSource file="source/Rulers.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer
                :messageHandlerIntervalMs="50"
                :viewerOptions="{
                    keyBindings: {
                        bindings: [
                            {
                                key: HrzProtocol.Key.K_DELETE,
                                action: HrzProtocol.KeyAction.EDITOR_DELETE_SELECTED_POINT,
                            },
                        ],
                    },
                }"
                @ready="onHorizonReady"
            />
        </template>
    </SplitView>
</template>

<style scoped>
@reference "../style.css";

.measurement-card {
    @apply rounded-xl bg-secondaryContainer text-onSecondaryContainer p-4 text-center;
}

.measurement-value {
    @apply text-3xl font-bold tracking-tight;
}

.measurement-label {
    @apply text-sm opacity-70 mt-1;
}

.clear-btn {
    @apply h-10 px-4 rounded-lg
           bg-secondaryContainer text-onSecondaryContainer
           text-mLabelLarge select-none
           transition-shadow hover:shadow-md active:brightness-90;
}
</style>
