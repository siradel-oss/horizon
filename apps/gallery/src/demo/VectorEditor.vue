<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol, HrzProtocolHelper } from "@siradel-oss/horizon-protocol";
import { applyDefaultSymbolicBaseLayer } from "@/utils/scenes";
import { MessageHandler } from "@/utils/messages";
import { eqLong, debounce } from "@/utils/utils";
import { ref, computed, watch } from "vue";

// ── Types ─────────────────────────────────────────────────────────────────────

interface FeatureData {
    id: number;
    height: number;
    color: string; // "#RRGGBB"
    coords: number[]; // 2D: [lon, lat, lon, lat, ...]
}

type AppMode = "idle" | "selected" | "editing" | "adding";

// ── Constants ─────────────────────────────────────────────────────────────────

const IN_MEMORY_LAYER_ID = 1;
const VECTOR_DATA_LAYER_ID = 2;

const FEATURE_ID_ATTR = 1;
const HEIGHT_ATTR = 2;
const COLOR_ATTR = 3;

const COLOR_PALETTE = [
    "#E74C3C",
    "#E67E22",
    "#F1C40F",
    "#2ECC71",
    "#3498DB",
    "#9B59B6",
    "#1ABC9C",
    "#E91E63",
];

function randomColor() {
    return COLOR_PALETTE[Math.floor(Math.random() * COLOR_PALETTE.length)];
}

function randomHeight() {
    return Math.floor(Math.random() * 36) + 5; // 5 – 40 m
}

const BASE_STYLING_SCRIPT = `\
set "extrusion" = attr("height");
set "color"     = attr("color");
set "bot_color" = darken(prp("color"), 0.4);
emit "buildings";`;

// ~50 m squares around Rennes centre (48.1073 °N, 1.6777 °W).
// Vertices listed counter-clockwise as required by the engine.
const INITIAL_FEATURES: FeatureData[] = [
    {
        id: 1,
        height: 12,
        color: "#4B90F0",
        coords: [-1.6795, 48.1075, -1.6795, 48.108, -1.6788, 48.108, -1.6788, 48.1075],
    },
    {
        id: 2,
        height: 25,
        color: "#5CBC8A",
        coords: [-1.6782, 48.1075, -1.6782, 48.108, -1.6775, 48.108, -1.6775, 48.1075],
    },
    {
        id: 3,
        height: 8,
        color: "#F0A040",
        coords: [-1.6795, 48.1068, -1.6795, 48.1073, -1.6788, 48.1073, -1.6788, 48.1068],
    },
    {
        id: 4,
        height: 40,
        color: "#A070E0",
        coords: [-1.6782, 48.1068, -1.6782, 48.1073, -1.6775, 48.1073, -1.6775, 48.1068],
    },
    {
        id: 5,
        height: 18,
        color: "#E05060",
        coords: [-1.6769, 48.1071, -1.6769, 48.1076, -1.6762, 48.1076, -1.6762, 48.1071],
    },
];

// ── Reactive state ────────────────────────────────────────────────────────────

const appMode = ref<AppMode>("idle");
const features = ref<FeatureData[]>(
    INITIAL_FEATURES.map((f) => ({
        ...f,
        coords: [...f.coords],
    }))
);
const selectedId = ref<number | null>(null);
const editHeight = ref(10);
const editColor = ref("#4B90F0");

const selectedFeature = computed(
    () => features.value.find((f) => f.id === selectedId.value) ?? null
);

// ── API handles ───────────────────────────────────────────────────────────────

let api: HrzApi.AsyncApi;
let msgHandler: MessageHandler;
let inMemoryLayerHandle: HrzProtocol.LayerHandle;
let vectorDataLayerHandle: HrzProtocol.LayerHandle;
let vectorTilesLayerHandle: HrzProtocol.LayerHandle;
let shapeLayerHandle: HrzProtocol.LayerHandle | null = null;

// ── Helpers ───────────────────────────────────────────────────────────────────

