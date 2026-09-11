<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import {
    applyDefaultSymbolicBaseLayer,
    applyScene,
    applySceneTemplate,
    getLayerByName,
} from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import { ref, shallowRef, reactive, watch } from "vue";
import FullscreenSource from "@/component/FullscreenSource.vue";

const MATERIALS = [
    { name: "default", label: "Default" },
    { name: "ao", label: "Ambient occlusion" },
    { name: "data", label: "Data 1" },
    { name: "data 2", label: "Data 2" },
] as const;

type MaterialName = (typeof MATERIALS)[number]["name"];
type PaletteName = keyof typeof PALETTES;

const DATA_MATERIALS = ["data", "data 2"] as const;
type DataMaterialName = (typeof DATA_MATERIALS)[number];

function isDataMaterial(name: MaterialName): name is DataMaterialName {
    return (DATA_MATERIALS as readonly string[]).includes(name);
}

const PALETTES = {
    rainbow: {
        label: "Rainbow",
        palette: {
            interpolationMode: HrzProtocol.ColorInterpolationMode.SRGB,
            colorStops: [
                { firstColor: { a: 1 }, secondColor: { r: 0.537, g: 0.004, b: 0.004, a: 1 } },
                { value: 0.1, firstColor: { r: 1, a: 1 }, secondColor: { r: 1, a: 1 } },
                { value: 0.3, firstColor: { r: 1, g: 1, a: 1 }, secondColor: { r: 1, g: 1, a: 1 } },
                { value: 0.5, firstColor: { g: 1, a: 1 }, secondColor: { g: 1, a: 1 } },
                { value: 0.7, firstColor: { g: 1, b: 1, a: 1 }, secondColor: { g: 1, b: 1, a: 1 } },
                { value: 0.9, firstColor: { b: 1, a: 1 }, secondColor: { b: 1, a: 1 } },
                { value: 1, firstColor: { b: 0.56, a: 1 }, secondColor: { a: 1 } },
            ],
            nanColor: { a: 1 },
        } as HrzProtocol.NumericPalette.$Shape,
    },
    viridis: {
        label: "Viridis",
        palette: {
            interpolationMode: HrzProtocol.ColorInterpolationMode.OKLAB,
            colorStops: [
                {
                    value: 0,
                    firstColor: { r: 0.267, g: 0.004, b: 0.329, a: 1 },
                    secondColor: { r: 0.267, g: 0.004, b: 0.329, a: 1 },
                },
                {
                    value: 0.25,
                    firstColor: { r: 0.192, g: 0.408, b: 0.557, a: 1 },
                    secondColor: { r: 0.192, g: 0.408, b: 0.557, a: 1 },
                },
                {
                    value: 0.5,
                    firstColor: { r: 0.208, g: 0.718, b: 0.475, a: 1 },
                    secondColor: { r: 0.208, g: 0.718, b: 0.475, a: 1 },
                },
                {
                    value: 0.75,
                    firstColor: { r: 0.565, g: 0.843, b: 0.263, a: 1 },
                    secondColor: { r: 0.565, g: 0.843, b: 0.263, a: 1 },
                },
                {
                    value: 1,
                    firstColor: { r: 0.992, g: 0.906, b: 0.145, a: 1 },
                    secondColor: { r: 0.992, g: 0.906, b: 0.145, a: 1 },
                },
            ],
            nanColor: { a: 1 },
        } as HrzProtocol.NumericPalette.$Shape,
    },
    plasma: {
        label: "Plasma",
        palette: {
            interpolationMode: HrzProtocol.ColorInterpolationMode.OKLAB,
            colorStops: [
                {
                    value: 0,
                    firstColor: { r: 0.051, g: 0.031, b: 0.529, a: 1 },
                    secondColor: { r: 0.051, g: 0.031, b: 0.529, a: 1 },
                },
                {
                    value: 0.25,
                    firstColor: { r: 0.494, g: 0.012, b: 0.659, a: 1 },
                    secondColor: { r: 0.494, g: 0.012, b: 0.659, a: 1 },
                },
                {
                    value: 0.5,
                    firstColor: { r: 0.8, g: 0.278, b: 0.471, a: 1 },
                    secondColor: { r: 0.8, g: 0.278, b: 0.471, a: 1 },
                },
                {
                    value: 0.75,
                    firstColor: { r: 0.973, g: 0.584, b: 0.251, a: 1 },
                    secondColor: { r: 0.973, g: 0.584, b: 0.251, a: 1 },
                },
                {
                    value: 1,
                    firstColor: { r: 0.941, g: 0.976, b: 0.129, a: 1 },
                    secondColor: { r: 0.941, g: 0.976, b: 0.129, a: 1 },
                },
            ],
            nanColor: { a: 1 },
        } as HrzProtocol.NumericPalette.$Shape,
    },
    traffic: {
        label: "Traffic (discrete)",
        palette: {
            interpolationMode: HrzProtocol.ColorInterpolationMode.SRGB,
            colorStops: [
                {
                    value: 0,
                    firstColor: { r: 0.18, g: 0.545, b: 0.196, a: 1 },
                    secondColor: { r: 0.18, g: 0.545, b: 0.196, a: 1 },
                },
                {
                    value: 0.333,
                    firstColor: { r: 0.18, g: 0.545, b: 0.196, a: 1 },
                    secondColor: { r: 0.984, g: 0.627, b: 0.075, a: 1 },
                },
                {
                    value: 0.667,
                    firstColor: { r: 0.984, g: 0.627, b: 0.075, a: 1 },
                    secondColor: { r: 0.867, g: 0.176, b: 0.149, a: 1 },
                },
                {
                    value: 1,
                    firstColor: { r: 0.867, g: 0.176, b: 0.149, a: 1 },
                    secondColor: { r: 0.867, g: 0.176, b: 0.149, a: 1 },
                },
            ],
            nanColor: { a: 1 },
        } as HrzProtocol.NumericPalette.$Shape,
    },
    stepped: {
        label: "Stepped (discrete)",
        palette: {
            interpolationMode: HrzProtocol.ColorInterpolationMode.SRGB,
            colorStops: [
                {
                    value: 0,
                    firstColor: { r: 0.086, g: 0.396, b: 0.753, a: 1 },
                    secondColor: { r: 0.086, g: 0.396, b: 0.753, a: 1 },
                },
                {
                    value: 0.2,
                    firstColor: { r: 0.086, g: 0.396, b: 0.753, a: 1 },
                    secondColor: { r: 0, g: 0.514, b: 0.561, a: 1 },
                },
                {
                    value: 0.4,
                    firstColor: { r: 0, g: 0.514, b: 0.561, a: 1 },
                    secondColor: { r: 0.18, g: 0.49, b: 0.196, a: 1 },
                },
                {
                    value: 0.6,
                    firstColor: { r: 0.18, g: 0.49, b: 0.196, a: 1 },
                    secondColor: { r: 0.937, g: 0.424, b: 0, a: 1 },
                },
                {
                    value: 0.8,
                    firstColor: { r: 0.937, g: 0.424, b: 0, a: 1 },
                    secondColor: { r: 0.776, g: 0.157, b: 0.157, a: 1 },
                },
                {
                    value: 1,
                    firstColor: { r: 0.776, g: 0.157, b: 0.157, a: 1 },
                    secondColor: { r: 0.776, g: 0.157, b: 0.157, a: 1 },
                },
            ],
            nanColor: { a: 1 },
        } as HrzProtocol.NumericPalette.$Shape,
    },
};

