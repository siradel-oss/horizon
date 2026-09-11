<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import ColorInput from "@/component/ColorInput.vue";
import ToolbarIconButton from "@/component/ToolbarIconButton.vue";
import Icon from "@/component/Icon.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyDefaultSymbolicBaseLayer, applySceneTemplate } from "@/utils/scenes";
import { MessageHandler } from "@/utils/messages";
import { eqLong } from "@/utils/utils";
import FullscreenSource from "@/component/FullscreenSource.vue";
import { ref, reactive, computed, watch } from "vue";

interface ModelDef {
    name: string;
    url: string;
    defaultColor?: HrzProtocol.Color.$Properties;
    blendMode?: HrzProtocol.BlendMode;
    scaleMultiplier?: number;
}

const GRAY: HrzProtocol.Color.$Properties = { r: 0.5, g: 0.5, b: 0.5, a: 1 };
const WHITE: HrzProtocol.Color.$Properties = { r: 1, g: 1, b: 1, a: 1 };

const MODELS: ModelDef[] = [
    {
        name: "Eiffel Tower",
        url: "assets/demo/tour_eiffel.glb",
        defaultColor: GRAY,
        blendMode: HrzProtocol.BlendMode.BLEND_OVERLAY,
    },
    {
        name: "Wind turbine",
        url: "assets/demo/wind_turbine_simple.glb",
        defaultColor: WHITE,
        blendMode: HrzProtocol.BlendMode.BLEND_MULTIPLY,
    },
    {
        name: "Oval tree",
        url: "assets/demo/tree_oval.glb",
        defaultColor: GRAY,
        blendMode: HrzProtocol.BlendMode.BLEND_OVERLAY,
        scaleMultiplier: 10,
    },
    {
        name: "Round tree",
        url: "assets/demo/tree_round.glb",
        defaultColor: GRAY,
        blendMode: HrzProtocol.BlendMode.BLEND_OVERLAY,
        scaleMultiplier: 10,
    },
];

const GIZMO_ID = 0;

type Mode = "select" | "add" | "delete";

interface PlacedModel {
    id: number;
    name: string;
    layerHandle: HrzProtocol.LayerHandle;
    position: HrzProtocol.GeographicPosition.$Properties;
    rotation: HrzProtocol.Quat.$Properties;
    scale: number;
    scaleMultiplier: number;
    color: HrzProtocol.Color.$Properties;
    visible: boolean;
}

let api: HrzApi.AsyncApi;
let msgHandler: MessageHandler;
let gizmoLayerHandle: HrzProtocol.LayerHandle | null = null;
let nextId = 1;

const mode = ref<Mode | null>(null);
const selectedModelName = ref(MODELS[0].name);
const selectedModelId = ref<number | null>(null);
const gizmoTranslate = ref(true);
const gizmoRotate = ref(false);
const gizmoWorldSpace = ref(true);
const placedModels = reactive<PlacedModel[]>([]);

const selectedModel = computed(() =>
    selectedModelId.value !== null
        ? (placedModels.find((m) => m.id === selectedModelId.value) ?? null)
        : null
);

function fmt(v: number | null | undefined, decimals: number) {
    return v == null ? "—" : v.toFixed(decimals);
}