/** Convert [lon, lat, lon, lat, ...] → [lat, lon, lat, lon, ...] for EditableShapeLayer. */
function coordsToShape(coords: number[]): number[] {
    const out: number[] = [];
    for (let i = 0; i < coords.length; i += 2) out.push(coords[i + 1], coords[i]);
    return out;
}

/** Convert [lat, lon, lat, lon, ...] → [lon, lat, lon, lat, ...] back to feature storage. */
function coordsFromShape(coords: number[]): number[] {
    const out: number[] = [];
    for (let i = 0; i < coords.length; i += 2) out.push(coords[i + 1], coords[i]);
    return out;
}

function featureToInMemory(f: FeatureData): HrzProtocol.InMemoryVectorFeature {
    const coords3d: number[] = [];
    for (let i = 0; i < f.coords.length; i += 2) coords3d.push(f.coords[i], f.coords[i + 1], 0);

    return HrzProtocol.InMemoryVectorFeature.create({
        geometry: {
            type: HrzProtocol.VectorGeometryType.POLYGON_GEOMETRY,
            coords: coords3d,
        },
        attributeValues: [
            { numberValue: f.id },
            { numberValue: f.height },
            { stringValue: f.color },
        ],
    });
}

// ── Viewer init ───────────────────────────────────────────────────────────────

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler_: MessageHandler) {
    api = api_;
    msgHandler = msgHandler_;

    await applyDefaultSymbolicBaseLayer(api);

    await api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        angularViewpoint: {
            target: { latitude: 48.1073, longitude: -1.6777, altitude: 0 },
            bearing: 0.37,
            tilt: 0.9,
            distance: 600,
        },
        altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
        maxAltitude: 1e6,
        minTilt: 0,
        maxTilt: Math.PI,
        goToAnimation: { duration: 0 },
        correctionAnimation: { duration: 0 },
    });

    // 1. InMemoryVectorSourceLayer — stores the editable polygon features
    inMemoryLayerHandle = await api.LayerService.createLayer({
        type: HrzProtocol.LayerType.IN_MEMORY_VECTOR_SOURCE,
    });
    await HrzApi.InMemoryVectorSourceLayerPathBuilder.create(inMemoryLayerHandle).set(api, {
        id: IN_MEMORY_LAYER_ID,
        projection: {
            descriptorType: HrzProtocol.SrsDescriptorType.SRID_DESCRIPTOR,
            descriptor: "EPSG:4326",
        },
        attributes: [
            {
                id: FEATURE_ID_ATTR,
                isFeatureId: true,
                transform: HrzProtocol.AttributeTransform.ATTRIBUTE_TRANSFORM_TO_INT,
            },
            {
                id: HEIGHT_ATTR,
                transform: HrzProtocol.AttributeTransform.ATTRIBUTE_TRANSFORM_TO_NUMBER,
            },
            {
                id: COLOR_ATTR,
                transform: HrzProtocol.AttributeTransform.ATTRIBUTE_TRANSFORM_TO_COLOR,
            },
        ],
        features: features.value.map((f) => featureToInMemory(f)),
    });

    // 2. VectorDataLayer — non-visual data provider referencing the in-memory source
    vectorDataLayerHandle = await api.LayerService.createLayer({
        type: HrzProtocol.LayerType.VECTOR_DATA,
    });
    await HrzApi.VectorDataLayerPathBuilder.create(vectorDataLayerHandle).set(api, {
        id: VECTOR_DATA_LAYER_ID,
        sources: [
            {
                hasGeometry: true,
                inMemoryDataProvider: { inMemoryLayerId: IN_MEMORY_LAYER_ID },
                attributes: [
                    { id: FEATURE_ID_ATTR, isFeatureId: true },
                    { id: HEIGHT_ATTR },
                    { id: COLOR_ATTR },
                ],
            },
        ],
    });

    // 3. VectorTilesLayer — visual rendering with extruded buildings coloured by attribute
    vectorTilesLayerHandle = await api.LayerService.createLayer({
        type: HrzProtocol.LayerType.VECTOR_TILES,
    });
    await HrzApi.VectorTilesLayerPathBuilder.create(vectorTilesLayerHandle).set(api, {
        source: { vectorDataLayerId: VECTOR_DATA_LAYER_ID },
        visible: true,
        missingTilePolicy: HrzProtocol.MissingTilePolicy.USE_EMPTY_TILE,
        sceneViews: { bits: 1 },
        resolution: {
            maxScreenSpaceError: 4,
        },
        lighting: {
            enableLighting: true,
            castShadows: true,
            receiveShadows: true,
        },
        style: {
            attributes: [
                { stylingName: "id", vectorDataAttrId: FEATURE_ID_ATTR },
                { stylingName: "height", vectorDataAttrId: HEIGHT_ATTR },
                { stylingName: "color", vectorDataAttrId: COLOR_ATTR },
            ],
            stylingScript: BASE_STYLING_SCRIPT,
            representations: [
                {
                    id: 0,
                    name: "buildings",
                    sceneViews: { bits: 1 },
                    extrudedGeometry: {
                        lighting: {
                            enableLighting: true,
                            castShadows: true,
                            receiveShadows: true,
                        },
                        extrusion: { name: "extrusion", defaultValue: 5 },
                        altitudeOffset: { name: "altitude_offset", defaultValue: 0 },
                        upperColor: {
                            name: "color",
                            defaultValue: { r: 0.5, g: 0.5, b: 0.5, a: 1 },
                        },
                        lowerColor: {
                            name: "bot_color",
                            defaultValue: { r: 0.3, g: 0.3, b: 0.3, a: 1 },
                        },
                        roofColor: {
                            name: "color",
                            defaultValue: { r: 0.5, g: 0.5, b: 0.5, a: 1 },
                        },
                    },
                },
            ],
        },
    });
}

