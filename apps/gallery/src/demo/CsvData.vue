<!--
    SPDX-FileCopyrightText: Copyright 2025 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol, HrzProtocolHelper } from "@siradel-oss/horizon-protocol";
import { applyDefaultSymbolicBaseLayer, applyScene, getLayerByName } from "@/utils/scenes";
import { colorFromHex } from "@/utils/colors";
import { MessageHandler } from "@/utils/messages";
import FullscreenSource from "@/component/FullscreenSource.vue";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import IconButton from "@/component/IconButton.vue";
import { ref, watch, computed } from "vue";
import { debounce, eqLong } from "@/utils/utils";

// First level key is the IRIS code, second level key is the population category.
type PopStats = { [key: number]: { [key: string]: number } };

let popStats: PopStats = {};
async function loadCsvData() {
    let response = await fetch("assets/demo/rennes_iris_pop20.csv");
    if (!response.ok) {
        throw new Error(`Failed to load CSV data: ${response.statusText}`);
    }

    let text = await response.text();
    let lines = text.split("\n").filter((line) => line.trim() !== "");
    let stats: PopStats = {};

    // First line is the header, first column is the IRIS code, the rest are population categories.
    // First parse the header to get the population categories.
    let categories = lines[0]
        .split(";")
        .slice(1)
        .map((cat) => cat.trim());

    // Load all the data lines.
    for (let i = 1; i < lines.length; i++) {
        let line = lines[i].split(";").map((value) => value.trim());
        if (line.length < categories.length + 1) continue; // Skip empty or malformed lines

        let irisCode = parseInt(line[0], 10);
        if (isNaN(irisCode)) continue; // Skip invalid IRIS codes

        stats[irisCode] = {};
        for (let j = 1; j < line.length; j++) {
            let category = categories[j - 1];
            let value = parseFloat(line[j]);
            if (!isNaN(value)) {
                stats[irisCode][category] = value;
            }
        }
    }

    return stats;
}

interface PickResult {
    cityName: string;
    irisName: string;
    stats: { [key: string]: string };
}

const POP_STATS_ATTRIBUTION = "Insee";
const POP_TOTAL_COLUMN_NAME = "P20_POP";

const POP_CATEGORY_NAMES: { [key: string]: string } = {
    P20_POP: "Total population",
    P20_POP0014: "Population 0-14 years",
    P20_POP1529: "Population 15-29 years",
    P20_POP3044: "Population 30-44 years",
    P20_POP4559: "Population 45-59 years",
    P20_POP6074: "Population 60-74 years",
    P20_POP75P: "Population 75 years and older",
};

const POP_TOTAL_ATTR_NAME = "pop_total";
const POP_TOTAL_ATTR_ID = 10;
const POP_CATEGORY_ATTR_NAME = "pop_category";
const POP_CATEGORY_ATTR_ID = 11;
const IRIS_AREA_ATTR_NAME = "area";
const IRIS_AREA_ATTR_ID = 1;
const CITY_NAME_ATTR_ID = 2;
const IRIS_NAME_ATTR_ID = 3;

interface DemoSetup {
    name: string;
    popCategory: string | null;
    maxPaletteValue: number;
    valueExpr: string;
    formatLegendValue: (value: number, isFirst: boolean, isLast: boolean) => string;
}

function formatPercentage(value: number, isFirst: boolean, isLast: boolean): string {
    return (isLast ? "≥ " : "") + Math.floor(value).toFixed(0) + "%";
}

