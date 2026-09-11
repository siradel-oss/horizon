<!--
    SPDX-FileCopyrightText: Copyright 2025 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyScene, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import { ref, shallowRef, watch, computed } from "vue";
import { debounce } from "@/utils/utils";

enum DashStyle {
    DASHES = "Dashes",
    DASHES_BG = "Dashes with background",
    GRADIENT = "Gradient",
    LIGHTENED_GRADIENT = "Lightened gradient",
}

enum LengthUnit {
    METERS = "Meters",
    PIXELS = "Pixels",
}

const LENGTH_UNIT = {
    [LengthUnit.METERS]: "m",
    [LengthUnit.PIXELS]: "px",
};

const api = shallowRef<HrzApi.AsyncApi | null>(null);
const layer = shallowRef<HrzProtocol.LayerHandle | null>(null);
let model = HrzProtocol.FlatOverlayPolylineVectorRepr.create();

const animationSpeed = ref<number>(0);
const dashPeriod = ref<number>(500);
const dashRatio = ref<number>(0.5);
const dashStyle = ref<DashStyle>(DashStyle.DASHES);
const dashLengthUnit = ref<LengthUnit>(LengthUnit.METERS);

const stylingScript = computed(() => {
    let script = "";
    switch (dashStyle.value) {
        case DashStyle.DASHES:
            script += `set "color" = attr("color");\n`;
            break;
        case DashStyle.DASHES_BG:
            script += `set "color" = attr("color");\n`;
            script += `set "bg_color" = alpha(lighten(attr("color"), 0.2), 0.3);\n`;
            break;
        case DashStyle.GRADIENT:
            script += `set "color" = attr("color");\n`;
            script += `set "bg_color" = alpha(attr("color"), 0);\n`;
            break;
        case DashStyle.LIGHTENED_GRADIENT:
            script += `set "color" = lighten(attr("color"), 0.5);\n`;
            script += `set "bg_color" = alpha(attr("color"), 0);\n`;
            break;
    }
    script += `emit 0;\n`;
    return script;
});

watch(
    [stylingScript, api, layer],
    debounce(() => {
        if (!api.value || !layer.value) {
            return;
        }

        HrzApi.VectorTilesLayerPathBuilder.create(layer.value!)
            .style()
            .stylingScript()
            .set(api.value, stylingScript.value);
    }, 300)
);

watch(
    [animationSpeed, dashPeriod, dashRatio, dashStyle, stylingScript, dashLengthUnit, api, layer],
    debounce(() => {
        if (!api.value || !layer.value) {
            return;
        }

        model.dashes ??= {};
        model.dashes.animationSpeed = { defaultValue: animationSpeed.value };
        model.dashes.period = { defaultValue: dashPeriod.value };
        model.dashes.primarySegmentLength = { defaultValue: dashRatio.value * dashPeriod.value };

        model.dashes.periodUnit =
            dashLengthUnit.value === LengthUnit.METERS
                ? HrzProtocol.DashSizeUnit.DASH_SIZE_IN_METERS
                : HrzProtocol.DashSizeUnit.DASH_SIZE_IN_PIXELS;
        model.dashes.primarySegmentLengthUnit = model.dashes.periodUnit;
        model.dashes.animationSpeedUnit = model.dashes.periodUnit;

        if (dashStyle.value == DashStyle.DASHES || dashStyle.value == DashStyle.DASHES_BG) {
            model.dashes.mode = HrzProtocol.DashMode.DASH_ENABLED_FILLED;
        } else {
            model.dashes.mode = HrzProtocol.DashMode.DASH_ENABLED_GRADIENT;
        }

        HrzApi.VectorTilesLayerPathBuilder.create(layer.value!)
            .style()
            .representations(0)
            .flatOverlayPolyline()
            .set(api.value, model);
    }, 300)
);

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api.value = api_;

    await applyScene(api.value, "paris_metro_dashes").then(async function () {
        layer.value = (await getLayerByName(api.value!, "Metro")) || null;
        model = await HrzApi.VectorTilesLayerPathBuilder.create(layer.value!)
            .style()
            .representations(0)
            .flatOverlayPolyline()
            .get(api.value!);

        animationSpeed.value = model.dashes?.animationSpeed?.defaultValue ?? 400;
        dashPeriod.value = model.dashes?.period?.defaultValue ?? 500;
        dashRatio.value =
            (model.dashes?.primarySegmentLength?.defaultValue ?? 200) / dashPeriod.value;
    });
}

async function retrieveMetroLayerModel(): Promise<any> {
    if (!layer.value || !api.value) {
        return {};
    }
    return (await HrzApi.VectorTilesLayerPathBuilder.create(layer.value).get(api.value)).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Polyline animation</h1>
                <p>
                    This example shows how to animate polylines in a vector tiles layer. The
                    animation is achieved by using a flat overlay representation with a dash style
                    and an animation speed.
                </p>
                <p>
                    There are two dash modes: the filled mode uses the polyline color for the dash
                    and the secondary color for the background, while the gradient mode uses a
                    gradient from the polyline color to the secondary color.
                </p>
                <hr />
                <p>
                    <label>Length unit</label>
                    <select v-model="dashLengthUnit">
                        <option v-for="key in LengthUnit" :value="key">{{ key }}</option>
                    </select>
                </p>
                <p>
                    <label>Dash style</label>
                    <select v-model="dashStyle">
                        <option v-for="style in DashStyle" :value="style">{{ style }}</option>
                    </select>
                </p>
                <p>
                    <label
                        >Animation speed ({{ $filters.formatNumber(animationSpeed) }}
                        {{ LENGTH_UNIT[dashLengthUnit] }}/s)</label
                    >
                    <input
                        type="range"
                        min="-500"
                        max="500"
                        step="10"
                        v-model.number="animationSpeed"
                    />
                </p>
                <p>
                    <label
                        >Dash period ({{ $filters.formatNumber(dashPeriod) }}
                        {{ LENGTH_UNIT[dashLengthUnit] }})</label
                    >
                    <input
                        type="range"
                        min="100"
                        max="1000"
                        step="any"
                        v-model.number="dashPeriod"
                    />
                </p>
                <p>
                    <label>Dash ratio ({{ $filters.formatNumber(dashRatio * 100) }} %)</label>
                    <input type="range" min="0" max="1" step="any" v-model.number="dashRatio" />
                </p>
                <hr />
                <p><label>Styling script</label></p>
                <pre>{{ stylingScript }}</pre>
                <hr />
                <p>
                    <FullscreenSceneModel
                        text="View metro layer definition"
                        :retrieveData="retrieveMetroLayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