let api = shallowRef<HrzApi.AsyncApi | null>(null);
let layer = shallowRef<HrzProtocol.LayerHandle | null>(null);

// Index of each data material in the layer's materials array, found after scene load.
let dataMatIndices = reactive<Record<DataMaterialName, number>>({ data: 2, "data 2": 3 });

// One palette selection per data material — shared between base and overlay selectors.
let selectedPalettes = reactive<Record<DataMaterialName, PaletteName>>({
    data: "rainbow",
    "data 2": "rainbow",
});

let baseMaterial = ref<MaterialName>("default");
let overlayMaterial = ref<MaterialName>("data");
let enableOverlay = ref(false);
let overlayOpacity = ref(0.5);

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api.value = api_;

    await applyScene(api.value, "dynamic_texturing").then(async () => {
        layer.value = (await getLayerByName(api.value!, "house.gltf")) || null;
    });

    await applyDefaultSymbolicBaseLayer(api.value);
    await applySceneTemplate(api.value, "non_realistic_ambiance_shadows");

    if (layer.value) {
        const layerDef = await HrzApi.SingleModelLayerPathBuilder.create(layer.value).get(
            api.value
        );
        const props = layerDef.materialProperties;
        if (props) {
            if (props.baseMaterial) baseMaterial.value = props.baseMaterial as MaterialName;
            if (props.overlayMaterial)
                overlayMaterial.value = props.overlayMaterial as MaterialName;
            enableOverlay.value = props.enableOverlay ?? false;
            overlayOpacity.value = props.overlayOpacity ?? 0.5;
        }
        for (const name of DATA_MATERIALS) {
            const idx = (layerDef.materials ?? []).findIndex((m) => m.name === name);
            if (idx >= 0) dataMatIndices[name] = idx;
        }
    }
}