const DEMO_SETUPS: DemoSetup[] = [
    {
        name: "Total population",
        popCategory: null,
        maxPaletteValue: 7000,
        valueExpr: `attr("${POP_TOTAL_ATTR_NAME}")`,
        formatLegendValue: (value: number, isFirst: boolean, isLast: boolean) => {
            return Math.floor(value).toFixed(0) + (isLast ? "+" : "");
        },
    },
    {
        name: "Population density (per km²)",
        popCategory: null,
        maxPaletteValue: 0.001,
        valueExpr: `div(attr("${POP_TOTAL_ATTR_NAME}"), attr("${IRIS_AREA_ATTR_NAME}"))`,
        formatLegendValue: (value: number, isFirst: boolean, isLast: boolean) => {
            return Math.floor(value * 1000000).toFixed(0) + (isLast ? "+" : "");
        },
    },
    {
        name: "Population 0-14 years (%)",
        popCategory: "P20_POP0014",
        maxPaletteValue: 50,
        valueExpr: `mul(div(attr("${POP_CATEGORY_ATTR_NAME}"), attr("${POP_TOTAL_ATTR_NAME}")) or 0, 100)`,
        formatLegendValue: formatPercentage,
    },
    {
        name: "Population 15-29 years (%)",
        popCategory: "P20_POP1529",
        maxPaletteValue: 50,
        valueExpr: `mul(div(attr("${POP_CATEGORY_ATTR_NAME}"), attr("${POP_TOTAL_ATTR_NAME}")) or 0, 100)`,
        formatLegendValue: formatPercentage,
    },
    {
        name: "Population 30-44 years (%)",
        popCategory: "P20_POP3044",
        maxPaletteValue: 50,
        valueExpr: `mul(div(attr("${POP_CATEGORY_ATTR_NAME}"), attr("${POP_TOTAL_ATTR_NAME}")) or 0, 100)`,
        formatLegendValue: formatPercentage,
    },
    {
        name: "Population 45-59 years (%)",
        popCategory: "P20_POP4559",
        maxPaletteValue: 50,
        valueExpr: `mul(div(attr("${POP_CATEGORY_ATTR_NAME}"), attr("${POP_TOTAL_ATTR_NAME}")) or 0, 100)`,
        formatLegendValue: formatPercentage,
    },
    {
        name: "Population 60-74 years (%)",
        popCategory: "P20_POP6074",
        maxPaletteValue: 50,
        valueExpr: `mul(div(attr("${POP_CATEGORY_ATTR_NAME}"), attr("${POP_TOTAL_ATTR_NAME}")) or 0, 100)`,
        formatLegendValue: formatPercentage,
    },
    {
        name: "Population 75 years and older (%)",
        popCategory: "P20_POP75P",
        maxPaletteValue: 50,
        valueExpr: `mul(div(attr("${POP_CATEGORY_ATTR_NAME}"), attr("${POP_TOTAL_ATTR_NAME}")) or 0, 100)`,
        formatLegendValue: formatPercentage,
    },
];

let api: HrzApi.AsyncApi;
let msgHandler: MessageHandler;
let vectorDataLayer: HrzProtocol.LayerHandle | undefined;
let vectorTilesLayer: HrzProtocol.LayerHandle | undefined;
let selectedSetup = ref<number>();
let legendValues = ref<HTMLDivElement | null>(null);
let legendColors = ref<HTMLDivElement | null>(null);
let pickResult = ref<PickResult>({ cityName: "", irisName: "", stats: {} });
let selectedIrisId = ref<number | null>(null);
let useExtrusion = ref(false);
let extrusionMultiplier = ref(1.0);

const TURBO_COLORS = ["#4058D3", "#3FA6E8", "#51F0B0", "#A5F070", "#FADA63", "#FC893D", "#E04025"];

function generatePalette(min: number, max: number): HrzProtocol.NumericPalette {
    let palette = HrzProtocol.NumericPalette.create();
    palette.interpolationMode = HrzProtocol.ColorInterpolationMode.OKLAB;

    for (let i = 0; i < TURBO_COLORS.length; i++) {
        const value = min + (max - min) * (i / (TURBO_COLORS.length - 1));
        const color = colorFromHex(TURBO_COLORS[i]);
        palette.colorStops.push({
            firstColor: color,
            secondColor: color,
            value: value,
        });
    }

    return palette;
}

