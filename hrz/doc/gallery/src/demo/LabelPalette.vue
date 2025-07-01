<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { applyDefaultSymbolicBaseLayer, applyScene, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import { ref, reactive, watch, computed } from "vue";
import { debounce } from "@/utils/utils";
import { colorFromHex } from "@/utils/colors";

const api = ref<HrzApi.AsyncApi | null>(null);
const layerLod1Data = ref<HrzProtocol.ILayerHandle | undefined>(undefined);
const layerLod1 = ref<HrzProtocol.ILayerHandle | undefined>(undefined);

const opacity = ref(1.0);

const stylingScript = computed(() => {
    return `set "top_color" = alpha(colorize("type_color", attr("type")), ${opacity.value});
set "bottom_color" = darken(prp("top_color"), 0.3);
set "extrusion" = attr("height");
emit 0;`;
});

const palette = reactive<{ [key: string]: string }>({
    commercial: "#94e4ff",
    hotel: "#1ac6ff",
    office: "#349cfe",
    religious: "#cbe864",
    theatre: "#51b86b",
    museum: "#8de2a7",
    sport: "#b6e958",
    kindergarten: "#fed7fb",
    school: "#f2dab1",
    service: "#ffae00",
    civic: "#fee858",
    public: "#ff8800",
    government: "#ffb10a",
    hospital: "#ea3941",
    parking: "#915f9b",
    industrial: "#b224ff",
    transportation: "#c774cd",
    warehouse: "#ec83a8",
    bridge: "#ababab",
});

const defaultColor = ref("#ffffff");

watch([api, stylingScript, layerLod1], async () => {
    if (!api.value || !layerLod1.value) return;

    debounce(async () => {
        await HrzApi.VectorTilesLayerPathBuilder.create(layerLod1.value!)
            .style()
            .stylingScript()
            .set(api.value!, stylingScript.value);
    }, 100)();
});

watch([api, layerLod1, palette, defaultColor], async () => {
    if (!api.value || !layerLod1.value) return;

    console.log(JSON.stringify(palette));

    let hrzPalette = HrzProtocol.Palette.create({
        name: "type_color",
        type: HrzProtocol.PaletteType.LABEL,
        label: {
            defaultColor: colorFromHex(defaultColor.value),
            labels: Object.entries(palette).map(([key, value]) => ({
                label: key,
                color: colorFromHex(value),
            })),
        },
    });

    debounce(async () => {
        await HrzApi.VectorTilesLayerPathBuilder.create(layerLod1.value!)
            .style()
            .palettes(0)
            .set(api.value!, hrzPalette);
    }, 200)();
});

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api.value = api_;

    applyScene(api.value, "rennes_building_type").then(async function () {
        layerLod1Data.value = await getLayerByName(api.value!, "Rennes buildings data");
        layerLod1.value = await getLayerByName(api.value!, "Rennes buildings");
    });

    applyDefaultSymbolicBaseLayer(api.value!);
}

async function retrieveLod1DataLayerModel(): Promise<any> {
    if (!layerLod1Data.value) {
        return {};
    }
    return (
        await HrzApi.VectorDataLayerPathBuilder.create(layerLod1Data.value).get(api.value!)
    ).toJSON();
}

async function retrieveLod1LayerModel(): Promise<any> {
    if (!layerLod1.value) {
        return {};
    }
    return (
        await HrzApi.VectorTilesLayerPathBuilder.create(layerLod1.value).get(api.value!)
    ).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Label palette</h1>
                <p>
                    This demo shows a simple polygon layer that is extruded with the height of each
                    building. Additionally, each building has an associated type attribute which is
                    used to colorize the building.
                </p>
                <p>The color palette can be customized below.</p>
                <hr />
                <p>
                    <label>Default color</label>
                    <input type="color" v-model="defaultColor" />
                </p>
                <p>
                    <label>Buildings opacity ({{ $filters.formatNumber(opacity * 100) }} %)</label>
                    <input type="range" min="0" max="1" step="any" v-model.number="opacity" />
                </p>
                <div class="grid grid-cols-[35%_65%] gap-y-2 items-center">
                    <template v-for="(color, label) in palette" :key="label">
                        <label class="capitalize">{{ label }}</label>
                        <input type="color" v-model="palette[label]" />
                    </template>
                </div>
                <hr />
                <p><label>Styling script</label></p>
                <pre>{{ stylingScript.trim() }}</pre>
                <p>
                    <FullscreenSceneModel
                        text="View LOD 1 data layer definition"
                        :retrieveData="retrieveLod1DataLayerModel"
                    />
                    <FullscreenSceneModel
                        text="View LOD 1 layer definition"
                        :retrieveData="retrieveLod1LayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
