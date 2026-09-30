<!--
    SPDX-FileCopyrightText: Copyright 2025 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { MessageHandler } from "@/utils/messages";
import { applyDefaultOrthoBaseLayer, applySceneTemplate, getLayerByName } from "@/utils/scenes";
import { ref, reactive, computed, watch } from "vue";
import { deepAssign } from "@/utils/utils";
import Long from "long";
import ColorButton from "@/component/ColorButton.vue";
import ColorInput from "@/component/ColorInput.vue";

// We provide sensible fallbacks for cases where some settings would be
// disabled by the graphics configuration.
let DEFAULT_VALUE: HrzProtocol.AmbientSettings.$Properties = HrzProtocol.AmbientSettings.toObject(
    HrzProtocol.AmbientSettings.fromObject({
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
            staticAtmosphereColor: {
                r: 0.8,
                g: 0.89,
                b: 0.92,
                a: 1,
            },
            staticSpaceColor: {
                r: 0.0,
                g: 0.0,
                b: 0.0,
                a: 1,
            },
            staticColorTransitionStartDistance: 15000,
            staticColorTransitionEndDistance: 60000,
            staticColorTransitionDistanceUnit:
                HrzProtocol.StaticSkyColorTransitionUnit.STATIC_SKY_COLOR_TRANSITION_UNIT_METERS,
        },
        sun: {
            mode: HrzProtocol.SunLightingMode.SUN_LIGHTING_SIMULATED,
            solarDate: {
                dayOfYear: 171,
                solarTime: 15,
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
    }),
    { defaults: true }
);

function copySettingsWithDefaultValues(
    settings: HrzProtocol.AmbientSettings.$Properties
): HrzProtocol.AmbientSettings.$Properties {
    let obj = structuredClone(DEFAULT_VALUE);
    deepAssign(settings, obj);
    return obj;
}

interface Preset {
    description: string;
    settings: HrzProtocol.AmbientSettings.$Properties;
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
            solarDate: {
                solarTime: 12,
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
            staticAtmosphereColor: { r: 75 / 255, g: 165 / 255, b: 210 / 255, a: 1 },
            staticSpaceColor: {
                r: 0.0,
                g: 0.0,
                b: 0.0,
                a: 1,
            },
            staticColorTransitionStartDistance: 15000,
            staticColorTransitionEndDistance: 60000,
            staticColorTransitionDistanceUnit:
                HrzProtocol.StaticSkyColorTransitionUnit.STATIC_SKY_COLOR_TRANSITION_UNIT_METERS,
        },
        sun: {
            mode: HrzProtocol.SunLightingMode.SUN_LIGHTING_STATIC,
            staticColor: { r: 1, g: 1, b: 1, a: 1 },
            angularDirection: {
                cameraFrame: HrzProtocol.AngularDirection.Frame.FRAME_CAMERA_HEADING,
                azimuth: 3.92,
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
            angularDirection: {
                cameraFrame: HrzProtocol.AngularDirection.Frame.FRAME_CAMERA_HEADING,
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
            staticAtmosphereColor: { r: 0.83, g: 0.83, b: 0.83, a: 1 },
            staticSpaceColor: {
                r: 0.0,
                g: 0.0,
                b: 0.0,
                a: 1,
            },
            staticColorTransitionStartDistance: 15000,
            staticColorTransitionEndDistance: 60000,
            staticColorTransitionDistanceUnit:
                HrzProtocol.StaticSkyColorTransitionUnit.STATIC_SKY_COLOR_TRANSITION_UNIT_METERS,
        },
        sun: {
            mode: HrzProtocol.SunLightingMode.SUN_LIGHTING_STATIC,
            staticColor: { r: 1, g: 1, b: 1, a: 1 },
            angularDirection: {
                cameraFrame: HrzProtocol.AngularDirection.Frame.FRAME_ENU,
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
            staticAtmosphereColor: { r: 0.83, g: 0.83, b: 0.83, a: 1 },
            staticSpaceColor: {
                r: 0.0,
                g: 0.0,
                b: 0.0,
                a: 1,
            },
            staticColorTransitionStartDistance: 15000,
            staticColorTransitionEndDistance: 60000,
            staticColorTransitionDistanceUnit:
                HrzProtocol.StaticSkyColorTransitionUnit.STATIC_SKY_COLOR_TRANSITION_UNIT_METERS,
        },
        sun: {
            mode: HrzProtocol.SunLightingMode.SUN_LIGHTING_STATIC,
            staticColor: { r: 0.87, g: 0.75, b: 0.51, a: 1 },
            solarDate: {
                solarTime: 17.07,
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
let settings = reactive<HrzProtocol.AmbientSettings.$Properties>(
    copySettingsWithDefaultValues(DEFAULT_VALUE)
);
let terrainLighting = reactive<HrzProtocol.LightingSettings.$Properties>({
    castShadows: true,
    enableLighting: true,
    receiveShadows: true,
});
let buildingsLighting = reactive<HrzProtocol.LightingSettings.$Properties>({
    castShadows: true,
    enableLighting: true,
    receiveShadows: true,
});

type SunDirection = "solarDate" | "calendarDate" | "unixTimeMs" | "angularDirection";

function sunDirectionOf(sun: HrzProtocol.SunSettings.$Properties | null | undefined): SunDirection {
    if (sun?.calendarDate) return "calendarDate";
    if (sun?.unixTimeMs != null) return "unixTimeMs";
    if (sun?.angularDirection) return "angularDirection";
    return "solarDate";
}

function keepOnlySunDirection(sun: HrzProtocol.SunSettings.$Properties, keep: SunDirection) {
    if (keep !== "solarDate") delete sun.solarDate;
    if (keep !== "calendarDate") delete sun.calendarDate;
    if (keep !== "unixTimeMs") delete sun.unixTimeMs;
    if (keep !== "angularDirection") delete sun.angularDirection;
}

const sunDirection = computed<SunDirection>({
    get: () => sunDirectionOf(settings.sun),
    set: (direction) => {
        const sun = settings.sun;
        if (!sun) return;

        keepOnlySunDirection(sun, direction);

        switch (direction) {
            case "solarDate":
                sun.solarDate ??= { solarTime: 15, dayOfYear: 171 };
                break;
            case "calendarDate":
                sun.calendarDate ??= { year: 2025, month: 6, day: 21, time: 15, utcOffset: 2 };
                break;
            case "unixTimeMs":
                // 2025-06-21 13:00 UTC, matching the other defaults.
                sun.unixTimeMs ??= 1750510800000;
                break;
            case "angularDirection":
                sun.angularDirection ??= {
                    cameraFrame: HrzProtocol.AngularDirection.Frame.FRAME_ENU,
                    azimuth: 3.14,
                    altitude: 0.6,
                };
                break;
        }
    },
});

// Either a frame attached to the camera, or a geographic position.
type AngularReference = HrzProtocol.AngularDirection.Frame | "geographicPosition";

function angularReferenceOf(
    direction: HrzProtocol.AngularDirection.$Properties | null | undefined
): AngularReference {
    if (direction?.geographicPosition) return "geographicPosition";
    return direction?.cameraFrame ?? HrzProtocol.AngularDirection.Frame.FRAME_ENU;
}

function keepOnlyAngularReference(
    direction: HrzProtocol.AngularDirection.$Properties,
    keep: AngularReference
) {
    if (keep === "geographicPosition") {
        delete direction.cameraFrame;
    } else {
        delete direction.geographicPosition;
    }
}

const angularReference = computed<AngularReference>({
    get: () => angularReferenceOf(settings.sun?.angularDirection),
    set: (reference) => {
        const direction = settings.sun?.angularDirection;
        if (!direction) return;

        keepOnlyAngularReference(direction, reference);

        if (reference === "geographicPosition") {
            direction.geographicPosition ??= { latitude: 48.11, longitude: -1.68 };
        } else {
            direction.cameraFrame = reference;
        }
    },
});

type TimeOfYear = "solarLongitude" | "dayOfYear";
type DateWithTimeOfYear = { solarLongitude?: number | null; dayOfYear?: number | null };

function timeOfYearOf(date: DateWithTimeOfYear | null | undefined): TimeOfYear {
    return date?.solarLongitude != null ? "solarLongitude" : "dayOfYear";
}

function keepOnlyTimeOfYear(date: DateWithTimeOfYear, keep: TimeOfYear) {
    if (keep !== "solarLongitude") delete date.solarLongitude;
    if (keep !== "dayOfYear") delete date.dayOfYear;
}

function useTimeOfYear<T extends DateWithTimeOfYear>(date: () => T | null | undefined) {
    return computed<TimeOfYear>({
        get: () => timeOfYearOf(date()),
        set: (choice) => {
            const value = date();
            if (!value) return;

            keepOnlyTimeOfYear(value, choice);

            if (choice === "solarLongitude") {
                value.solarLongitude ??= 90;
            } else {
                value.dayOfYear ??= 171;
            }
        },
    });
}

const solarDateTimeOfYear = useTimeOfYear(() => settings.sun?.solarDate);

function formatTimeOfDay(hours: number): string {
    const totalMinutes = Math.round(hours * 60);
    const h = Math.floor(totalMinutes / 60) % 24;
    const m = totalMinutes % 60;
    return `${h.toString().padStart(2, "0")}:${m.toString().padStart(2, "0")}`;
}

const unixTimeLabel = computed(() => {
    const value = settings.sun?.unixTimeMs;
    if (value == null) return "";

    const unixTimeMs = Long.isLong(value) ? value.toNumber() : value;

    // The Date constructor accepts up to 8.64e15 milliseconds either way.
    if (!Number.isFinite(unixTimeMs) || Math.abs(unixTimeMs) > 8.64e15) return "";

    return new Date(unixTimeMs).toISOString().replace("T", " ").slice(0, 16) + " UTC";
});

let api: HrzApi.AsyncApi | undefined;
let buildingsLayer: HrzProtocol.LayerHandle | undefined;

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

function applyPreset(name: string) {
    const preset = PRESETS[name].settings;
    deepAssign(copySettingsWithDefaultValues(preset), settings);

    // `deepAssign` only ever merges, so the direction of the previous preset, and the
    // one in DEFAULT_VALUE, would otherwise stay set next to the one the preset asks
    // for. The nested time of year and angular reference have exactly the same problem.
    if (settings.sun) {
        keepOnlySunDirection(settings.sun, sunDirectionOf(preset.sun));
    }
    if (settings.sun?.solarDate) {
        settings.sun.solarDate.atPrimeMeridian = !!preset.sun?.solarDate?.atPrimeMeridian;
        keepOnlyTimeOfYear(settings.sun.solarDate, timeOfYearOf(preset.sun?.solarDate));
    }
    if (settings.sun?.angularDirection) {
        keepOnlyAngularReference(
            settings.sun.angularDirection,
            angularReferenceOf(preset.sun?.angularDirection)
        );
    }
}

watch(selectedPreset, async () => {
    if (selectedPreset.value in PRESETS) {
        applyPreset(selectedPreset.value);
    }
});

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler: MessageHandler) {
    api = api_;

    await applyDefaultOrthoBaseLayer(api);
    await applySceneTemplate(api, "rennes_buildings");

    buildingsLayer = await getLayerByName(api, "Rennes buildings");

    applyPreset(selectedPreset.value);
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
            <hr />
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
                    <ColorButton icon="edit" @click="selectedPreset = 'custom'"
                        >Customize</ColorButton
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
                        <thead>
                            <tr>
                                <th>Target</th>
                                <th>Lighting</th>
                                <th>Receive<br />shadows</th>
                                <th>Cast<br />shadows</th>
                            </tr>
                        </thead>
                        <tbody>
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
                        </tbody>
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
                        <select v-model="sunDirection">
                            <option value="solarDate">Solar date</option>
                            <option value="calendarDate">Calendar date</option>
                            <option value="unixTimeMs">Unix time</option>
                            <option value="angularDirection">Angular direction</option>
                        </select>
                    </p>

                    <template v-if="settings.sun?.solarDate">
                        <p>
                            <label>
                                <input
                                    type="checkbox"
                                    v-model="settings.sun.solarDate.atPrimeMeridian"
                                />
                                At the prime meridian
                            </label>
                        </p>
                        <p>
                            <label
                                >{{
                                    settings.sun.solarDate.atPrimeMeridian
                                        ? "Prime meridian"
                                        : "Local"
                                }}
                                solar time ({{
                                    formatTimeOfDay(settings.sun?.solarDate?.solarTime || 0)
                                }})</label
                            >
                            <input
                                type="range"
                                min="0"
                                max="24"
                                step="any"
                                v-model.number="settings.sun.solarDate.solarTime"
                            />
                        </p>
                        <p>
                            <select v-model="solarDateTimeOfYear">
                                <option value="dayOfYear">Day of year</option>
                                <option value="solarLongitude">Solar longitude</option>
                            </select>
                        </p>
                        <p v-if="solarDateTimeOfYear == 'dayOfYear'">
                            <label
                                >Day of year ({{
                                    $filters.formatNumber(
                                        settings.sun?.solarDate?.dayOfYear || 0,
                                        2
                                    )
                                }})</label
                            >
                            <input
                                type="range"
                                min="0"
                                max="365"
                                step="any"
                                v-model.number="settings.sun.solarDate.dayOfYear"
                            />
                        </p>
                        <p v-if="solarDateTimeOfYear == 'solarLongitude'">
                            <label
                                >Solar longitude ({{
                                    $filters.formatNumber(
                                        settings.sun?.solarDate?.solarLongitude || 0,
                                        2
                                    )
                                }}°)</label
                            >
                            <input
                                type="range"
                                min="0"
                                max="360"
                                step="any"
                                v-model.number="settings.sun.solarDate.solarLongitude"
                            />
                        </p>
                    </template>

                    <template v-if="settings.sun?.calendarDate">
                        <p>
                            <label>Year</label>
                            <input
                                type="number"
                                step="1"
                                v-model.number="settings.sun.calendarDate.year"
                            />
                        </p>
                        <p>
                            <label>Month (1-12)</label>
                            <input
                                type="number"
                                min="1"
                                max="12"
                                step="1"
                                v-model.number="settings.sun.calendarDate.month"
                            />
                        </p>
                        <p>
                            <label>Day (1-31)</label>
                            <input
                                type="number"
                                min="1"
                                max="31"
                                step="1"
                                v-model.number="settings.sun.calendarDate.day"
                            />
                        </p>
                        <p>
                            <label
                                >Time ({{
                                    formatTimeOfDay(settings.sun?.calendarDate?.time || 0)
                                }})</label
                            >
                            <input
                                type="range"
                                min="0"
                                max="24"
                                step="any"
                                v-model.number="settings.sun.calendarDate.time"
                            />
                        </p>
                        <p>
                            <label>UTC offset (hours)</label>
                            <input
                                type="number"
                                min="-12"
                                max="14"
                                step="any"
                                v-model.number="settings.sun.calendarDate.utcOffset"
                            />
                        </p>
                    </template>

                    <p v-if="settings.sun?.unixTimeMs != null">
                        <label>Unix time in ms ({{ unixTimeLabel }})</label>
                        <input type="number" step="1" v-model.number="settings.sun.unixTimeMs" />
                    </p>

                    <template v-if="settings.sun?.angularDirection">
                        <p>
                            <select v-model="angularReference">
                                <option :value="HrzProtocol.AngularDirection.Frame.FRAME_ENU">
                                    Reference: north
                                </option>
                                <option
                                    :value="HrzProtocol.AngularDirection.Frame.FRAME_CAMERA_HEADING"
                                >
                                    Reference: camera heading
                                </option>
                                <option :value="HrzProtocol.AngularDirection.Frame.FRAME_CAMERA">
                                    Reference: camera
                                </option>
                                <option value="geographicPosition">
                                    Reference: geographic position
                                </option>
                            </select>
                        </p>
                        <template v-if="settings.sun.angularDirection.geographicPosition">
                            <p>
                                <label>Reference latitude (°)</label>
                                <input
                                    type="number"
                                    min="-90"
                                    max="90"
                                    step="any"
                                    v-model.number="
                                        settings.sun.angularDirection.geographicPosition.latitude
                                    "
                                />
                            </p>
                            <p>
                                <label>Reference longitude (°)</label>
                                <input
                                    type="number"
                                    min="-180"
                                    max="180"
                                    step="any"
                                    v-model.number="
                                        settings.sun.angularDirection.geographicPosition.longitude
                                    "
                                />
                            </p>
                        </template>
                        <p>
                            <label
                                >Azimuth ({{
                                    $filters.formatNumber(
                                        settings.sun?.angularDirection?.azimuth || 0,
                                        2
                                    )
                                }}
                                rad)</label
                            >
                            <input
                                type="range"
                                min="0"
                                max="6.2831"
                                step="any"
                                v-model.number="settings.sun.angularDirection.azimuth"
                            />
                        </p>
                        <p>
                            <label
                                >Altitude ({{
                                    $filters.formatNumber(
                                        settings.sun?.angularDirection?.altitude || 0,
                                        2
                                    )
                                }}
                                rad)</label
                            >
                            <input
                                type="range"
                                min="-1.5707"
                                max="1.5707"
                                step="any"
                                v-model.number="settings.sun.angularDirection.altitude"
                            />
                        </p>
                    </template>
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
                        <label>Atmosphere color</label>
                        <ColorInput
                            v-if="settings.sky?.staticAtmosphereColor"
                            v-model="settings.sky.staticAtmosphereColor"
                        />
                    </p>
                    <p v-show="settings.sky?.mode === HrzProtocol.SkyMode.SKY_STATIC">
                        <label>Space color</label>
                        <ColorInput
                            v-if="settings.sky?.staticSpaceColor"
                            v-model="settings.sky.staticSpaceColor"
                        />
                    </p>
                    <p v-show="settings.sky?.mode === HrzProtocol.SkyMode.SKY_STATIC">
                        <select
                            v-if="settings.sky"
                            v-model.number="settings.sky.staticColorTransitionDistanceUnit"
                        >
                            <option
                                :value="
                                    HrzProtocol.StaticSkyColorTransitionUnit
                                        .STATIC_SKY_COLOR_TRANSITION_UNIT_METERS
                                "
                            >
                                Meters
                            </option>
                            <option
                                :value="
                                    HrzProtocol.StaticSkyColorTransitionUnit
                                        .STATIC_SKY_COLOR_TRANSITION_UNIT_PIXELS
                                "
                            >
                                Pixels
                            </option>
                        </select>
                    </p>
                    <p v-show="settings.sky?.mode === HrzProtocol.SkyMode.SKY_STATIC">
                        <label
                            >Color transition start distance ({{
                                $filters.formatNumber(
                                    settings.sky?.staticColorTransitionStartDistance || 0
                                )
                            }}
                            {{
                                settings.sky?.staticColorTransitionDistanceUnit ==
                                HrzProtocol.StaticSkyColorTransitionUnit
                                    .STATIC_SKY_COLOR_TRANSITION_UNIT_METERS
                                    ? "m"
                                    : "px"
                            }})</label
                        >
                        <input
                            type="range"
                            min="0"
                            :max="
                                settings.sky?.staticColorTransitionDistanceUnit ==
                                HrzProtocol.StaticSkyColorTransitionUnit
                                    .STATIC_SKY_COLOR_TRANSITION_UNIT_METERS
                                    ? 20000000
                                    : 2000
                            "
                            step="any"
                            v-if="settings.sky"
                            v-model.number="settings.sky.staticColorTransitionStartDistance"
                        />
                    </p>
                    <p v-show="settings.sky?.mode === HrzProtocol.SkyMode.SKY_STATIC">
                        <label
                            >Color transition end distance ({{
                                $filters.formatNumber(
                                    settings.sky?.staticColorTransitionEndDistance || 0
                                )
                            }}
                            {{
                                settings.sky?.staticColorTransitionDistanceUnit ==
                                HrzProtocol.StaticSkyColorTransitionUnit
                                    .STATIC_SKY_COLOR_TRANSITION_UNIT_METERS
                                    ? "m"
                                    : "px"
                            }})</label
                        >
                        <input
                            type="range"
                            min="0"
                            :max="
                                settings.sky?.staticColorTransitionDistanceUnit ==
                                HrzProtocol.StaticSkyColorTransitionUnit
                                    .STATIC_SKY_COLOR_TRANSITION_UNIT_METERS
                                    ? 20000000
                                    : 2000
                            "
                            step="any"
                            v-if="settings.sky"
                            v-model.number="settings.sky.staticColorTransitionEndDistance"
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
                <hr />
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