async function respondToVectorDataRequest(msg: HrzProtocol.VectorDataRequestMessage.$Shape) {
    let response = HrzProtocol.VectorDataRequestResponse.create({
        ticket: msg.ticket,
        features: [],
    });

    let expectedAttributes: string[] = [];
    for (const attrId of msg.attributeIds || []) {
        if (attrId == POP_TOTAL_ATTR_ID) {
            expectedAttributes.push(POP_TOTAL_COLUMN_NAME);
        } else if (attrId == POP_CATEGORY_ATTR_ID) {
            expectedAttributes.push(
                DEMO_SETUPS[selectedSetup.value || 0].popCategory || POP_TOTAL_COLUMN_NAME
            );
        } else {
            expectedAttributes.push("<UNKNOWN>");
        }
    }

    for (const featureId of msg.featureIdSelection?.featureIds || []) {
        const irisId = HrzProtocolHelper.attributeAsNumber(
            (featureId.attributes || [])[0].value || {}
        );
        const irisAttributes = popStats[irisId] || {};
        const feature = HrzProtocol.ClientFeature.create();
        for (const attrName of expectedAttributes) {
            feature.attributeValues.push({
                numberValue: irisAttributes[attrName] || NaN,
            });
        }
        response.features?.push(feature);
    }

    response.attribution = POP_STATS_ATTRIBUTION;

    await api.ClientDataService.provideVectorData(response);
}

const stylingScript = computed(() => {
    const setup = DEMO_SETUPS[selectedSetup.value || 0];

    let script = `if (is_nan(attr("${POP_TOTAL_ATTR_NAME}"))) {
    discard;
}
`;
    if (useExtrusion.value) {
        script += `set "up_color" = alpha(colorize("palette",
    ${setup.valueExpr}), 0.6);
set "low_color" = darken(prp("up_color"), 0.25);
set "outline_color" = alpha(prp("low_color"), 1.0);
set "extrusion" = mul(${setup.valueExpr}, ${
            (1000 * extrusionMultiplier.value) / setup.maxPaletteValue
        });
fork { emit 0; }
emit 2;`;
    } else {
        script += `set "fill_color" = alpha(colorize("palette",
    ${setup.valueExpr}), 0.5);
set "outline_color" = alpha(darken(prp("fill_color"), 0.25), 1.0);
fork { emit 1; }
emit 0;`;
    }

    return script;
});

watch(stylingScript, async () => {
    if (api && vectorTilesLayer) {
        debounce(async () => {
            await HrzApi.VectorTilesLayerPathBuilder.create(vectorTilesLayer!)
                .style()
                .stylingScript()
                .set(api, stylingScript.value);
        }, 200)();
    }
});

watch(selectedSetup, async (newSetup, oldSetup) => {
    if (api && vectorTilesLayer && newSetup !== undefined) {
        const setup = DEMO_SETUPS[newSetup];

        let palette = generatePalette(0, setup.maxPaletteValue);

        await HrzApi.VectorTilesLayerPathBuilder.create(vectorTilesLayer)
            .style()
            .palettes(0)
            .numeric()
            .set(api, palette);

        if (
            setup.popCategory &&
            (oldSetup === undefined || setup.popCategory !== DEMO_SETUPS[oldSetup].popCategory)
        ) {
            await api.ClientDataService.invalidateVectorData({
                vectorDataLayerId: 0,
                vectorDataSourceIndex: 1,
                everything: {},
            });
        }

        if (legendValues.value && legendColors.value && palette.colorStops) {
            legendValues.value.innerHTML = "";

            const colorStopCount = palette.colorStops.length || 0;

            for (let i = colorStopCount - 1; i >= 0; i--) {
                const value = palette.colorStops[i].value || 0;
                const valueDiv = document.createElement("div");
                valueDiv.textContent = setup.formatLegendValue(
                    value,
                    i === 0,
                    i === colorStopCount - 1
                );
                legendValues.value.appendChild(valueDiv);
            }

            for (let i = TURBO_COLORS.length - 2; i >= 0; i--) {
                const colorTop = TURBO_COLORS[i + 1];
                const colorBottom = TURBO_COLORS[i];
                const colorDiv = document.createElement("div");
                colorDiv.style.background = `linear-gradient(to bottom in oklab, ${colorTop}, ${colorBottom})`;
                legendColors.value.appendChild(colorDiv);
            }
        }
    }
});

