<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { MessageHandler } from "@/utils/messages";
import { applyDefaultOrthoBaseLayer, applySceneTemplate, getLayerByName } from "@/utils/scenes";
import { ref, reactive, watch, computed } from "vue";
import { deepAssign } from "@/utils/utils";
import TextButton from "@/component/TextButton.vue";
import ColorInput from "@/component/ColorInput.vue";

// We provide sensible fallbacks for cases where some settings would be
// disabled by the graphics configuration.
let DEFAULT_VALUE: HrzProtocol.IAmbientSettings = {
    sunAmbientBalance: 0.5,
    lightingStrength: 1.0,
    wrapLighting: 0.0,
    ambientLighting: {
        mode: HrzProtocol.AmbientLightingMode.AMBIENT_LIGHTING_SIMULATED,
        staticColor: {
            r: 0.42,
            g: 0.42,
            b: 0.42,
            a: 1,
        },
    },
    lighting: {
        castShadows: true,
        enableLighting: true,
        receiveShadows: true,
    },
    primaryFog: {
        color: {},
    },
    secondaryFog: {
        color: {},
    },
    sky: {
        mode: HrzProtocol.SkyMode.SKY_SIMULATED,
        attenuation: 0.5,
        staticColor: {
            r: 0.8,
            g: 0.89,
            b: 0.92,
            a: 1,
        },
    },
    sun: {
        mode: HrzProtocol.SunLightingMode.SUN_LIGHTING_SIMULATED,
        direction: {
            mode: HrzProtocol.SunDirectionMode.SUN_DIRECTION_RELATIVE_TO_DATE,
            dayOfYear: 171,
            localSolarTime: 15,
        },
        staticColor: {
            r: 1,
            g: 1,
            b: 1,
            a: 1,
        },
    },
    undergroundColor: {
        r: 0.45,
        b: 0.44,
        g: 0.27,
        a: 1,
    },
};

DEFAULT_VALUE = HrzProtocol.AmbientSettings.toObject(
    HrzProtocol.AmbientSettings.fromObject(DEFAULT_VALUE),
    { defaults: true }
);

function copySettingsWithDefaultValues(
    settings: HrzProtocol.IAmbientSettings
): HrzProtocol.IAmbientSettings {
    let obj = structuredClone(DEFAULT_VALUE);
    deepAssign(settings, obj);
    return obj;
}

interface Preset {
    description: string;
    settings: HrzProtocol.IAmbientSettings;
}

let PRESETS: { [key: string]: Preset } = {};
PRESETS["Realistic, clear day"] = {
    description: `This is the "realistic" preset: the sun position and appearance
    and the atmosphere are all simulated based on a day of the year and the local
    solar time.`,
    settings: {
        lightingStrength: 1.0,
        sunAmbientBalance: 0.5,
        lighting: {
            enableLighting: true,
            castShadows: true,
            receiveShadows: true,
        },
        sky: {
            mode: HrzProtocol.SkyMode.SKY_SIMULATED,
        },
        sun: {
            mode: HrzProtocol.SunLightingMode.SUN_LIGHTING_SIMULATED,
            direction: {
                mode: HrzProtocol.SunDirectionMode.SUN_DIRECTION_RELATIVE_TO_DATE,
                localSolarTime: 12,
                dayOfYear: 180,
            },
        },
        ambientLighting: {
            mode: HrzProtocol.AmbientLightingMode.AMBIENT_LIGHTING_SIMULATED,
        },
    },
};

PRESETS["Simple & readable"] = {
    description: `This preset uses fog to limit how far the user can see.
    The lighting and the atmosphere are set by the user instead of being simulated.
    Additionally wrap lighting is used to improve readability in shadow areas.
    This preset can be useful for data visualization.`,
    settings: {
        lightingStrength: 1.0,
        sunAmbientBalance: 0.5,
        wrapLighting: 1.0,
        lighting: {
            enableLighting: true,
            castShadows: false,
            receiveShadows: false,
        },
        sky: {
            mode: HrzProtocol.SkyMode.SKY_STATIC,
            staticColor: { r: 75 / 255, g: 165 / 255, b: 210 / 255, a: 1 },
        },
        sun: {
            mode: HrzProtocol.SunLightingMode.SUN_LIGHTING_STATIC,
            staticColor: { r: 1, g: 1, b: 1, a: 1 },
            direction: {
                mode: HrzProtocol.SunDirectionMode.SUN_DIRECTION_RELATIVE_TO_TANGENTIAL_FRAME,
                azimuth: 2.36,
                altitude: 0.94,
            },
        },
        ambientLighting: {
            mode: HrzProtocol.AmbientLightingMode.AMBIENT_LIGHTING_STATIC,
            staticColor: { r: 0.42, g: 0.42, b: 0.42, a: 1 },
        },
        primaryFog: {
            color: { r: 1, g: 1, b: 1, a: 1 },
            density: 0.5,
            applyToSky: true,
            startDistance: 2000.0,
            falloffStart: 0.0,
            falloffEnd: 1000.0,
        },
    },
};