// ── Picking ───────────────────────────────────────────────────────────────────

async function onClickAt(x: number, y: number) {
    if (!api || appMode.value === "editing" || appMode.value === "adding") return;

    const result = await api.ViewerService.pickScreen({ coords: { x, y }, includedRasters: [] });
    if (!result.hasATicket || !result.ticket) return;

    msgHandler.awaitPickResult(result.ticket, (pickResult) => {
        for (const r of pickResult.results ?? []) {
            if (eqLong(r.layer?.handle?.opaque, vectorTilesLayerHandle.opaque)) {
                const idAttr = r.vector?.featureId?.attributes?.[0]?.value;
                if (idAttr != null) {
                    const id = HrzProtocolHelper.attributeAsNumber(idAttr);
                    if (id != null) {
                        selectFeature(id);
                        return;
                    }
                }
            }
        }
        deselectFeature();
    });
}

function selectFeature(id: number) {
    const f = features.value.find((f) => f.id === id);
    if (!f) return;

    // Set selectedId before editHeight/editColor so the property watchers
    // correctly identify which feature to update.
    selectedId.value = id;
    editHeight.value = f.height;
    editColor.value = f.color;
    appMode.value = "selected";

    api.ViewerService.deselectAllFeatures();
    api.ViewerService.selectFeatures({
        features: [
            {
                layer: vectorTilesLayerHandle,
                featureId: {
                    attributes: [{ id: FEATURE_ID_ATTR, value: { numberValue: id } }],
                },
            },
        ],
    });
}

function deselectFeature() {
    selectedId.value = null;
    appMode.value = "idle";
    api.ViewerService.deselectAllFeatures();
}

// ── Property editing (live updates) ──────────────────────────────────────────

const updateFeatureProps = debounce(async () => {
    if (appMode.value !== "selected") return;
    const f = selectedFeature.value;
    if (!f) return;

    f.height = editHeight.value;
    f.color = editColor.value;

    const idx = features.value.indexOf(f);
    await HrzApi.InMemoryVectorSourceLayerPathBuilder.create(inMemoryLayerHandle)
        .features(idx)
        .set(api, featureToInMemory(f));
}, 150);

watch(editHeight, updateFeatureProps);
watch(editColor, updateFeatureProps);

// ── Geometry editing ──────────────────────────────────────────────────────────