watch(selectedIrisId, async (id) => {
    await api.ViewerService.deselectAllFeatures();
    if (id) {
        await api.ViewerService.selectFeatures({
            features: [
                {
                    layer: vectorTilesLayer,
                    featureId: {
                        attributes: [{ id: 0, value: { numberValue: id } }],
                    },
                },
            ],
        });
    }
});

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler_: MessageHandler) {
    api = api_;
    msgHandler = msgHandler_;

    await applyScene(api, "rennes_iris_pop").then(async () => {
        vectorDataLayer = await getLayerByName(api, "IRIS vector data");
        vectorTilesLayer = await getLayerByName(api, "IRIS vector tiles");
    });
    await applyDefaultSymbolicBaseLayer(api);

    await api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        pose: {
            bearing: 0,
            tilt: -Math.PI / 2,
            position: {
                latitude: 48.111558,
                longitude: -1.678848,
                altitude: 16000,
            },
        },
        maxAltitude: 10000000,
        minTilt: 0,
        maxTilt: Math.PI,
    });

    await api.ViewerService.configureMouseHover({
        enableHighlight: true,
        highlightRateMs: 33,
    });

    await HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
        .highlight()
        .mouseHoverHighlightColor()
        .set(api, { r: 0, g: 0, b: 0, a: 0.4 });

    popStats = await loadCsvData();

    selectedSetup.value = 0;

    msgHandler.watchForever((msg) => {
        if (
            msg.payload == "vectorDataRequest" &&
            msg.vectorDataRequest.vectorDataLayerId == 0 &&
            msg.vectorDataRequest.vectorDataSourceIndex == 1 &&
            msg.vectorDataRequest.featureIdSelection
        ) {
            respondToVectorDataRequest(msg.vectorDataRequest);
        }
    });
}

function displayPickResult(msg: HrzProtocol.PickResults.$Shape) {
    pickResult.value.cityName = "";
    selectedIrisId.value = null;

    const result = msg.results?.[0];
    if (!result || !eqLong(result.layer?.handle?.opaque, vectorTilesLayer?.opaque)) return;
    if (!result.vector || !result.vector.ids || !result.vector.values) return;

    let id = HrzProtocolHelper.attributeAsNumber(
        result.vector.featureId?.attributes?.[0]?.value || {}
    );
    selectedIrisId.value = id;

    pickResult.value.stats = {};

    for (let i = 0; i < result.vector.ids?.length; ++i) {
        const idValue = HrzProtocolHelper.uint64AsNumber(result.vector.ids[i]);
        switch (idValue) {
            case IRIS_NAME_ATTR_ID:
                pickResult.value.irisName = result.vector.values[i].stringValue || "";
                break;
            case CITY_NAME_ATTR_ID:
                pickResult.value.cityName = result.vector.values[i].stringValue || "";
                break;
            case IRIS_AREA_ATTR_ID:
                let area = HrzProtocolHelper.attributeAsNumber(result.vector.values[i]);
                if (area) {
                    pickResult.value.stats["Area"] = (area / 1000000).toFixed(1) + " km²";
                }
                break;
        }
    }

    let stats = popStats[id];
    if (stats) {
        for (const [key, value] of Object.entries(stats)) {
            if (value !== undefined && value !== null) {
                let count = value.toFixed(0);
                if (key === POP_TOTAL_COLUMN_NAME) {
                    pickResult.value.stats[POP_CATEGORY_NAMES[key]] = value.toFixed(0);
                } else {
                    let total = stats[POP_TOTAL_COLUMN_NAME];
                    let percentage = ((value / total) * 100).toFixed(0);
                    pickResult.value.stats[POP_CATEGORY_NAMES[key]] =
                        value.toFixed(0) + ` (${percentage}%)`;
                }
            }
        }
    }

    console.log(pickResult);
}

async function schedulePick(x: number, y: number) {
    if (!api || !msgHandler) return;
    let ticket = await api.ViewerService.pickScreen({ coords: { x, y } });

    if (ticket.hasATicket && ticket.ticket) {
        msgHandler.awaitPickResult(ticket?.ticket, displayPickResult);
    }
}

async function retrieveVectorDataLayerModel(): Promise<any> {
    if (!vectorDataLayer) {
        return {};
    }
    return (await HrzApi.VectorDataLayerPathBuilder.create(vectorDataLayer).get(api)).toJSON();
}