PRESETS["Spotlight"] = {
    description: `This preset shows two interesting things.
    The first is a halo-like effect that focuses the scene close to the camera
    using a dark fog in the background. The second is the use of a sun position that is fixed
    to the camera. This means that shadows turn with the camera. Although this is unrealistic,
    it can be used to provide visual depth information no matter the camera angle. This is
    typically used in strategy games.`,
    settings: {
        lightingStrength: 1.0,
        sunAmbientBalance: 0.9,
        wrapLighting: 0,
        lighting: {
            enableLighting: true,
            castShadows: true,
            receiveShadows: true,
        },
        sky: {
            mode: HrzProtocol.SkyMode.SKY_SIMULATED,
        },
        sun: {
            mode: HrzProtocol.SunLightingMode.SUN_LIGHTING_STATIC,
            staticColor: { r: 1, g: 1, b: 1, a: 1 },
            direction: {
                mode: HrzProtocol.SunDirectionMode.SUN_DIRECTION_RELATIVE_TO_TANGENTIAL_FRAME,
                azimuth: 3.14,
                altitude: 0.6,
            },
        },
        ambientLighting: {
            mode: HrzProtocol.AmbientLightingMode.AMBIENT_LIGHTING_SIMULATED,
        },
        primaryFog: {
            color: { r: 0.12, g: 0.13, b: 0.2, a: 0.96 },
            density: 5,
            applyToSky: true,
            startDistance: 1500.0,
            falloffStart: 0.0,
            falloffEnd: 30000.0,
        },
    },
};

PRESETS["Foggy day"] = {
    description: `This preset uses two fog layers to give the impression of a foggy, rainy day.
    Although not particularly attractive, this shows how the look and feel of scenes can be
    dramatically adjusted.`,
    settings: {
        lightingStrength: 1.0,
        sunAmbientBalance: 0.0,
        lighting: {
            enableLighting: true,
            castShadows: true,
            receiveShadows: true,
        },
        sky: {
            mode: HrzProtocol.SkyMode.SKY_STATIC,
            staticColor: { r: 0.83, g: 0.83, b: 0.83, a: 1 },
        },
        sun: {
            mode: HrzProtocol.SunLightingMode.SUN_LIGHTING_STATIC,
            staticColor: { r: 1, g: 1, b: 1, a: 1 },
            direction: {
                mode: HrzProtocol.SunDirectionMode.SUN_DIRECTION_RELATIVE_TO_CARDINAL_FRAME,
                azimuth: 0,
                altitude: 0.6,
            },
        },
        ambientLighting: {
            mode: HrzProtocol.AmbientLightingMode.AMBIENT_LIGHTING_STATIC,
            staticColor: { r: 0.47, g: 0.47, b: 0.47, a: 1 },
        },
        primaryFog: {
            color: { r: 0.6, g: 0.6, b: 0.6, a: 0.88 },
            density: 3,
            applyToSky: false,
            startDistance: 100.0,
            falloffStart: 0.0,
            falloffEnd: 150.0,
        },
        secondaryFog: {
            color: { r: 0.69, g: 0.69, b: 0.69, a: 0.95 },
            density: 1,
            applyToSky: true,
            startDistance: 1000.0,
            falloffStart: 0.0,
            falloffEnd: 3000.0,
        },
    },
};