function buildComponents(): HrzProtocol.GizmoComponent.$Properties[] {
    const frame = gizmoWorldSpace.value
        ? HrzProtocol.GizmoReferenceFrame.WORLD_REFERENCE
        : HrzProtocol.GizmoReferenceFrame.OBJECT_REFERENCE;
    const parts: HrzProtocol.GizmoComponent.$Properties[] = [
        { type: HrzProtocol.GizmoComponentType.GIZMO_ORIGIN, referenceFrame: frame },
    ];
    if (gizmoTranslate.value) {
        parts.push(
            {
                type: HrzProtocol.GizmoComponentType.GIZMO_TRANSLATION_AXIS_X,
                referenceFrame: frame,
            },
            {
                type: HrzProtocol.GizmoComponentType.GIZMO_TRANSLATION_AXIS_Y,
                referenceFrame: frame,
            },
            {
                type: HrzProtocol.GizmoComponentType.GIZMO_TRANSLATION_AXIS_Z,
                referenceFrame: frame,
            },
            {
                type: HrzProtocol.GizmoComponentType.GIZMO_TRANSLATION_PLANE_CAMERA_PLANE,
                referenceFrame: frame,
            }
        );
    }
    if (gizmoRotate.value) {
        parts.push(
            { type: HrzProtocol.GizmoComponentType.GIZMO_ROTATION_RING_X, referenceFrame: frame },
            { type: HrzProtocol.GizmoComponentType.GIZMO_ROTATION_RING_Y, referenceFrame: frame },
            { type: HrzProtocol.GizmoComponentType.GIZMO_ROTATION_RING_Z, referenceFrame: frame },
            {
                type: HrzProtocol.GizmoComponentType.GIZMO_ROTATION_RING_CAMERA_PLANE,
                referenceFrame: frame,
            }
        );
    }
    return parts;
}

async function updateGizmoComponents() {
    if (!gizmoLayerHandle) return;
    const path = HrzApi.GizmoLayerPathBuilder.create(gizmoLayerHandle);
    const model = await path.clone().get(api);
    model.components = buildComponents();
    await path.clone().set(api, model);
}

watch([gizmoTranslate, gizmoRotate, gizmoWorldSpace], () => {
    if (selectedModel.value) updateGizmoComponents();
});

function setMode(m: Mode) {
    if (mode.value === m) {
        mode.value = null;
        selectedModelId.value = null;
        if (gizmoLayerHandle) {
            HrzApi.GizmoLayerPathBuilder.create(gizmoLayerHandle).visible().set(api, false);
        }
    } else {
        if (mode.value === "select" && m !== "select") {
            selectedModelId.value = null;
            if (gizmoLayerHandle) {
                HrzApi.GizmoLayerPathBuilder.create(gizmoLayerHandle).visible().set(api, false);
            }
        }
        mode.value = m;
    }
}

async function selectModel(id: number) {
    selectedModelId.value = id;
    const model = placedModels.find((m) => m.id === id);
    if (!model || !gizmoLayerHandle) return;
    await HrzApi.GizmoLayerPathBuilder.create(gizmoLayerHandle).set(api, {
        visible: true,
        id: GIZMO_ID,
        size: 125,
        sizeUnit: HrzProtocol.UiSizeUnit.UI_SIZE_IN_PIXELS,
        bboxPadding: 0,
        position: model.position,
        rotation: model.rotation,
        components: buildComponents(),
        sceneViews: { bits: 1 },
        grid: {},
        line: {},
    });
}

function handleGizmoUpdate(update: HrzProtocol.GizmoUpdateMessage.$Shape) {
    const model = placedModels.find((m) => m.id === selectedModelId.value);
    if (!model || !gizmoLayerHandle) return;
    if (update.geoPos) model.position = update.geoPos;
    if (update.rotation) model.rotation = update.rotation;
    const path = HrzApi.SingleModelLayerPathBuilder.create(model.layerHandle);
    if (update.geoPos) path.clone().geographic().set(api, update.geoPos);
    if (update.rotation) path.clone().transform().rotation().set(api, update.rotation);
    const gPath = HrzApi.GizmoLayerPathBuilder.create(gizmoLayerHandle);
    if (update.geoPos) gPath.clone().position().set(api, update.geoPos);
    if (update.rotation) gPath.clone().rotation().set(api, update.rotation);
}

