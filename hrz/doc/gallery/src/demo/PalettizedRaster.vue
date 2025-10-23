<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { applyScene, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import { ref, reactive, watch } from "vue";
import ColorInput from "@/component/ColorInput.vue";
import { MessageHandler } from "@/utils/messages";
import FullscreenSource from "@/component/FullscreenSource.vue";
import { eqLong } from "@/utils/utils";

enum PaletteMode {
    GRADIENT = "Gradient",
    CATEGORIZED = "Categorized",
}

let api: HrzApi.AsyncApi;
let msgHandler: MessageHandler;
let layer = ref<HrzProtocol.ILayerHandle | undefined>();

let useBilinearInterpolation = ref(false);
let colorInterpolationMode = ref<HrzProtocol.ColorInterpolationMode>(
    HrzProtocol.ColorInterpolationMode.OKLAB
);
let colorValues = reactive<number[]>([]);
let colors = reactive<HrzProtocol.IColor[]>([]);
let paletteMode = ref<PaletteMode>(PaletteMode.GRADIENT);

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler_: MessageHandler) {
    api = api_;
    msgHandler = msgHandler_;
    applyScene(api, "san_francisco_data_raster").then(async function () {
        layer.value = await getLayerByName(api, "Computation result");
    });
}

watch(layer, async function (newLayer) {
    if (!newLayer) {
        return;
    }

    let rasterData = await HrzApi.ImageryRasterLayerPathBuilder.create(newLayer).raster().get(api);

    useBilinearInterpolation.value =
        rasterData.sampling?.filtering === HrzProtocol.TextureFiltering.BILINEAR;
    colorInterpolationMode.value =
        rasterData.provider?.palettized?.palette?.interpolationMode ||
        HrzProtocol.ColorInterpolationMode.OKLAB;

    let colorPoints = rasterData.provider?.palettized?.palette?.colorPoints;
    if (colorPoints) {
        colorPoints.sort((a, b) => (a.value || 0) - (b.value || 0));

        for (let [index, value] of colorPoints.entries()) {
            colorValues[index] = value.value || 0;
            if (value.firstColor?.a == 0) {
                colors[index] = value.secondColor || { r: 0, g: 0, b: 0, a: 0 };
            } else {
                colors[index] = value.firstColor || { r: 0, g: 0, b: 0, a: 0 };
            }
        }
    }
});

watch(useBilinearInterpolation, async function (useBilinearInterpolation) {
    if (layer.value) {
        await HrzApi.ImageryRasterLayerPathBuilder.create(layer.value)
            .raster()
            .sampling()
            .filtering()
            .set(
                api,
                useBilinearInterpolation
                    ? HrzProtocol.TextureFiltering.BILINEAR
                    : HrzProtocol.TextureFiltering.NEAREST
            );
    }
});

watch(colorInterpolationMode, async function (colorInterpolationMode) {
    if (layer.value) {
        await HrzApi.ImageryRasterLayerPathBuilder.create(layer.value)
            .raster()
            .provider()
            .palettized()
            .palette()
            .interpolationMode()
            .set(api, colorInterpolationMode);
    }
});

watch([colors, paletteMode], async function () {
    if (!layer.value) {
        return;
    }

    let paletteData = await HrzApi.ImageryRasterLayerPathBuilder.create(layer.value)
        .raster()
        .provider()
        .palettized()
        .palette()
        .get(api);

    if (paletteMode.value == PaletteMode.GRADIENT) {
        for (let [i, color] of colors.entries()) {
            paletteData.colorPoints[i].value = colorValues[i];
            paletteData.colorPoints[i].firstColor = i == 0 ? {} : color;
            paletteData.colorPoints[i].secondColor = i == colors.length - 1 ? {} : color;
        }
    } else {
        for (let [i, _] of colors.entries()) {
            paletteData.colorPoints[i].value = colorValues[i];
            paletteData.colorPoints[i].firstColor = i == 0 ? {} : colors[i - 1];
            paletteData.colorPoints[i].secondColor = i == colors.length - 1 ? {} : colors[i];
        }
    }

    await HrzApi.ImageryRasterLayerPathBuilder.create(layer.value)
        .raster()
        .provider()
        .palettized()
        .palette()
        .set(api, paletteData);
});

async function retrieveRasterLayerModel(): Promise<any> {
    if (!layer.value) {
        return {};
    }
    return (await HrzApi.ImageryRasterLayerPathBuilder.create(layer.value).get(api)).toJSON();
}

async function retrievePaletteModel(): Promise<any> {
    if (!layer.value) {
        return {};
    }
    let paletteData = await HrzApi.ImageryRasterLayerPathBuilder.create(layer.value)
        .raster()
        .provider()
        .palettized()
        .palette()
        .get(api);
    paletteData.colorPoints?.sort((a, b) => (a.value || 0) - (b.value || 0));
    return paletteData.toJSON();
}