PRESETS["Electric sheep"] = {
    description: `Similarly to the foggy preset, this preset uses two fog layers to
    give a highly stylized look to the scene, reminiscent of the movie Blade Runner 2049.`,
    settings: {
        lightingStrength: 1.0,
        sunAmbientBalance: 0.3,
        wrapLighting: 1.0,
        lighting: {
            enableLighting: true,
            castShadows: false,
            receiveShadows: false,
        },
        sky: {
            mode: HrzProtocol.SkyMode.SKY_STATIC,
            staticColor: { r: 0.83, g: 0.83, b: 0.83, a: 1 },
        },
        sun: {
            mode: HrzProtocol.SunLightingMode.SUN_LIGHTING_STATIC,
            staticColor: { r: 0.87, g: 0.75, b: 0.51, a: 1 },
            direction: {
                mode: HrzProtocol.SunDirectionMode.SUN_DIRECTION_RELATIVE_TO_DATE,
                localSolarTime: 17.07,
                dayOfYear: 180,
            },
        },
        ambientLighting: {
            mode: HrzProtocol.AmbientLightingMode.AMBIENT_LIGHTING_STATIC,
            staticColor: { r: 0.95, g: 0.68, b: 0.44, a: 1 },
        },
        primaryFog: {
            color: { r: 0.96, g: 0.47, b: 0.24, a: 1 },
            density: 10,
            applyToSky: false,
            startDistance: 0.0,
            falloffStart: 0.0,
            falloffEnd: 100.0,
        },
        secondaryFog: {
            color: { r: 0.94, g: 0.74, b: 0.44, a: 0.95 },
            density: 1,
            applyToSky: true,
            startDistance: 0.0,
            falloffStart: 0.0,
            falloffEnd: 5000.0,
        },
    },
};

let selectedPreset = ref<string>(Object.keys(PRESETS)[0]);
let settings = reactive<HrzProtocol.IAmbientSettings>(copySettingsWithDefaultValues(DEFAULT_VALUE));
let terrainLighting = reactive<HrzProtocol.ILightingSettings>({
    castShadows: true,
    enableLighting: true,
    receiveShadows: true,
});
let buildingsLighting = reactive<HrzProtocol.ILightingSettings>({
    castShadows: true,
    enableLighting: true,
    receiveShadows: true,
});
let api: HrzApi.AsyncApi;
let buildingsLayer: HrzProtocol.ILayerHandle;

watch(settings, async () => {
    if (api) {
        await HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
            .ambient()
            .set(api, settings);
    }
});

watch(terrainLighting, async () => {
    if (api) {
        await HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
            .terrain()
            .lighting()
            .set(api, terrainLighting);
    }
});

watch(buildingsLighting, async () => {
    if (api && buildingsLayer) {
        await HrzApi.VectorTilesLayerPathBuilder.create(buildingsLayer)
            .lighting()
            .set(api, buildingsLighting);
    }
});