async function addModelAt(
    position: HrzProtocol.GeographicPosition.$Properties,
    initialRotation?: HrzProtocol.Quat.$Properties
) {
    const entry = MODELS.find((m) => m.name === selectedModelName.value)!;
    const handle = await api.LayerService.createLayer({ type: HrzProtocol.LayerType.SINGLE_MODEL });
    const rotation: HrzProtocol.Quat.$Properties = initialRotation ?? { x: 0, y: 0, z: 0, w: 1 };
    const scale = 1;
    const scaleMultiplier = entry.scaleMultiplier ?? 1;
    const color: HrzProtocol.Color.$Properties = entry.defaultColor ?? { r: 1, g: 1, b: 1, a: 1 };
    const blendMode = entry.blendMode ?? HrzProtocol.BlendMode.BLEND_OVERLAY;
    const actualScale = scale * scaleMultiplier;
    await HrzApi.SingleModelLayerPathBuilder.create(handle).set(api, {
        url: entry.url,
        geographic: position,
        visible: true,
        color,
        lighting: { castShadows: true, receiveShadows: true, enableLighting: true },
        materialProperties: {
            featureColorBlendMode: blendMode,
            featureColorBlendStrength: 1,
        },
        sceneViews: { bits: 1 },
        transform: {
            frame: {
                front: HrzProtocol.Axis.NEG_Z,
                up: HrzProtocol.Axis.POS_Y,
                handedness: HrzProtocol.Handedness.RIGHT,
            },
            rotation,
            scale: { x: actualScale, y: actualScale, z: actualScale },
        },
    });
    const id = nextId++;
    placedModels.push({
        id,
        name: `${entry.name} #${id}`,
        layerHandle: handle,
        position,
        rotation,
        scale,
        scaleMultiplier,
        color,
        visible: true,
    });
}

async function deleteModel(id: number) {
    const idx = placedModels.findIndex((m) => m.id === id);
    if (idx === -1) return;
    const model = placedModels[idx];
    await api.LayerService.destroyLayer(model.layerHandle);
    placedModels.splice(idx, 1);
    if (selectedModelId.value === id) {
        selectedModelId.value = null;
        if (gizmoLayerHandle) {
            HrzApi.GizmoLayerPathBuilder.create(gizmoLayerHandle).visible().set(api, false);
        }
    }
}

async function toggleVisibility(id: number) {
    const model = placedModels.find((m) => m.id === id);
    if (!model) return;
    model.visible = !model.visible;
    await HrzApi.SingleModelLayerPathBuilder.create(model.layerHandle)
        .visible()
        .set(api, model.visible);
}

watch(
    () => selectedModel.value?.scale,
    async (scale) => {
        if (!selectedModel.value || scale == null) return;
        const actualScale = scale * selectedModel.value.scaleMultiplier;
        await HrzApi.SingleModelLayerPathBuilder.create(selectedModel.value.layerHandle)
            .transform()
            .scale()
            .set(api, { x: actualScale, y: actualScale, z: actualScale });
    }
);

watch(
    () => selectedModel.value?.color,
    async (color) => {
        if (!selectedModel.value || !color) return;
        await HrzApi.SingleModelLayerPathBuilder.create(selectedModel.value.layerHandle)
            .color()
            .set(api, color);
    },
    { deep: true }
);

watch(
    () => selectedModel.value?.layerHandle,
    async (handle) => {
        api.ViewerService.deselectAllFeatures();
        if (handle) {
            api.ViewerService.selectFeatures({
                features: [
                    {
                        layer: handle,
                        // No feature ID for single models
                        featureId: {},
                    },
                ],
            });
        }
    }
);

async function selectFromList(id: number) {
    mode.value = "select";
    await selectModel(id);
}

