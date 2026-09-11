<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol, HrzProtocolHelper } from "@siradel-oss/horizon-protocol";
import { applyScene, applySceneTemplate, getLayerByName } from "@/utils/scenes";
import { MessageHandler } from "@/utils/messages";
import { ref, computed, watch, shallowRef } from "vue";

const apiRef = shallowRef<HrzApi.AsyncApi | null>(null);
const vectorDataLayerHandle = shallowRef<HrzProtocol.LayerHandle | null>(null);
const threeDTilesHandle = shallowRef<HrzProtocol.LayerHandle | null>(null);

const scriptOptions: { key: string; label: string }[] = [
    { key: "no-script", label: "No script" },
    { key: "all-red", label: "Uniform colour" },
    { key: "client-values", label: "Using client values" },
    { key: "filtering", label: "Filtering" },
    { key: "palettization", label: "Palettization" },
    { key: "classes", label: "Batch classes" },
    { key: "classes-attributes", label: "Batch classes attributes" },
];

const scripts: Record<string, string> = {
    "no-script": "",

    "all-red": `set "color" = #ff0000;
emit "tile";`,

    "client-values": `if (attr("Evenness") == "even") {
    set "color" = #00ff00;
} else {
    set "color" = #ff0000;
}
emit "tile";`,

    filtering: `if (attr("building_name") != "building1") {
    discard;
}
emit "tile";`,

    "classes-attributes": `if (attr("class_name") == "roof") {
    set "color" = colorize("paints", attr("roof_paint"));
} elif (attr("class_name") == "wall") {
    set "color" = colorize("paints", attr("wall_paint"));
}
emit "tile";`,

    classes: `set "color" = colorize("classes", attr("class_name"));
emit "tile";`,

    palettization: `set "color" = colorize("palette", attr("height"));
emit "tile";`,
};

const selectedScript = ref("classes-attributes");
const scriptText = computed(() => scripts[selectedScript.value]);

watch(scriptText, async (text) => {
    if (!apiRef.value || !threeDTilesHandle.value) return;
    await HrzApi.ThreeDTilesLayerPathBuilder.create(threeDTilesHandle.value)
        .stylingScript()
        .set(apiRef.value, text);
});

async function onHorizonReady(api: HrzApi.AsyncApi, msgHandler: MessageHandler) {
    apiRef.value = api;

    msgHandler.watchForever(async (msg) => {
        if (msg.payload != "vectorDataRequest") return;
        const request = msg.vectorDataRequest;
        if (
            request?.selection !== "featureIdSelection" ||
            request.attributeIds?.length !== 1 ||
            request.attributeIds[0] !== 101
        ) {
            return;
        }
        const features: HrzProtocol.ClientFeature.$Shape[] = [];
        for (const featureId of request.featureIdSelection.featureIds || []) {
            const id = HrzProtocolHelper.attributeAsNumber(featureId.attributes![0].value!);
            features.push({
                attributeValues: [{ stringValue: id % 2 === 0 ? "even" : "odd" }],
            });
        }
        await api.ClientDataService.provideVectorData({ ticket: request.ticket, features });
    });

    await applyScene(api, "three_d_tiles_styling");
    await applySceneTemplate(api, "non_realistic_ambiance_shadows");

    vectorDataLayerHandle.value = (await getLayerByName(api, "Vector data 1")) || null;
    threeDTilesHandle.value = (await getLayerByName(api, "3D tiles 1")) || null;

    await api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        angularViewpoint: {
            target: {
                latitude: 40.04270809655855,
                longitude: -75.61220999788243,
            },
            bearing: -0.5851866602897644,
            tilt: 1.1045150756835938,
            distance: 94.88977813720703,
        },
        limitBounds: { east: 180, north: 90, south: -90, west: -180 },
        maxAltitude: 10000000,
        minTilt: 0,
        maxTilt: Math.PI,
        altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
        goToAnimation: { duration: 0 },
        isInterruptible: true,
        correctionAnimation: { duration: 0 },
    });
}

async function retrieveVectorDataLayerModel(): Promise<any> {
    if (!vectorDataLayerHandle.value || !apiRef.value) return {};
    return (
        await HrzApi.VectorDataLayerPathBuilder.create(vectorDataLayerHandle.value).get(
            apiRef.value
        )
    ).toJSON();
}

async function retrieveThreeDTilesLayerModel(): Promise<any> {
    if (!threeDTilesHandle.value || !apiRef.value) return {};
    return (
        await HrzApi.ThreeDTilesLayerPathBuilder.create(threeDTilesHandle.value).get(apiRef.value)
    ).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>3D Tiles styling</h1>
                <p>
                    3D Tiles can be styled and filtered using a styling script. The script can
                    access feature attributes from the batch table, class hierarchy, or from a
                    client-side vector data layer. The script controls the colour of each feature
                    and whether it is shown or discarded.
                </p>
                <p>
                    This demo uses the
                    <a
                        href="https://github.com/CesiumGS/3d-tiles/blob/main/extensions/3DTILES_batch_table_hierarchy/README.md"
                        target="_blank"
                        >3DTILES_batch_table_hierarchy</a
                    >
                    extension, which exposes per-feature class names and attributes. An additional
                    client-side attribute (<em>Evenness</em>) is computed on the fly and injected
                    via a vector data layer.
                </p>
                <hr />
                <p>
                    <label>Style</label>
                    <select v-model="selectedScript">
                        <option v-for="opt in scriptOptions" :key="opt.key" :value="opt.key">
                            {{ opt.label }}
                        </option>
                    </select>
                </p>
                <pre>{{ scriptText }}</pre>
                <hr />
                <p>
                    <FullscreenSource
                        text="View demo source"
                        file="source/ThreeDTilesStyling.vue"
                    />
                    <FullscreenSceneModel
                        text="View vector data layer model"
                        :retrieveData="retrieveVectorDataLayerModel"
                    />
                    <FullscreenSceneModel
                        text="View 3D Tiles layer model"
                        :retrieveData="retrieveThreeDTilesLayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