watch(selectedPreset, async () => {
    if (selectedPreset.value in PRESETS) {
        deepAssign(copySettingsWithDefaultValues(PRESETS[selectedPreset.value].settings), settings);
    }
});

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler: MessageHandler) {
    api = api_;

    await applyDefaultOrthoBaseLayer(api);
    await applySceneTemplate(api, "rennes_buildings");

    buildingsLayer = (await getLayerByName(api, "Rennes buildings")) || {};

    deepAssign(copySettingsWithDefaultValues(PRESETS[selectedPreset.value].settings), settings);
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Ambiance</h1>
                <p>
                    The look and feel of a scene can be finely tuned using the ambient settings,
                    part of the scene view settings. Through this structure the lighting, shadows,
                    atmosphere, light position, fog, and much more, can be adjusted. Below are a few
                    presets that you can adjust at will.
                </p>
            </div>
            <div class="my-6">
                <label>Preset</label><br />
                <select v-model="selectedPreset">
                    <option v-for="(value, key) in PRESETS" :value="key">{{ key }}</option>
                    <option value="custom">Custom</option>
                </select>
            </div>
            <div class="typography-normal">
                <p v-if="selectedPreset !== 'custom'">
                    {{ PRESETS[selectedPreset].description }}
                </p>
                <p v-if="selectedPreset !== 'custom'">
                    <TextButton color="onSurfaceVariant" @click="selectedPreset = 'custom'"
                        >Customize</TextButton
                    >
                </p>
                <div v-if="selectedPreset === 'custom'">
                    <h3>Lighting</h3>
                    <p>
                        <label
                            >Lighting strength ({{
                                $filters.formatNumber(settings.lightingStrength || 0, 2)
                            }})</label
                        ><br />
                        <input
                            type="range"
                            min="0"
                            max="2"
                            step="any"
                            v-model.number="settings.lightingStrength"
                        />
                    </p>
                    <p>
                        <label
                            >Sun-ambient balance ({{
                                $filters.formatNumber(settings.sunAmbientBalance || 0, 2)
                            }})</label
                        ><br />
                        <input
                            type="range"
                            min="0"
                            max="1"
                            step="any"
                            v-model.number="settings.sunAmbientBalance"
                        />
                    </p>
                    <p>
                        <label
                            >Wrap lighting ({{
                                $filters.formatNumber((settings.wrapLighting || 0) * 100)
                            }}%)</label
                        ><br />
                        <input
                            type="range"
                            min="0"
                            max="1"
                            step="any"
                            v-model.number="settings.wrapLighting"
                        />
                    </p>
                    <table class="w-full text-center my-4">
                        <tr>
                            <th>Target</th>
                            <th>Lighting</th>
                            <th>Receive<br />shadows</th>
                            <th>Cast<br />shadows</th>
                        </tr>
                        <tr>
                            <td>Global</td>
                            <td>
                                <input
                                    type="checkbox"
                                    v-if="settings.lighting"
                                    v-model="settings.lighting.enableLighting"
                                />
                            </td>
                            <td>
                                <input
                                    type="checkbox"
                                    v-if="settings.lighting"
                                    v-model="settings.lighting.receiveShadows"
                                    :disabled="!settings.lighting.enableLighting"
                                />
                            </td>
                            <td>
                                <input
                                    type="checkbox"
                                    v-if="settings.lighting"
                                    v-model="settings.lighting.castShadows"
                                />
                            </td>
                        </tr>
                        <tr>
                            <td>Terrain</td>
                            <td>
                                <input
                                    type="checkbox"
                                    v-model="terrainLighting.enableLighting"
                                    :disabled="!settings.lighting?.enableLighting"
                                />
                            </td>
                            <td>
                                <input
                                    type="checkbox"
                                    v-model="terrainLighting.receiveShadows"
                                    :disabled="
                                        !settings.lighting?.enableLighting ||
                                        !terrainLighting.enableLighting ||
                                        !settings.lighting.receiveShadows
                                    "
                                />
                            </td>
                            <td>
                                <input
                                    type="checkbox"
                                    v-model="terrainLighting.castShadows"
                                    :disabled="!settings.lighting?.castShadows"
                                />
                            </td>
                        </tr>
                        <tr>
                            <td>Buildings</td>
                            <td>
                                <input
                                    type="checkbox"
                                    v-model="buildingsLighting.enableLighting"
                                    :disabled="!settings.lighting?.enableLighting"
                                />
                            </td>
                            <td>
                                <input
                                    type="checkbox"
                                    v-model="buildingsLighting.receiveShadows"
                                    :disabled="
                                        !settings.lighting?.enableLighting ||
                                        !buildingsLighting.enableLighting ||
                                        !settings.lighting.receiveShadows
                                    "
                                />
                            </td>
                            <td>
                                <input
                                    type="checkbox"
                                    v-model="buildingsLighting.castShadows"
                                    :disabled="!settings.lighting?.castShadows"
                                />
                            </td>
                        </tr>
                    </table>
                    <hr />
                    <h3>Ambient lighting</h3>
                    <p>
                        <select
                            v-if="settings.ambientLighting"
                            v-model.number="settings.ambientLighting.mode"
                        >
                            <option
                                :value="HrzProtocol.AmbientLightingMode.AMBIENT_LIGHTING_STATIC"
                            >
                                Static
                            </option>
                            <option
                                :value="HrzProtocol.AmbientLightingMode.AMBIENT_LIGHTING_SIMULATED"
                            >
                                Simulated
                            </option>
                        </select>
                    </p>
                    <p
                        v-show="
                            settings.ambientLighting?.mode ===
                            HrzProtocol.AmbientLightingMode.AMBIENT_LIGHTING_STATIC
                        "
                    >
                        <ColorInput
                            v-if="settings.ambientLighting?.staticColor"
                            v-model="settings.ambientLighting.staticColor"
                        />
                    </p>
                    <hr />
                    <h3>Sun lighting</h3>
                    <p>
                        <select v-if="settings.sun" v-model.number="settings.sun.mode">
                            <option :value="HrzProtocol.SunLightingMode.SUN_LIGHTING_STATIC">
                                Static
                            </option>
                            <option :value="HrzProtocol.SunLightingMode.SUN_LIGHTING_SIMULATED">
                                Simulated
                            </option>
                        </select>
                    </p>
                    <p
                        v-show="
                            settings.sun?.mode === HrzProtocol.SunLightingMode.SUN_LIGHTING_STATIC
                        "
                    >
                        <ColorInput
                            v-if="settings.sun?.staticColor"
                            v-model="settings.sun.staticColor"
                        />
                    </p>
                    <hr />
                    <h3>Sun direction</h3>
                    <p>
                        <select
                            v-if="settings.sun?.direction"
                            v-model.number="settings.sun.direction.mode"
                        >
                            <option
                                :value="HrzProtocol.SunDirectionMode.SUN_DIRECTION_RELATIVE_TO_DATE"
                            >
                                Relative to date
                            </option>
                            <option
                                :value="
                                    HrzProtocol.SunDirectionMode
                                        .SUN_DIRECTION_RELATIVE_TO_TANGENTIAL_FRAME
                                "
                            >
                                Relative to tangential frame
                            </option>
                            <option
                                :value="
                                    HrzProtocol.SunDirectionMode
                                        .SUN_DIRECTION_RELATIVE_TO_CARDINAL_FRAME
                                "
                            >
                                Relative to cardinal frame
                            </option>
                        </select>
                    </p>
                    <p
                        v-if="
                            settings.sun?.direction?.mode !==
                            HrzProtocol.SunDirectionMode.SUN_DIRECTION_RELATIVE_TO_DATE
                        "
                    >
                        <label
                            >Azimuth ({{
                                $filters.formatNumber(settings.sun?.direction?.azimuth || 0, 2)
                            }}
                            rad)</label
                        >
                        <input
                            type="range"
                            min="0"
                            max="6.2831"
                            step="any"
                            v-if="settings.sun?.direction"
                            v-model.number="settings.sun.direction.azimuth"
                        />
                    </p>
                    <p
                        v-if="
                            settings.sun?.direction?.mode !==
                            HrzProtocol.SunDirectionMode.SUN_DIRECTION_RELATIVE_TO_DATE
                        "
                    >
                        <label
                            >Altitude ({{
                                $filters.formatNumber(settings.sun?.direction?.altitude || 0, 2)
                            }}
                            rad)</label
                        >
                        <input
                            type="range"
                            min="-1.5707"
                            max="1.5707"
                            step="any"
                            v-if="settings.sun?.direction"
                            v-model.number="settings.sun.direction.altitude"
                        />
                    </p>
                    <p
                        v-if="
                            settings.sun?.direction?.mode ===
                            HrzProtocol.SunDirectionMode.SUN_DIRECTION_RELATIVE_TO_DATE
                        "
                    >
                        <label
                            >Day of year ({{
                                $filters.formatNumber(settings.sun?.direction?.dayOfYear || 0, 2)
                            }})</label
                        >
                        <input
                            type="range"
                            min="0"
                            max="365"
                            step="any"
                            v-if="settings.sun?.direction"
                            v-model.number="settings.sun.direction.dayOfYear"
                        />
                    </p>
                    <p
                        v-if="
                            settings.sun?.direction?.mode ===
                            HrzProtocol.SunDirectionMode.SUN_DIRECTION_RELATIVE_TO_DATE
                        "
                    >
                        <label
                            >Local solar time ({{
                                $filters.formatNumber(
                                    settings.sun?.direction?.localSolarTime || 0,
                                    2
                                )
                            }})</label
                        >
                        <input
                            type="range"
                            min="0"
                            max="24"
                            step="any"
                            v-if="settings.sun?.direction"
                            v-model.number="settings.sun.direction.localSolarTime"
                        />
                    </p>
                    <hr />
                    <h3>Sky</h3>
                    <p>
                        <select v-if="settings.sky" v-model.number="settings.sky.mode">
                            <option :value="HrzProtocol.SkyMode.SKY_STATIC">Static</option>
                            <option :value="HrzProtocol.SkyMode.SKY_SIMULATED">Simulated</option>
                        </select>
                    </p>
                    <p v-show="settings.sky?.mode === HrzProtocol.SkyMode.SKY_SIMULATED">
                        <label
                            >Atmosphere attenuation ({{
                                $filters.formatNumber((settings.sky?.attenuation || 0) * 100)
                            }}%)</label
                        >
                        <input
                            type="range"
                            min="0"
                            max="1"
                            step="any"
                            v-if="settings.sky"
                            v-model.number="settings.sky.attenuation"
                        />
                    </p>
                    <p v-show="settings.sky?.mode === HrzProtocol.SkyMode.SKY_STATIC">
                        <ColorInput
                            v-if="settings.sky?.staticColor"
                            v-model="settings.sky.staticColor"
                        />
                    </p>
                    <hr />
                    <h3>Primary fog</h3>
                    <p>
                        <label>Color and opacity</label>
                        <ColorInput
                            v-if="settings.primaryFog?.color"
                            v-model="settings.primaryFog.color"
                            alpha
                        />
                    </p>
                    <p>
                        <label
                            ><input
                                type="checkbox"
                                v-if="settings.primaryFog"
                                v-model="settings.primaryFog.applyToSky"
                            />
                            Apply to sky</label
                        >
                    </p>
                    <p>
                        <label
                            >Density ({{
                                $filters.formatNumber(settings.primaryFog?.density || 0, 1)
                            }})</label
                        >
                        <input
                            type="range"
                            min="0"
                            max="20"
                            step="any"
                            v-if="settings.primaryFog"
                            v-model.number="settings.primaryFog.density"
                        />
                    </p>
                    <p>
                        <label
                            >Start distance ({{
                                $filters.formatNumber(settings.primaryFog?.startDistance || 0)
                            }}
                            m)</label
                        >
                        <input
                            type="range"
                            min="0"
                            max="5000"
                            step="any"
                            v-if="settings.primaryFog"
                            v-model.number="settings.primaryFog.startDistance"
                        />
                    </p>
                    <p>
                        <label
                            >Falloff start altitude ({{
                                $filters.formatNumber(settings.primaryFog?.falloffStart || 0)
                            }}
                            m)</label
                        >
                        <input
                            type="range"
                            min="0"
                            max="3000"
                            step="any"
                            v-if="settings.primaryFog"
                            v-model.number="settings.primaryFog.falloffStart"
                        />
                    </p>
                    <p>
                        <label
                            >Falloff end altitude ({{
                                $filters.formatNumber(settings.primaryFog?.falloffEnd || 0)
                            }}
                            m)</label
                        >
                        <input
                            type="range"
                            min="0"
                            max="3000"
                            step="any"
                            v-if="settings.primaryFog"
                            v-model.number="settings.primaryFog.falloffEnd"
                        />
                    </p>
                    <hr />
                    <h3>Secondary fog</h3>
                    <p>
                        <label>Color and opacity</label>
                        <ColorInput
                            v-if="settings.secondaryFog?.color"
                            v-model="settings.secondaryFog.color"
                            alpha
                        />
                    </p>
                    <p>
                        <label
                            ><input
                                type="checkbox"
                                v-if="settings.secondaryFog"
                                v-model="settings.secondaryFog.applyToSky"
                            />
                            Apply to sky</label
                        >
                    </p>
                    <p>
                        <label
                            >Density ({{
                                $filters.formatNumber(settings.secondaryFog?.density || 0, 1)
                            }})</label
                        >
                        <input
                            type="range"
                            min="0"
                            max="20"
                            step="any"
                            v-if="settings.secondaryFog"
                            v-model.number="settings.secondaryFog.density"
                        />
                    </p>
                    <p>
                        <label
                            >Start distance ({{
                                $filters.formatNumber(settings.secondaryFog?.startDistance || 0)
                            }}
                            m)</label
                        >
                        <input
                            type="range"
                            min="0"
                            max="5000"
                            step="any"
                            v-if="settings.secondaryFog"
                            v-model.number="settings.secondaryFog.startDistance"
                        />
                    </p>
                    <p>
                        <label
                            >Falloff start altitude ({{
                                $filters.formatNumber(settings.secondaryFog?.falloffStart || 0)
                            }}
                            m)</label
                        >
                        <input
                            type="range"
                            min="0"
                            max="3000"
                            step="any"
                            v-if="settings.secondaryFog"
                            v-model.number="settings.secondaryFog.falloffStart"
                        />
                    </p>
                    <p>
                        <label
                            >Falloff end altitude ({{
                                $filters.formatNumber(settings.secondaryFog?.falloffEnd || 0)
                            }}
                            m)</label
                        >
                        <input
                            type="range"
                            min="0"
                            max="3000"
                            step="any"
                            v-if="settings.secondaryFog"
                            v-model.number="settings.secondaryFog.falloffEnd"
                        />
                    </p>
                </div>
                <p>
                    <FullscreenSource file="source/Ambiance.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
