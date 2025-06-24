<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { applyScene, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import { ref, watch } from "vue";

enum Palette {
    POLY_POLY_CONTINUOUS = "Polychromatic topography, polychromatic bathymetry, continuous",
    POLY_MONO_CONTINUOUS = "Polychromatic topography, monochromatic bathymetry, continuous",
    MONO_POLY_CONTINUOUS = "Monochromatic topography, polychromatic bathymetry, continuous",
    POLY_POLY_DISCRETE = "Polychromatic topography, polychromatic bathymetry, discrete",
    POLY_MONO_DISCRETE = "Polychromatic topography, monochromatic bathymetry, discrete",
    MONO_POLY_DISCRETE = "Monochromatic topography, polychromatic bathymetry, discrete",
    MONO_MONO = "Monochromatic topography, monochromatic bathymetry",
}

let bathymetryColorPoints = [
    {
        value: -10000,
        color: {
            r: 0.062745101749897,
            g: 0.21568627655506134,
            b: 0.2980392277240753,
            a: 1,
        },
    },
    {
        value: -9000,
        color: {
            r: 0.09019608050584793,
            g: 0.29411765933036804,
            b: 0.40392157435417175,
            a: 1,
        },
    },
    {
        value: -8000,
        color: {
            r: 0.10980392247438431,
            g: 0.3686274588108063,
            b: 0.5098039507865906,
            a: 1,
        },
    },
    {
        value: -7000,
        color: {
            r: 0.14509804546833038,
            g: 0.42352941632270813,
            b: 0.5686274766921997,
            a: 1,
        },
    },
    {
        value: -6000,
        color: {
            r: 0.1764705926179886,
            g: 0.47843137383461,
            b: 0.6392157077789307,
            a: 1,
        },
    },
    {
        value: -5000,
        color: {
            r: 0.21176470816135406,
            g: 0.5372549295425415,
            b: 0.7058823704719543,
            a: 1,
        },
    },
    {
        value: -4000,
        color: {
            r: 0.24313725531101227,
            g: 0.5921568870544434,
            b: 0.7764706015586853,
            a: 1,
        },
    },
    {
        value: -3500,
        color: {
            r: 0.27843138575553894,
            g: 0.6470588445663452,
            b: 0.843137264251709,
            a: 1,
        },
    },
    {
        value: -3000,
        color: {
            r: 0.29411765933036804,
            g: 0.6823529601097107,
            b: 0.8784313797950745,
            a: 1,
        },
    },
    {
        value: -2500,
        color: {
            r: 0.30980393290519714,
            g: 0.7215686440467834,
            b: 0.9098039269447327,
            a: 1,
        },
    },
    {
        value: -2000,
        color: {
            r: 0.3686274588108063,
            g: 0.7568627595901489,
            b: 0.9254902005195618,
            a: 1,
        },
    },
    {
        value: -1500,
        color: {
            r: 0.4313725531101227,
            g: 0.7843137383460999,
            b: 0.9529411792755127,
            a: 1,
        },
    },
    {
        value: -1000,
        color: {
            r: 0.49803921580314636,
            g: 0.8156862854957581,
            b: 0.9764705896377563,
            a: 1,
        },
    },
    {
        value: -750,
        color: {
            r: 0.49803921580314636,
            g: 0.8156862854957581,
            b: 0.9764705896377563,
            a: 1,
        },
    },
    {
        value: -500,
        color: {
            r: 0.6549019813537598,
            g: 0.8666666746139526,
            b: 1,
            a: 1,
        },
    },
    {
        value: -250,
        color: {
            r: 0.7019608020782471,
            g: 0.886274516582489,
            b: 1,
            a: 1,
        },
    },
    {
        value: -100,
        color: {
            r: 0.7607843279838562,
            g: 0.9098039269447327,
            b: 1,
            a: 1,
        },
    },
    {
        value: 0.001,
        color: {
            r: 0.8549019694328308,
            g: 0.9411764740943909,
            b: 1,
            a: 1,
        },
    },
];

let bathymetryMonochromeColor = {
    r: 0.3764705955982208,
    g: 0.6784313917160034,
    b: 0.8627451062202454,
    a: 1,
};

let topographyColorPoints = [
    {
        value: 0.001,
        color: {
            r: 0.5607843399047852,
            g: 0.9176470637321472,
            b: 0.6274510025978088,
            a: 1,
        },
    },
    {
        value: 50,
        color: {
            r: 0.5137255191802979,
            g: 0.8980392217636108,
            b: 0.6235294342041016,
            a: 1,
        },
    },
    {
        value: 100,
        color: {
            r: 0.40392157435417175,
            g: 0.8705882430076599,
            b: 0.572549045085907,
            a: 1,
        },
    },
    {
        value: 200,
        color: {
            r: 0.3019607961177826,
            g: 0.8392156958580017,
            b: 0.5215686559677124,
            a: 1,
        },
    },
    {
        value: 300,
        color: {
            r: 0.7764706015586853,
            g: 0.8666666746139526,
            b: 0.615686297416687,
            a: 1,
        },
    },
    {
        value: 400,
        color: {
            r: 0.8627451062202454,
            g: 0.9019607901573181,
            b: 0.6784313917160034,
            a: 1,
        },
    },
    {
        value: 500,
        color: {
            r: 0.9529411792755127,
            g: 0.9333333373069763,
            b: 0.7372549176216125,
            a: 1,
        },
    },
    {
        value: 750,
        color: {
            r: 0.8666666746139526,
            g: 0.8235294222831726,
            b: 0.6470588445663452,
            a: 1,
        },
    },
    {
        value: 1000,
        color: {
            r: 0.7803921699523926,
            g: 0.7176470756530762,
            b: 0.5568627715110779,
            a: 1,
        },
    },
    {
        value: 1500,
        color: {
            r: 0.6901960968971252,
            g: 0.6078431606292725,
            b: 0.47058823704719543,
            a: 1,
        },
    },
    {
        value: 2000,
        color: {
            r: 0.6313725709915161,
            g: 0.5372549295425415,
            b: 0.4431372582912445,
            a: 1,
        },
    },
    {
        value: 3000,
        color: {
            r: 0.5764706134796143,
            g: 0.46666666865348816,
            b: 0.4117647111415863,
            a: 1,
        },
    },
    {
        value: 4000,
        color: {
            r: 0.5176470875740051,
            g: 0.3960784375667572,
            b: 0.3843137323856354,
            a: 1,
        },
    },
    {
        value: 5000,
        color: {
            r: 0.6784313917160034,
            g: 0.6235294342041016,
            b: 0.6117647290229797,
            a: 1,
        },
    },
    {
        value: 6000,
        color: {
            r: 0.7333333492279053,
            g: 0.7019608020782471,
            b: 0.686274528503418,
            a: 1,
        },
    },
    {
        value: 7000,
        color: {
            r: 0.7843137383460999,
            g: 0.7764706015586853,
            b: 0.7647058963775635,
            a: 1,
        },
    },
    {
        value: 8000,
        color: {
            r: 0.843137264251709,
            g: 0.843137264251709,
            b: 0.843137264251709,
            a: 1,
        },
    },
];

let topographyMonochromeColor = {
    r: 0.5372549295425415,
    g: 0.843137264251709,
    b: 0.6313725709915161,
    a: 1,
};

let bathymetryTopometryCutoffValue = 0.001;

let api: HrzApi.AsyncApi;
let imageryLayer = ref<HrzProtocol.ILayerHandle | undefined>();
let dtmLayer = ref<HrzProtocol.ILayerHandle | undefined>();

let palette = ref<Palette>(Palette.POLY_POLY_CONTINUOUS);
let includeDtmLayer = ref(false);

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api = api_;
    applyScene(api, "hawaii_palettized_terrain").then(async function () {
        imageryLayer.value = await getLayerByName(api, "Palettised AWS Terrain Tiles");
        dtmLayer.value = await getLayerByName(api, "AWS Terrain Tiles");

        if (dtmLayer.value) {
            includeDtmLayer.value = await HrzApi.DtmRasterLayerPathBuilder.create(dtmLayer.value)
                .visible()
                .get(api);
        }
    });
}