async function enterGeometryEdit() {
    const f = selectedFeature.value;
    if (!f) return;
    const idx = features.value.indexOf(f);

    // Hide the feature by patching the styling script to discard its ID.
    await HrzApi.VectorTilesLayerPathBuilder.create(vectorTilesLayerHandle)
        .style()
        .stylingScript()
        .set(api, `if (attr("id") == ${f.id}) { discard; }\n${BASE_STYLING_SCRIPT}`);

    // Create a temporary EditableShapeLayer pre-loaded with the feature geometry.
    // Rhumb-line segments match the segment type used by the vector data layer.
    shapeLayerHandle = await api.LayerService.createLayer({
        type: HrzProtocol.LayerType.EDITABLE_SHAPE,
    });
    await HrzApi.EditableShapeLayerPathBuilder.create(shapeLayerHandle).set(api, {
        geometry: {
            type: HrzProtocol.EditableShapeGeometryType.EDITABLE_POLYGON,
            lineType: HrzProtocol.EditableShapeLineType.EDITABLE_SHAPE_LINE_RHUMB_LINE,
            coords: coordsToShape(f.coords),
        },
        strokeColor: { r: 0.18, g: 0.53, b: 0.98, a: 1.0 },
        fillColor: { r: 0.18, g: 0.53, b: 0.98, a: 0.2 },
        selectedStrokeColor: { r: 0.18, g: 0.53, b: 0.98, a: 1.0 },
        selectedFillColor: { r: 0.18, g: 0.53, b: 0.98, a: 0.2 },
        strokeWidth: 3,
        selectedStrokeWidth: 3,
        controlPointSize: 8,
        controlPointColor: { r: 1.0, g: 1.0, b: 1.0, a: 1.0 },
        midpointControlPointColor: { r: 0.7, g: 0.85, b: 1.0, a: 0.8 },
        selectedControlPointColor: { r: 1.0, g: 0.8, b: 0.0, a: 1.0 },
        showMidpointControlPoints: true,
        zIndex: 0,
        sceneViews: { bits: 1 },
        visible: true,
        modelUpdateFrequency: HrzProtocol.EditableShapeModelUpdateFrequency.DEFAULT_MODEL_UPDATES,
    });

    // Start in selection mode so the user can immediately drag existing vertices.
    await api.ShapeEditorService.selectShape({ layer: shapeLayerHandle });
    await api.ShapeEditorService.lockShapeSelection();
    await api.ShapeEditorService.selectMode({
        mode: HrzProtocol.ShapeEditorMode.SELECTION_MODE,
    });

    appMode.value = "editing";
}

async function commitGeometry() {
    const f = selectedFeature.value;
    if (!f || !shapeLayerHandle) return;

    const geom = await HrzApi.EditableShapeLayerPathBuilder.create(shapeLayerHandle)
        .geometry()
        .get(api);

    // Store the edited coords back into the client feature, swapping lat/lon back.
    f.coords = coordsFromShape(geom.coords);

    const idx = features.value.indexOf(f);
    await Promise.all([
        // Write new geometry back to the in-memory layer.
        HrzApi.InMemoryVectorSourceLayerPathBuilder.create(inMemoryLayerHandle)
            .features(idx)
            .set(api, featureToInMemory(f)),
        // Restore the styling script so the feature is visible again.
        HrzApi.VectorTilesLayerPathBuilder.create(vectorTilesLayerHandle)
            .style()
            .stylingScript()
            .set(api, BASE_STYLING_SCRIPT),
    ]);

    await destroyShapeLayer();
    appMode.value = "selected";
}

async function cancelGeometryEdit() {
    const f = selectedFeature.value;
    if (!f || !shapeLayerHandle) return;

    // Restore the styling script — no geometry write needed since nothing changed.
    await HrzApi.VectorTilesLayerPathBuilder.create(vectorTilesLayerHandle)
        .style()
        .stylingScript()
        .set(api, BASE_STYLING_SCRIPT);

    await destroyShapeLayer();
    appMode.value = "selected";
}

async function destroyShapeLayer() {
    if (!shapeLayerHandle) return;
    await api.ShapeEditorService.selectShape({}); // deselect before destroy
    await api.LayerService.destroyLayer(shapeLayerHandle);
    shapeLayerHandle = null;
}