async function onCanvasClick(x: number, y: number) {
    if (!api) return;
    const result = await api.ViewerService.pickScreen({ coords: { x, y }, includedRasters: [] });
    if (!result.hasATicket || !result.ticket) return;
    msgHandler.awaitPickResult(result.ticket, async (pickResult) => {
        if (mode.value === "add") {
            if (pickResult.position) await addModelAt(pickResult.position);
            return;
        }
        const hit = pickResult.results?.find((r) =>
            placedModels.some((m) => eqLong(r.layer?.handle?.opaque, m.layerHandle.opaque))
        );
        if (!hit) return;
        const model = placedModels.find((m) =>
            eqLong(hit.layer?.handle?.opaque, m.layerHandle.opaque)
        );
        if (!model) return;
        if (mode.value === "delete") {
            await deleteModel(model.id);
        } else {
            await selectFromList(model.id);
        }
    });
}

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler_: MessageHandler) {
    api = api_;
    msgHandler = msgHandler_;
    applyDefaultSymbolicBaseLayer(api);
    applySceneTemplate(api, "initial_viewpoint_tour_eiffel", true);

    gizmoLayerHandle = await api.LayerService.createLayer({ type: HrzProtocol.LayerType.GIZMO });
    await HrzApi.GizmoLayerPathBuilder.create(gizmoLayerHandle).set(api, {
        visible: false,
        id: GIZMO_ID,
        size: 125,
        sizeUnit: HrzProtocol.UiSizeUnit.UI_SIZE_IN_PIXELS,
        bboxPadding: 0,
        position: { latitude: 0, longitude: 0, altitude: 0 },
        rotation: { x: 0, y: 0, z: 0, w: 1 },
        components: [],
        sceneViews: { bits: 1 },
        grid: {},
        line: {},
    });

    await addModelAt(
        { latitude: 48.858236, longitude: 2.2945038 },
        { x: 0, y: 0, z: 0.383, w: 0.924 }
    );

    msgHandler.watchForever((msg) => {
        if (msg.payload == "gizmoUpdate" && msg.gizmoUpdate?.id === GIZMO_ID) {
            handleGizmoUpdate(msg.gizmoUpdate!);
        }
    });

    HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
        .highlight()
        .selectionColor()
        .set(api, { r: 1, g: 0.5, b: 0, a: 0 });
}
</script>