function makeNumericPalette(palette: Palette): HrzProtocol.INumericPalette {
    let colorPoints: HrzProtocol.IColorPoint[] = [];

    switch (palette) {
        case Palette.POLY_POLY_CONTINUOUS:
            for (let i = 0; i < bathymetryColorPoints.length; i++) {
                colorPoints.push({
                    value: bathymetryColorPoints[i].value,
                    firstColor: bathymetryColorPoints[i].color,
                    secondColor: bathymetryColorPoints[i].color,
                });
            }
            colorPoints[colorPoints.length - 1].secondColor = topographyColorPoints[0].color;
            for (let i = 1; i < topographyColorPoints.length; i++) {
                colorPoints.push({
                    value: topographyColorPoints[i].value,
                    firstColor: topographyColorPoints[i].color,
                    secondColor: topographyColorPoints[i].color,
                });
            }
            break;
        case Palette.POLY_MONO_CONTINUOUS:
            for (let i = 0; i < topographyColorPoints.length; i++) {
                colorPoints.push({
                    value: topographyColorPoints[i].value,
                    firstColor: topographyColorPoints[i].color,
                    secondColor: topographyColorPoints[i].color,
                });
            }
            colorPoints[0].firstColor = bathymetryMonochromeColor;
            break;
        case Palette.MONO_POLY_CONTINUOUS:
            for (let i = 0; i < bathymetryColorPoints.length; i++) {
                colorPoints.push({
                    value: bathymetryColorPoints[i].value,
                    firstColor: bathymetryColorPoints[i].color,
                    secondColor: bathymetryColorPoints[i].color,
                });
            }
            colorPoints[colorPoints.length - 1].secondColor = topographyMonochromeColor;
            break;
        case Palette.POLY_POLY_DISCRETE:
            for (let i = 0; i < bathymetryColorPoints.length - 1; i++) {
                colorPoints.push({
                    value: bathymetryColorPoints[i].value,
                    firstColor: bathymetryColorPoints[i].color,
                    secondColor: bathymetryColorPoints[i + 1].color,
                });
            }
            colorPoints.push({
                value: bathymetryTopometryCutoffValue,
                firstColor: bathymetryColorPoints[bathymetryColorPoints.length - 1].color,
                secondColor: topographyColorPoints[0].color,
            });
            for (let i = 1; i < topographyColorPoints.length; i++) {
                colorPoints.push({
                    value: topographyColorPoints[i].value,
                    firstColor: topographyColorPoints[i - 1].color,
                    secondColor: topographyColorPoints[i].color,
                });
            }
            break;
        case Palette.POLY_MONO_DISCRETE:
            colorPoints.push({
                value: bathymetryTopometryCutoffValue,
                firstColor: bathymetryMonochromeColor,
                secondColor: topographyColorPoints[0].color,
            });
            for (let i = 1; i < topographyColorPoints.length; i++) {
                colorPoints.push({
                    value: topographyColorPoints[i].value,
                    firstColor: topographyColorPoints[i - 1].color,
                    secondColor: topographyColorPoints[i].color,
                });
            }
            break;
        case Palette.MONO_POLY_DISCRETE:
            for (let i = 0; i < bathymetryColorPoints.length - 1; i++) {
                colorPoints.push({
                    value: bathymetryColorPoints[i].value,
                    firstColor: bathymetryColorPoints[i].color,
                    secondColor: bathymetryColorPoints[i + 1].color,
                });
            }
            colorPoints.push({
                value: bathymetryTopometryCutoffValue,
                firstColor: bathymetryColorPoints[bathymetryColorPoints.length - 1].color,
                secondColor: topographyMonochromeColor,
            });
            break;
        case Palette.MONO_MONO:
            colorPoints.push({
                value: bathymetryTopometryCutoffValue,
                firstColor: bathymetryMonochromeColor,
                secondColor: topographyMonochromeColor,
            });
            break;
        default:
            break;
    }

    return HrzProtocol.NumericPalette.create({
        colorPoints: colorPoints,
        nanColor: {},
    });
}