// ── Add feature ──────────────────────────────────────────────────────────────

async function startAddFeature() {
    deselectFeature();

    shapeLayerHandle = await api.LayerService.createLayer({
        type: HrzProtocol.LayerType.EDITABLE_SHAPE,
    });
    await HrzApi.EditableShapeLayerPathBuilder.create(shapeLayerHandle).set(api, {
        geometry: {
            type: HrzProtocol.EditableShapeGeometryType.EDITABLE_POLYGON,
            lineType: HrzProtocol.EditableShapeLineType.EDITABLE_SHAPE_LINE_RHUMB_LINE,
            coords: [],
            linestringSizes: [],
        },
        strokeColor: { r: 0.18, g: 0.53, b: 0.98, a: 1.0 },
        fillColor: { r: 0.18, g: 0.53, b: 0.98, a: 0.2 },
        selectedStrokeColor: { r: 0.18, g: 0.53, b: 0.98, a: 1.0 },
        selectedFillColor: { r: 0.18, g: 0.53, b: 0.98, a: 0.2 },
        strokeWidth: 3,
        selectedStrokeWidth: 3,
        controlPointSize: 8,
        controlPointColor: { r: 1.0, g: 1.0, b: 1.0, a: 1.0 },
        midpointControlPointColor: { r: 0.7, g: 0.85, b: 1.0, a: 0.8 },
        selectedControlPointColor: { r: 1.0, g: 0.8, b: 0.0, a: 1.0 },
        showMidpointControlPoints: true,
        zIndex: 0,
        sceneViews: { bits: 1 },
        visible: true,
        modelUpdateFrequency: HrzProtocol.EditableShapeModelUpdateFrequency.DEFAULT_MODEL_UPDATES,
    });

    await api.ShapeEditorService.selectShape({ layer: shapeLayerHandle });
    await api.ShapeEditorService.lockShapeSelection();
    await api.ShapeEditorService.selectMode({
        mode: HrzProtocol.ShapeEditorMode.APPEND_MODE,
    });

    appMode.value = "adding";
}

async function commitNewFeature() {
    if (!shapeLayerHandle) return;

    const geom = await HrzApi.EditableShapeLayerPathBuilder.create(shapeLayerHandle)
        .geometry()
        .get(api);

    if (geom.coords.length < 6) {
        // fewer than 3 vertices — nothing to save
        await cancelNewFeature();
        return;
    }

    const newId = Math.max(0, ...features.value.map((f) => f.id)) + 1;
    const newFeature: FeatureData = {
        id: newId,
        height: randomHeight(),
        color: randomColor(),
        coords: coordsFromShape(geom.coords),
    };

    features.value.push(newFeature);
    await HrzApi.InMemoryVectorSourceLayerPathBuilder.create(inMemoryLayerHandle).addFeatures(
        api,
        featureToInMemory(newFeature)
    );

    await destroyShapeLayer();
    selectFeature(newId);
}

async function cancelNewFeature() {
    await destroyShapeLayer();
    appMode.value = "idle";
}

// ── Delete ────────────────────────────────────────────────────────────────────

async function deleteFeature() {
    const f = selectedFeature.value;
    if (!f) return;
    const idx = features.value.indexOf(f);

    await HrzApi.InMemoryVectorSourceLayerPathBuilder.create(inMemoryLayerHandle).removeFeatures(
        api,
        idx
    );
    features.value.splice(idx, 1);

    deselectFeature();
}
</script>