async function retrieveVectorTilesLayerModel(): Promise<any> {
    if (!vectorTilesLayer) {
        return {};
    }
    return (await HrzApi.VectorTilesLayerPathBuilder.create(vectorTilesLayer).get(api)).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h3>CSV data & histogram</h3>
                <p>
                    This example shows how to join CSV data with vector data in Horizon. The CSV
                    file contains population statistics for the IRIS areas of Rennes, France. The
                    data is loaded from a local file, sent to Horizon through a client vector data
                    provider, and joined with data from a PMTiles provider.
                </p>
                <p>
                    Of course one can imagine loading data from any source. It so happens that CSV
                    is a fairly common encoding for tabular data, hence why it is illustrated here.
                </p>
                <p>Additionally, each IRIS region can be clicked to display some statistics.</p>
                <hr />
                <p>
                    <label>Choose a metric to display</label>
                    <select v-model.number="selectedSetup">
                        <option v-for="(setup, index) in DEMO_SETUPS" :key="index" :value="index">
                            {{ setup.name }}
                        </option>
                    </select>
                </p>
                <p></p>
                <p>
                    <label
                        ><input type="checkbox" v-model="useExtrusion" /> Display as
                        histogram</label
                    >
                </p>
                <p v-if="useExtrusion">
                    <label>Extrusion multiplier (x {{ extrusionMultiplier.toFixed(2) }})</label>
                    <input
                        type="range"
                        v-model.number="extrusionMultiplier"
                        min="0.1"
                        max="2"
                        step="any"
                    />
                </p>
                <p><label>Styling script</label></p>
                <pre>{{ stylingScript }}</pre>
                <hr />
                <p>
                    <FullscreenSource file="source/CsvData.vue" />
                    <FullscreenSceneModel
                        text="View vector data layer model"
                        :retrieveData="retrieveVectorDataLayerModel"
                    />
                    <FullscreenSceneModel
                        text="View vector tiles layer model"
                        :retrieveData="retrieveVectorTilesLayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" @clickAt="schedulePick" />
            <div
                class="absolute bottom-4 left-4 p-4 rounded-lg shadow-xl bg-surfaceContainerLow text-onSurface"
                v-show="selectedIrisId"
            >
                <div class="relative">
                    <div class="absolute top-[-12px] right-[-12px]">
                        <IconButton
                            icon="close"
                            color="onSurface"
                            class="text-[24px]"
                            @click="selectedIrisId = null"
                        />
                    </div>
                    <h3 class="text-mTitleMedium text-onSurfaceVariant">
                        {{ pickResult.cityName
                        }}<span v-show="pickResult.cityName != pickResult.irisName">
                            ({{ pickResult.irisName }})</span
                        >
                    </h3>
                    <table class="mt-2 metadata-table w-full">
                        <tr v-for="(value, key) in pickResult.stats" :key="key">
                            <td class="text-mLabelLarge p-2">{{ key }}</td>
                            <td class="text-mBodyMedium p-2">{{ value }}</td>
                        </tr>
                    </table>
                </div>
            </div>
            <div class="absolute top-4 right-4 bottom-44 flex flex-col justify-center">
                <div
                    class="bg-surfaceContainerLow text-onSurface rounded-lg p-4 shadow-xl w-[160px]"
                >
                    <div class="grid grid-cols-2 h-[350px] gap-2">
                        <div
                            class="flex flex-col justify-between text-mLabelMedium text-right"
                            ref="legendValues"
                        ></div>
                        <div
                            class="grid grid-rows-6 w-[50px] justify-stretch align-stretch"
                            ref="legendColors"
                        ></div>
                    </div>
                    <h4 class="mt-4 text-center text-mLabelLarge">
                        {{ DEMO_SETUPS[selectedSetup || 0].name }}
                    </h4>
                </div>
            </div>
        </template>
    </SplitView>
</template>

<style scoped>
@reference "../style.css";

.metadata-table tr:nth-child(odd) > * {
    @apply bg-onSurfaceVariant/5 first:rounded-l-sm last:rounded-r-sm;
}
</style>