<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Scene editor</h1>
                <p>
                    The demo implements a rudimentary scene editor that allows placing, transforming
                    and deleting 3D models on the map. It uses the single model layer type to place
                    glTF models, and the gizmo layer to provide interactive transformation controls
                    for the placed models. Additional properties such a color and scale can be
                    adjusted through a properties panel.
                </p>
                <p>
                    The other systems demonstrated here include the picking system (used to pick the
                    location for placing new models, and to select existing models for
                    transformation or deletion) and the highlighting system (used to highlight the
                    selected model).
                </p>
                <p>
                    <FullscreenSource file="source/SceneEditor.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer
                :messageHandlerIntervalMs="50"
                @ready="onHorizonReady"
                @clickAt="onCanvasClick"
            />

            <!-- Top bar -->
            <div
                class="absolute top-4 left-4 right-4 flex items-center gap-2 bg-surfaceContainerLow text-onSurface rounded-md shadow-lg px-2 py-1.5"
            >
                <div class="flex gap-1">
                    <ToolbarIconButton
                        icon="arrow_selector_tool"
                        title="Select"
                        :selected="mode === 'select'"
                        @click="setMode('select')"
                    />
                    <ToolbarIconButton
                        icon="add_location"
                        title="Add"
                        :selected="mode === 'add'"
                        @click="setMode('add')"
                    />
                    <ToolbarIconButton
                        icon="delete"
                        title="Delete"
                        :selected="mode === 'delete'"
                        :danger="true"
                        @click="setMode('delete')"
                    />
                </div>

                <template v-if="mode === 'add'">
                    <div class="w-px self-stretch bg-outline/30" />
                    <div class="flex gap-1 rounded bg-surfaceContainer">
                        <button
                            v-for="m in MODELS"
                            :key="m.name"
                            :title="m.name"
                            @click="selectedModelName = m.name"
                            class="h-9 px-3 flex items-center justify-center rounded transition-colors select-none text-sm"
                            :class="
                                selectedModelName === m.name
                                    ? 'bg-primary text-onPrimary'
                                    : 'text-onSurfaceVariant hover:bg-surfaceContainerHigh hover:text-onSurface'
                            "
                        >
                            {{ m.name }}
                        </button>
                    </div>
                </template>

                <template v-if="mode === 'select' && selectedModel">
                    <div class="w-px self-stretch bg-outline/30" />
                    <div class="flex items-center gap-1.5">
                        <span class="text-[10px] text-onSurfaceVariant leading-none">Controls</span>
                        <div class="flex gap-1 rounded bg-surfaceContainer">
                            <ToolbarIconButton
                                icon="open_with"
                                title="Translate"
                                :selected="gizmoTranslate"
                                @click="gizmoTranslate = !gizmoTranslate"
                            />
                            <ToolbarIconButton
                                icon="rotate_right"
                                title="Rotate"
                                :selected="gizmoRotate"
                                @click="gizmoRotate = !gizmoRotate"
                            />
                        </div>
                    </div>
                    <div class="w-px self-stretch bg-outline/30" />
                    <div class="flex items-center gap-1.5">
                        <span class="text-[10px] text-onSurfaceVariant leading-none"
                            >Reference</span
                        >
                        <div class="flex gap-1 rounded bg-surfaceContainer">
                            <ToolbarIconButton
                                icon="public"
                                title="World space"
                                :selected="gizmoWorldSpace"
                                @click="gizmoWorldSpace = true"
                            />
                            <ToolbarIconButton
                                icon="view_in_ar"
                                title="Object space"
                                :selected="!gizmoWorldSpace"
                                @click="gizmoWorldSpace = false"
                            />
                        </div>
                    </div>
                </template>
            </div>

            <!-- Left panels -->
            <div class="absolute left-4 top-22 w-56 flex flex-col gap-2">
                <!-- Model list panel -->
                <div
                    class="bg-surfaceContainerLow text-onSurface rounded-lg shadow-lg p-3 flex flex-col gap-2"
                >
                    <h3 class="font-medium text-sm">Model instances</h3>
                    <p v-if="placedModels.length === 0" class="text-xs text-onSurfaceVariant">
                        Use Add mode to place models.
                    </p>
                    <ul class="flex flex-col gap-1">
                        <li
                            v-for="model in placedModels"
                            :key="model.id"
                            @click="selectFromList(model.id)"
                            :class="[
                                'flex items-center gap-2 px-2 py-1 rounded cursor-pointer text-sm transition-colors',
                                selectedModelId === model.id
                                    ? 'bg-primaryContainer text-onPrimaryContainer'
                                    : 'hover:bg-surfaceContainerHigh',
                            ]"
                        >
                            <button
                                @click.stop="toggleVisibility(model.id)"
                                class="flex items-center text-onSurfaceVariant hover:text-onSurface transition-colors shrink-0 text-[20px]"
                                :title="model.visible ? 'Hide' : 'Show'"
                            >
                                <Icon :icon="model.visible ? 'visibility' : 'visibility_off'" />
                            </button>
                            <span class="truncate flex-1">{{ model.name }}</span>
                        </li>
                    </ul>
                </div>

                <!-- Properties panel -->
                <div
                    v-if="mode === 'select' && selectedModel"
                    class="bg-surfaceContainerLow text-onSurface rounded-lg shadow-lg p-4 flex flex-col gap-3"
                >
                    <h3 class="font-medium text-sm truncate">{{ selectedModel.name }}</h3>

                    <div class="text-xs text-onSurfaceVariant space-y-1">
                        <div>
                            Lat: {{ fmt(selectedModel.position.latitude, 5) }}° Lon:
                            {{ fmt(selectedModel.position.longitude, 5) }}°
                        </div>
                        <div>Alt: {{ fmt(selectedModel.position.altitude ?? 0, 1) }} m</div>
                    </div>

                    <label class="flex flex-col gap-1 text-sm">
                        Scale: {{ selectedModel.scale.toFixed(2) }}
                        <input
                            type="range"
                            min="0.01"
                            max="10"
                            step="0.01"
                            v-model.number="selectedModel.scale"
                        />
                    </label>

                    <label class="flex items-center gap-2 text-sm">
                        Color:
                        <ColorInput
                            :key="selectedModelId!"
                            class="flex-1"
                            v-model="selectedModel.color"
                        />
                    </label>

                    <button
                        @click="deleteModel(selectedModel!.id)"
                        class="mt-1 h-9 flex items-center justify-center gap-2 rounded text-sm font-medium bg-error text-onError hover:opacity-90 transition-opacity"
                    >
                        <Icon icon="delete" />
                        Delete
                    </button>
                </div>
            </div>
        </template>
    </SplitView>
</template>