watch([palette], async function () {
    if (!imageryLayer.value) {
        return;
    }

    await HrzApi.ImageryRasterLayerPathBuilder.create(imageryLayer.value)
        .raster()
        .provider()
        .palettized()
        .palette()
        .set(api, makeNumericPalette(palette.value));
});

watch(includeDtmLayer, async function (includeDtmLayer) {
    if (dtmLayer.value) {
        await HrzApi.DtmRasterLayerPathBuilder.create(dtmLayer.value)
            .visible()
            .set(api, includeDtmLayer ? true : false);
    }
});

async function retrieveImageryRasterLayerModel(): Promise<any> {
    if (!imageryLayer.value) {
        return {};
    }
    return (
        await HrzApi.ImageryRasterLayerPathBuilder.create(imageryLayer.value).get(api)
    ).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Palettised topography and bathymetry</h1>
                <p>
                    This demo uses a worldwide terrain heightmap, that has data for both on-land
                    topography and undersea bathymetry. The heightmap is used as a DTM layer, as
                    well as a data source for a palettised imagery layer.
                </p>
                <p>
                    Any dataset suitable for a DTM layer can also be used as datasource for a
                    palettised imagery layer. The palette can be adjusted at runtime to suit the
                    visualisation needs.
                </p>
                <p>
                    <label>Palette</label>
                    <select v-model.number="palette">
                        <option v-for="value in Palette" :value="value" :key="value">
                            {{ value }}
                        </option>
                    </select>
                </p>
                <p>
                    <label>
                        <input type="checkbox" v-model="includeDtmLayer" />
                        Include DTM layer
                    </label>
                </p>
                <p>
                    <FullscreenSceneModel
                        text="View palettised imagery layer definition"
                        :retrieveData="retrieveImageryRasterLayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