watch([baseMaterial, api, layer], async () => {
    if (!api.value || !layer.value) return;
    await HrzApi.SingleModelLayerPathBuilder.create(layer.value)
        .materialProperties()
        .baseMaterial()
        .set(api.value, baseMaterial.value);
});

watch([overlayMaterial, api, layer], async () => {
    if (!api.value || !layer.value) return;
    await HrzApi.SingleModelLayerPathBuilder.create(layer.value)
        .materialProperties()
        .overlayMaterial()
        .set(api.value, overlayMaterial.value);
});

watch([enableOverlay, api, layer], async () => {
    if (!api.value || !layer.value) return;
    if (enableOverlay.value) {
        await HrzApi.SingleModelLayerPathBuilder.create(layer.value)
            .materialProperties()
            .overlayMaterial()
            .set(api.value, overlayMaterial.value);
    }
    await HrzApi.SingleModelLayerPathBuilder.create(layer.value)
        .materialProperties()
        .enableOverlay()
        .set(api.value, enableOverlay.value);
});

watch([overlayOpacity, api, layer], async () => {
    if (!api.value || !layer.value) return;
    await HrzApi.SingleModelLayerPathBuilder.create(layer.value)
        .materialProperties()
        .overlayOpacity()
        .set(api.value, overlayOpacity.value);
});

for (const name of DATA_MATERIALS) {
    watch([() => selectedPalettes[name], api, layer], async () => {
        if (!api.value || !layer.value) return;
        await HrzApi.SingleModelLayerPathBuilder.create(layer.value)
            .materials(dataMatIndices[name])
            .dataTexturePalette()
            .set(api.value, PALETTES[selectedPalettes[name]].palette);
    });
}

async function retrieveLayerModel(): Promise<any> {
    if (!layer.value || !api.value) return {};
    return (await HrzApi.SingleModelLayerPathBuilder.create(layer.value).get(api.value)).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Dynamic multi-texturing</h1>
                <p>
                    This demo shows how to switch between multiple materials on a 3D model at
                    runtime. Four materials are available: <em>default</em> uses the base glTF
                    textures, <em>ambient occlusion</em> uses dynamically-fetched AO textures using
                    the
                    <a href="../doc/SIRADEL_templated_image_url.html"
                        >SIRADEL_templated_image_url</a
                    >
                    extension, and <em>data 1</em> and <em>data 2</em> display a "data texture"
                    colored with a customizable palette for each of them using the
                    <a href="../doc/SIRADEL_data_texture.html">SIRADEL_data_texture</a> extension.
                </p>
                <p>
                    A second material can be blended on top of the base using the overlay feature.
                    The overlay opacity controls how strongly the two materials are mixed.
                </p>
                <hr />
                <p>
                    <label>Base material</label>
                    <select v-model="baseMaterial">
                        <option v-for="m in MATERIALS" :key="m.name" :value="m.name">
                            {{ m.label }}
                        </option>
                    </select>
                </p>
                <p v-if="isDataMaterial(baseMaterial)">
                    <label>Palette</label>
                    <select v-model="selectedPalettes[baseMaterial]">
                        <option v-for="(entry, key) in PALETTES" :key="key" :value="key">
                            {{ entry.label }}
                        </option>
                    </select>
                </p>
                <hr />
                <p>
                    <label>
                        <input type="checkbox" v-model="enableOverlay" />
                        Enable overlay
                    </label>
                </p>
                <template v-if="enableOverlay">
                    <p>
                        <label>Overlay material</label>
                        <select v-model="overlayMaterial">
                            <option v-for="m in MATERIALS" :key="m.name" :value="m.name">
                                {{ m.label }}
                            </option>
                        </select>
                    </p>
                    <p v-if="isDataMaterial(overlayMaterial)">
                        <label>Palette</label>
                        <select v-model="selectedPalettes[overlayMaterial]">
                            <option v-for="(entry, key) in PALETTES" :key="key" :value="key">
                                {{ entry.label }}
                            </option>
                        </select>
                    </p>
                    <p>
                        <label>Opacity ({{ $filters.formatNumber(overlayOpacity, 2) }})</label>
                        <input
                            type="range"
                            min="0"
                            max="1"
                            step="any"
                            v-model.number="overlayOpacity"
                        />
                    </p>
                </template>
                <hr />
                <p>
                    <FullscreenSceneModel
                        text="View model layer definition"
                        :retrieveData="retrieveLayerModel"
                    />
                    <FullscreenSource text="View source" file="source/DynamicMultiTexturing.vue" />
                    <FullscreenSource
                        text="View glTF model"
                        file="assets/demo/house_dynamic_materials/house.gltf"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