<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Vector editor</h1>
                <p>
                    Click a building to select it. Edit its height or colour directly, or use the
                    shape editor to reshape its footprint. Press <kbd>Delete</kbd> to remove a
                    selected control point.
                </p>

                <!-- Feature list ───────────────────────────────────────────── -->
                <div class="feature-list mt-3">
                    <div
                        v-for="f in features"
                        :key="f.id"
                        class="feature-item"
                        :class="{ selected: f.id === selectedId }"
                        @click="selectFeature(f.id)"
                    >
                        <span class="color-dot" :style="{ background: f.color }"></span>
                        <span class="feature-name">Building {{ f.id }}</span>
                        <span class="feature-height">{{ f.height }} m</span>
                    </div>
                </div>

                <!-- Add feature button ──────────────────────────────────────── -->
                <p v-if="appMode !== 'editing' && appMode !== 'adding'" class="mt-3">
                    <button class="action-btn w-full" @click="startAddFeature">
                        + Add feature
                    </button>
                </p>

                <!-- Properties panel (selected / editing) ───────────────────── -->
                <template v-if="appMode === 'selected' || appMode === 'editing'">
                    <div class="props-section mt-4">
                        <p>
                            <label>Height</label>
                            <span class="flex items-center gap-2 mt-1">
                                <input
                                    type="range"
                                    min="1"
                                    max="100"
                                    step="1"
                                    v-model.number="editHeight"
                                    :disabled="appMode === 'editing'"
                                    class="flex-1"
                                />
                                <span class="w-12 text-right">{{ editHeight }} m</span>
                            </span>
                        </p>
                        <p>
                            <label>Colour</label>
                            <input
                                type="color"
                                v-model="editColor"
                                :disabled="appMode === 'editing'"
                                class="block mt-1 h-9 w-full cursor-pointer rounded"
                            />
                        </p>

                        <!-- Normal selection mode -->
                        <template v-if="appMode === 'selected'">
                            <p class="flex gap-2 mt-3">
                                <button class="action-btn" @click="enterGeometryEdit">
                                    Edit geometry
                                </button>
                                <button class="action-btn danger" @click="deleteFeature">
                                    Delete
                                </button>
                            </p>
                        </template>

                        <!-- Shape editor active (editing existing) -->
                        <template v-else-if="appMode === 'editing'">
                            <p class="hint mt-2">
                                Drag control points to reshape the footprint. Click a midpoint
                                handle to insert a new vertex.
                            </p>
                            <p class="flex gap-2 mt-3">
                                <button class="action-btn primary" @click="commitGeometry">
                                    Save feature
                                </button>
                                <button class="action-btn" @click="cancelGeometryEdit">
                                    Cancel
                                </button>
                            </p>
                        </template>
                    </div>
                </template>

                <!-- Add mode panel ──────────────────────────────────────────── -->
                <template v-else-if="appMode === 'adding'">
                    <div class="props-section mt-4">
                        <p class="hint">
                            Click on the map to place vertices.<br />
                            Right-click to close the shape, then confirm below.
                        </p>
                        <p class="flex gap-2 mt-3">
                            <button class="action-btn primary" @click="commitNewFeature">
                                Add feature
                            </button>
                            <button class="action-btn" @click="cancelNewFeature">Cancel</button>
                        </p>
                    </div>
                </template>

                <p class="mt-4">
                    <FullscreenSource file="source/VectorEditor.vue" />
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
                @clickAt="onClickAt"
            />
        </template>
    </SplitView>
</template>

<style scoped>
@reference "../style.css";

.feature-list {
    @apply flex flex-col gap-1;
}

.feature-item {
    @apply flex items-center gap-2 px-3 py-2 rounded-lg cursor-pointer
           bg-surface text-onSurface
           transition-shadow hover:shadow-sm active:brightness-95 select-none;
}

.feature-item.selected {
    @apply bg-secondaryContainer text-onSecondaryContainer shadow-sm;
}

.color-dot {
    @apply w-4 h-4 rounded-full flex-shrink-0;
}

.feature-name {
    @apply flex-1 text-mLabelLarge;
}

.feature-height {
    @apply text-mLabelSmall opacity-60;
}

.props-section {
    @apply border-t border-outline pt-3;
}

.hint {
    @apply text-sm opacity-70 italic;
}

.action-btn {
    @apply h-10 px-4 rounded-lg
           bg-secondaryContainer text-onSecondaryContainer
           text-mLabelLarge select-none flex-1
           transition-shadow hover:shadow-md active:brightness-90;
}

.action-btn.primary {
    @apply bg-primary text-onPrimary;
}

.action-btn.danger {
    @apply bg-error text-onError;
}
</style>