let pickedRasterValue = ref<string | null>(null);

async function schedulePick(x: number, y: number) {
    // Since we have no cache on the palettized raster, we must use
    // ViewerService.FetchRasterData to reliably retrieve the raster
    // value. So first we must know the position we clicked on, so we
    // do a pick to retrieve the lat/long coordinates of the click.

    if (!api) return;

    let request: HrzProtocol.IPickRequest = {
        coords: { x: x, y: y },
        includedRasters: [],
    };

    let requestResult = await api.ViewerService.pickScreen(request);

    if (requestResult.hasATicket && requestResult.ticket) {
        msgHandler.awaitPickResult(requestResult.ticket, (pickResult: HrzProtocol.IPickResults) => {
            scheduleRasterDataFetch(pickResult.position || {});
        });
    }
}

async function scheduleRasterDataFetch(position: HrzProtocol.IGeographicPosition) {
    if (!layer.value) {
        return;
    }

    let rasterRequest: HrzProtocol.IRasterDataFetchRequest = {
        position: position,
        rasterLayers: [layer.value],
    };

    let fetchRequest = await api.ViewerService.fetchRasterData(rasterRequest);

    if (fetchRequest.hasATicket && fetchRequest.ticket) {
        msgHandler.awaitRasterDataFetchResult(
            fetchRequest.ticket,
            (result: HrzProtocol.IPickLayerResult[]) => {
                let foundLayerResult = result.find((layerResult) => {
                    return eqLong(layerResult.layer?.handle?.opaque, layer.value?.opaque);
                });
                if (foundLayerResult) {
                    if (
                        typeof foundLayerResult.raster?.number == "number" &&
                        !foundLayerResult.raster?.nodata
                    ) {
                        pickedRasterValue.value = (foundLayerResult.raster?.number || 0).toFixed(2);
                    } else {
                        pickedRasterValue.value = "no data";
                    }
                } else {
                    pickedRasterValue.value = null;
                }
            }
        );
    }
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Raster with palette</h1>
                <p>
                    Rasters containing scalar values can be visualized using a palette by wrapping
                    their provider inside a palettized raster provider.
                </p>
                <p>
                    In this example you can play with some parameters to see how they affect the
                    raster.
                </p>
                <p>
                    Additionally the palette itself can be modified, including how the colors are
                    distributed around the predefined values. One mode creates a contiguous
                    gradient, and the other sets constant colors between pairs of values. Both modes
                    can be combined at will and are expressed through the same structure. Look at
                    the palette model to see how it is represented.
                    <a href="../numeric_palettes.html"
                        >See the documentation for more information about palettes.</a
                    >
                </p>
                <p>
                    <label>
                        <input type="checkbox" v-model="useBilinearInterpolation" />
                        Use bilinear interpolation
                    </label>
                </p>
                <p>
                    <label>Color interpolation mode</label>
                    <select v-model.number="colorInterpolationMode">
                        <option
                            v-for="(value, name) in HrzProtocol.ColorInterpolationMode"
                            :value="value"
                            :key="value"
                        >
                            {{ name }}
                        </option>
                    </select>
                </p>
                <p>
                    <label>Palette mode</label>
                    <select v-model.number="paletteMode">
                        <option v-for="value in PaletteMode" :value="value" :key="value">
                            {{ value }}
                        </option>
                    </select>
                </p>
                <div v-if="paletteMode == PaletteMode.GRADIENT">
                    <p v-for="(_, index) in colors" :key="index" class="flex items-center">
                        <label class="w-[50px]">{{ colorValues[index] }}</label>
                        <ColorInput class="flex-1" v-model.debounce="colors[index]" />
                    </p>
                </div>
                <div v-else-if="paletteMode == PaletteMode.CATEGORIZED">
                    <p v-for="(_, index) in colors.slice(1)" :key="index" class="flex items-center">
                        <label class="w-[100px]"
                            >{{ colorValues[index] }} to {{ colorValues[index + 1] }}</label
                        >
                        <ColorInput class="flex-1" v-model.debounce="colors[index]" />
                    </p>
                </div>
                <p>
                    <FullscreenSource text="View demo source" file="source/PalettizedRaster.vue" />
                    <FullscreenSceneModel
                        text="View raster layer data"
                        :retrieveData="retrieveRasterLayerModel"
                    />
                    <FullscreenSceneModel
                        text="View palette model"
                        :retrieveData="retrievePaletteModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" @clickAt="schedulePick" />
            <div
                ref="popup"
                class="absolute w-72 left-4 top-4 shadow-lg rounded-xl p-4 text-mBodyMedium bg-surfaceContainerLow text-onSurface"
            >
                <span v-if="pickedRasterValue !== null"
                    >Picked raster value: <strong>{{ pickedRasterValue }}</strong></span
                >
                <span v-else>Click anywhere to query the data raster value.</span>
            </div>
        </template>
    </SplitView>
</template>
