<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import ColorButton from "@/component/ColorButton.vue";
import TextButton from "@/component/TextButton.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applySceneTemplate, getLayerByName } from "@/utils/scenes";
import { MessageHandler } from "@/utils/messages";
import { computed, onUnmounted, ref, watch } from "vue";

// One time zone per offset from UTC, ordered from the westernmost: there
// are hundreds of zones but only a few dozen offsets, and two zones sharing
// one are interchangeable as far as the Sun is concerned.
const TIME_ZONES = [
    "Etc/GMT+12",
    "Pacific/Pago_Pago",
    "Pacific/Honolulu",
    "Pacific/Marquesas",
    "America/Anchorage",
    "America/Los_Angeles",
    "America/Denver",
    "America/Chicago",
    "America/New_York",
    "America/Halifax",
    "America/St_Johns",
    "America/Sao_Paulo",
    "America/Noronha",
    "Atlantic/Azores",
    "UTC",
    "Europe/London",
    "Europe/Paris",
    "Europe/Athens",
    "Europe/Moscow",
    "Asia/Tehran",
    "Asia/Dubai",
    "Asia/Kabul",
    "Asia/Karachi",
    "Asia/Kolkata",
    "Asia/Kathmandu",
    "Asia/Dhaka",
    "Asia/Yangon",
    "Asia/Bangkok",
    "Asia/Shanghai",
    "Australia/Eucla",
    "Asia/Tokyo",
    "Australia/Darwin",
    "Australia/Brisbane",
    "Australia/Lord_Howe",
    "Pacific/Guadalcanal",
    "Pacific/Auckland",
    "Pacific/Chatham",
    "Pacific/Apia",
    "Pacific/Kiritimati",
];

const SUN_SETTINGS: HrzProtocol.SunSettings.$Properties = {
    mode: HrzProtocol.SunLightingMode.SUN_LIGHTING_SIMULATED,
    staticColor: { r: 1, g: 1, b: 1, a: 1 },
};

const AMBIENT_SETTINGS: HrzProtocol.AmbientSettings.$Properties = {
    lightingStrength: 1.0,
    sunAmbientBalance: 0.5,
    wrapLighting: 0.0,
    lighting: {
        enableLighting: true,
        castShadows: true,
        receiveShadows: true,
    },
    sky: {
        mode: HrzProtocol.SkyMode.SKY_SIMULATED,
        attenuation: 0.3,
        staticAtmosphereColor: { r: 0.8, g: 0.89, b: 0.92, a: 1 },
        staticSpaceColor: { r: 0, g: 0, b: 0, a: 1 },
    },
    ambientLighting: {
        mode: HrzProtocol.AmbientLightingMode.AMBIENT_LIGHTING_SIMULATED,
        staticColor: { r: 0.42, g: 0.42, b: 0.42, a: 1 },
    },
};

interface Viewpoint {
    name: string;
    angularViewpoint: HrzProtocol.AngularViewpoint.$Properties;
}

const VIEWPOINTS: Viewpoint[] = [
    {
        name: "Planet",
        angularViewpoint: {
            target: { latitude: 20, longitude: 5, altitude: 0 },
            bearing: 0,
            tilt: 0,
            distance: 7e6,
        },
    },
    {
        name: "City",
        angularViewpoint: {
            target: { latitude: 48.1082, longitude: -1.6807, altitude: 0 },
            bearing: 0.37,
            tilt: 1.0,
            distance: 1200,
        },
    },
    {
        name: "Moutain",
        angularViewpoint: {
            target: { latitude: 45.776281, longitude: 6.85516, altitude: -8.69 },
            bearing: 3.140855,
            tilt: 1.388976,
            distance: 21519.5,
        },
    },
];

type SunDirection = "calendarDate" | "unixTimeMs" | "solarDate" | "angularDirection";

interface DirectionChoice {
    id: SunDirection;
    label: string;
    description: string;
}

const DIRECTIONS: DirectionChoice[] = [
    {
        id: "calendarDate",
        label: "Calendar date",
        description: `A Gregorian date, a time, and the offset from UTC that applies on
        that date. It is the only mode that knows about leap years.`,
    },
    {
        id: "unixTimeMs",
        label: "Unix time",
        description: `A point in time as a single integer. Nothing has to be carried
        over at midnight or at the end of a month, which makes it easy to animate or
        interpolate.`,
    },
    {
        id: "solarDate",
        label: "Solar date",
        description: `A mean solar time, and a day within an idealised year (no leap
        years). The time is either the local solar time at the camera, so that the Sun
        follows the camera East and West but still climbs and sinks as the camera moves
        North or South, or the solar time at the prime meridian, so that the Sun stays
        fixed with respect to the Earth.`,
    },
    {
        id: "angularDirection",
        label: "Angular direction",
        description: `The Sun’s position in the sky is directly set by azimuth and altitude
        angles. They are measured either in a frame that travels with the camera, keeping
        the Sun’s bearing from the North, from the camera’s heading, or its exact place on
        screen, or alternatively at a fixed point on Earth, so that the Sun moves across the sky as the camera does. It can be at
        positions the real Sun cannot.`,
    },
];

interface ZonedDateTime {
    year: number;
    month: number;
    day: number;
    hour: number;
    minute: number;
    second: number;
}

// Breaks an instant down into the date and time a clock on that offset reads.
function zonedDateTime(instantMs: number, utcOffset: number): ZonedDateTime {
    const shifted = new Date(instantMs + utcOffset * 60000);
    return {
        year: shifted.getUTCFullYear(),
        month: shifted.getUTCMonth() + 1,
        day: shifted.getUTCDate(),
        hour: shifted.getUTCHours(),
        minute: shifted.getUTCMinutes(),
        second: shifted.getUTCSeconds(),
    };
}

// The reverse operation: the instant at which a clock on that offset reads
// that date and time.
function instantOf(local: ZonedDateTime, utcOffset: number): number {
    const asIfUtc = Date.UTC(
        local.year,
        local.month - 1,
        local.day,
        local.hour,
        local.minute,
        local.second
    );
    return asIfUtc - utcOffset * 60000;
}

const ZONE_FORMATS = new Map<string, Intl.DateTimeFormat>();

function zoneFormat(timeZone: string): Intl.DateTimeFormat {
    let format = ZONE_FORMATS.get(timeZone);
    if (!format) {
        format = new Intl.DateTimeFormat("en-US", {
            timeZone,
            hourCycle: "h23",
            year: "numeric",
            month: "2-digit",
            day: "2-digit",
            hour: "2-digit",
            minute: "2-digit",
            second: "2-digit",
        });
        ZONE_FORMATS.set(timeZone, format);
    }
    return format;
}

// The offset a zone is on at that instant, in minutes. This is where daylight
// saving time is dealt with: a zone's offset is not a constant, it depends on
// the date, and Horizon only ever sees the offset, never the zone.
function zoneOffsetMinutes(instantMs: number, timeZone: string): number {
    const parts: Record<string, string> = {};
    for (const part of zoneFormat(timeZone).formatToParts(new Date(instantMs))) {
        parts[part.type] = part.value;
    }
    const asIfUtc = Date.UTC(
        Number(parts.year),
        Number(parts.month) - 1,
        Number(parts.day),
        Number(parts.hour),
        Number(parts.minute),
        Number(parts.second)
    );
    // `instantMs` carries milliseconds, which the fields above dropped, hence
    // the rounding to the closest minute.
    return Math.round((asIfUtc - instantMs) / 60000);
}

// The instant at which a clock in that zone reads that date and time. The
// offset has to be read at the instant we are looking for, so we guess one,
// then correct the guess. This is exact except within the hour that a daylight
// saving transition skips or repeats.
function instantInZone(local: ZonedDateTime, timeZone: string): number {
    const asIfUtc = instantOf(local, 0);
    const guess = asIfUtc - zoneOffsetMinutes(asIfUtc, timeZone) * 60000;
    return asIfUtc - zoneOffsetMinutes(guess, timeZone) * 60000;
}

// Days elapsed since the 1st of January at midnight, which is what the
// idealised-year mode counts in: the 1st of January is day 0.
function dayOfYear(local: ZonedDateTime): number {
    const startOfYear = Date.UTC(local.year, 0, 1);
    const startOfDay = Date.UTC(local.year, local.month - 1, local.day);
    return Math.round((startOfDay - startOfYear) / 86400000);
}

function hourOfDay(local: ZonedDateTime): number {
    return local.hour + local.minute / 60 + local.second / 3600;
}

const DAY_OF_YEAR_FORMAT = new Intl.DateTimeFormat("en-GB", {
    day: "numeric",
    month: "long",
    timeZone: "UTC",
});

function formatDayOfYear(day: number): string {
    // Rendered in a year that is not a leap year, since the idealised year the
    // solar date mode counts in does not have one either.
    return DAY_OF_YEAR_FORMAT.format(new Date(Date.UTC(2001, 0, 1 + Math.min(day, 364))));
}

function pad(value: number, length: number = 2): string {
    return Math.floor(value).toString().padStart(length, "0");
}

function formatUtcOffset(minutes: number): string {
    const sign = minutes < 0 ? "-" : "+";
    return `UTC${sign}${pad(Math.abs(minutes) / 60)}:${pad(Math.abs(minutes) % 60)}`;
}

function degrees(radians: number): string {
    return ((radians * 180) / Math.PI).toFixed(0);
}

// The zone whose offset matches the browser's right now, so that the demo
// opens on a clock the visitor recognises. UTC is passed over: someone on
// +00:00 is far more likely to be in London than to keep their clock on UTC.
function defaultTimeZone(): string {
    const browserOffset = -new Date().getTimezoneOffset();
    return (
        TIME_ZONES.find(
            (zone) => zone !== "UTC" && zoneOffsetMinutes(Date.now(), zone) === browserOffset
        ) ?? "Europe/Paris"
    );
}

const direction = ref<SunDirection>("calendarDate");
const currentDirection = computed<DirectionChoice>(() =>
    DIRECTIONS.find((choice) => choice.id === direction.value)!
);
const isDateDirection = computed<boolean>(() => direction.value !== "angularDirection");
// The two modes that name an actual instant, and the only ones worth running:
// the idealised-year one has no calendar to advance, and the angle one is set
// by hand.
const usesRealDate = computed<boolean>(
    () => direction.value === "calendarDate" || direction.value === "unixTimeMs"
);
// Of those two, only the calendar date carries a time zone. A Unix timestamp
// is UTC by definition, and the idealised-year mode reads its time at the
// prime meridian.
const usesTimeZone = computed<boolean>(() => direction.value === "calendarDate");

const timeZone = ref<string>(defaultTimeZone());

// The single piece of state the three date modes are built around: the instant
// being looked at, in milliseconds since the Unix epoch. The date and time
// inputs are views of it, and so is every message but the angle one.
const instantMs = ref<number>(
    // Today at nine in the morning: a low Sun makes the shadows easy to read.
    instantInZone(
        {
            ...zonedDateTime(Date.now(), zoneOffsetMinutes(Date.now(), timeZone.value)),
            hour: 9,
            minute: 0,
            second: 0,
        },
        timeZone.value
    )
);

// Whether the solar date is read at the prime meridian rather than at the camera.
const atPrimeMeridian = ref<boolean>(true);

// What the angle mode is built around instead. The angles are measured either
// in a frame attached to the camera, or at a geographic position.
type AngularReference = HrzProtocol.AngularDirection.Frame | "geographicPosition";

const azimuth = ref<number>(2.6);
const altitude = ref<number>(0.5);
const reference = ref<AngularReference>(HrzProtocol.AngularDirection.Frame.FRAME_ENU);
const referenceLatitude = ref<number>(48.11);
const referenceLongitude = ref<number>(-1.68);
// Set while the next click in the view is to become the reference position.
const pickingReference = ref<boolean>(false);

const playing = ref<boolean>(false);
// Animation rate, as a power of ten of simulated hours per second of real time.
const rateExponent = ref<number>(0);
const rate = computed<number>(() => Math.pow(10, rateExponent.value));

// The offset each zone is on over the selected day. Offsets can only change
// at a transition, so this is computed once per day rather than on every frame
// of the animation: `selectedDay` notifies only when its value changes.
const selectedDay = computed<number>(() => Math.floor(instantMs.value / 86400000));
const timeZoneOptions = computed(() =>
    TIME_ZONES.map((zone) => {
        // Read at midday, which is past the small hours a transition falls in.
        const instant = selectedDay.value * 86400000 + 43200000;
        return {
            id: zone,
            label: `${formatUtcOffset(zoneOffsetMinutes(instant, zone))} — ${zone}`,
        };
    })
);

const utcOffset = computed<number>(() =>
    usesTimeZone.value ? zoneOffsetMinutes(instantMs.value, timeZone.value) : 0
);
const zoned = computed<ZonedDateTime>(() => zonedDateTime(instantMs.value, utcOffset.value));

function setZoned(local: ZonedDateTime) {
    // Setting the date or the time by hand stops the animation, which would
    // otherwise fight the user over the value of the inputs.
    playing.value = false;
    instantMs.value = usesTimeZone.value
        ? instantInZone(local, timeZone.value)
        : instantOf(local, 0);
}

const dateInput = computed<string>({
    get: () => `${pad(zoned.value.year, 4)}-${pad(zoned.value.month)}-${pad(zoned.value.day)}`,
    set: (value) => {
        const [year, month, day] = value.split("-").map(Number);
        // The input is momentarily empty while it is being edited.
        if (!year || !month || !day) return;
        setZoned({ ...zoned.value, year, month, day });
    },
});

const timeInput = computed<string>({
    get: () => `${pad(zoned.value.hour)}:${pad(zoned.value.minute)}:${pad(zoned.value.second)}`,
    set: (value) => {
        const [hour, minute, second] = value.split(":").map(Number);
        if (!Number.isFinite(hour) || !Number.isFinite(minute)) return;
        setZoned({ ...zoned.value, hour, minute, second: second || 0 });
    },
});

const timeOfDay = computed<number>({
    get: () => hourOfDay(zoned.value),
    set: (hours) => {
        // Stay within the day: 24:00:00 would roll over to the next one.
        const seconds = Math.min(Math.round(hours * 3600), 24 * 3600 - 1);
        setZoned({
            ...zoned.value,
            hour: Math.floor(seconds / 3600),
            minute: Math.floor(seconds / 60) % 60,
            second: seconds % 60,
        });
    },
});

const unixTimeInput = computed<number>({
    get: () => Math.round(instantMs.value),
    set: (value) => {
        if (!Number.isFinite(value)) return;
        playing.value = false;
        instantMs.value = value;
    },
});

const INSTANT_FORMAT = new Intl.DateTimeFormat("en-GB", {
    timeZone: "UTC",
    dateStyle: "full",
    timeStyle: "medium",
});

// The instant a Unix timestamp stands for, spelled out. A timestamp is an
// instant, not a local time, so it is read at UTC.
const instantLabel = computed<string>(() => {
    const value = Math.round(instantMs.value);
    // The Date constructor accepts up to 8.64e15 milliseconds either way.
    if (!Number.isFinite(value) || Math.abs(value) > 8.64e15) return "not a date";
    return `${INSTANT_FORMAT.format(new Date(value))} UTC`;
});

const dayOfYearInput = computed<number>({
    get: () => dayOfYear(zoned.value),
    set: (day) => {
        const date = new Date(Date.UTC(zoned.value.year, 0, 1 + Math.round(day)));
        setZoned({
            ...zoned.value,
            month: date.getUTCMonth() + 1,
            day: date.getUTCDate(),
        });
    },
});

const timeLabel = computed<string>(() => {
    switch (direction.value) {
        case "solarDate":
            return atPrimeMeridian.value ? "Solar time at the prime meridian" : "Local solar time";
        default:
            return "Time";
    }
});

// What the two angles are measured from depends on the frame they are in.
const azimuthFrom = computed<string>(() => {
    if (
        reference.value === "geographicPosition" ||
        reference.value === HrzProtocol.AngularDirection.Frame.FRAME_ENU
    )
        return "North";
    return reference.value === HrzProtocol.AngularDirection.Frame.FRAME_CAMERA_HEADING
        ? "the camera heading"
        : "the view direction";
});

const altitudeFrom = computed<string>(() =>
    reference.value === HrzProtocol.AngularDirection.Frame.FRAME_CAMERA
        ? "the view direction"
        : "the horizon"
);

function formatRate(hoursPerSecond: number): string {
    if (hoursPerSecond < 1 / 60) return `${(hoursPerSecond * 3600).toFixed(0)} s`;
    if (hoursPerSecond < 1) return `${(hoursPerSecond * 60).toFixed(0)} min`;
    if (hoursPerSecond < 24) return `${hoursPerSecond.toFixed(1)} h`;
    return `${(hoursPerSecond / 24).toFixed(1)} days`;
}

function setNow() {
    instantMs.value = Date.now();
}

function setDayOfMonth(month: number, day: number) {
    setZoned({ ...zoned.value, month, day });
}

const sunDirection = computed<HrzProtocol.SunSettings.$Properties>(() => {
    switch (direction.value) {
        case "calendarDate":
            return {
                calendarDate: {
                    year: zoned.value.year,
                    month: zoned.value.month,
                    day: zoned.value.day,
                    time: hourOfDay(zoned.value),
                    utcOffset: utcOffset.value / 60,
                },
            };
        case "unixTimeMs":
            return { unixTimeMs: Math.round(instantMs.value) };
        case "solarDate":
            // `zoned` is already read at the prime meridian in this mode, and it
            // is handed over either as such, or as the solar time wherever the
            // camera happens to be.
            return {
                solarDate: {
                    solarTime: hourOfDay(zoned.value),
                    atPrimeMeridian: atPrimeMeridian.value,
                    dayOfYear: dayOfYear(zoned.value),
                },
            };
        case "angularDirection":
            return {
                angularDirection: {
                    ...(reference.value === "geographicPosition"
                        ? {
                              geographicPosition: {
                                  latitude: referenceLatitude.value,
                                  longitude: referenceLongitude.value,
                              },
                          }
                        : { cameraFrame: reference.value }),
                    azimuth: azimuth.value,
                    altitude: altitude.value,
                },
            };
    }
});

const ENUM_NAMES: Record<string, Record<number, string>> = {
    cameraFrame: {
        [HrzProtocol.AngularDirection.Frame.FRAME_ENU]: "FRAME_ENU",
        [HrzProtocol.AngularDirection.Frame.FRAME_CAMERA_HEADING]: "FRAME_CAMERA_HEADING",
        [HrzProtocol.AngularDirection.Frame.FRAME_CAMERA]: "FRAME_CAMERA",
    },
};

function protoText(message: object, indent: string = ""): string {
    return Object.entries(message)
        .map(([key, value]) => {
            const name = key.replace(/[A-Z]/g, (letter) => `_${letter.toLowerCase()}`);
            if (typeof value === "object" && value !== null) {
                return `${indent}${name} {\n${protoText(value, indent + "    ")}\n${indent}}`;
            }
            if (typeof value === "number") {
                const text =
                    ENUM_NAMES[key]?.[value] ??
                    (Number.isInteger(value) ? `${value}` : value.toFixed(4));
                return `${indent}${name}: ${text}`;
            }
            return `${indent}${name}: ${value}`;
        })
        .join("\n");
}

const sunDirectionPreview = computed<string>(() => protoText(sunDirection.value));

let api: HrzApi.AsyncApi | undefined;
let msgHandler: MessageHandler | undefined;

// The engine is driven asynchronously, and the animation produces a new date
// on every frame: rather than letting calls pile up, we only ever keep the
// latest direction and send it once the previous call has completed.
let pendingDirection: HrzProtocol.SunSettings.$Properties | undefined;
let sending = false;

async function setSunDirection(value: HrzProtocol.SunSettings.$Properties) {
    pendingDirection = value;

    if (!api || sending) return;

    const target = api;
    sending = true;
    try {
        while (pendingDirection) {
            const settings = pendingDirection;
            pendingDirection = undefined;

            await HrzApi.SceneViewSettingsPathBuilder.create(
                HrzProtocol.SceneViewIndex.SCENE_VIEW_0
            )
                .ambient()
                .sun()
                .set(target, { ...SUN_SETTINGS, ...settings });
        }
    } finally {
        sending = false;
    }
}

watch(sunDirection, setSunDirection);

// The geographic reference position of the angle mode is easier to point at than
// to type: while a pick is armed, the next click in the view lands there.
async function onCanvasClick(x: number, y: number) {
    if (!api || !msgHandler || !pickingReference.value) return;

    const pick = await api.ViewerService.pickScreen({ coords: { x, y }, includedRasters: [] });
    if (!pick.hasATicket || !pick.ticket) return;

    msgHandler.awaitPickResult(pick.ticket, (results) => {
        // A click on the sky picks nothing at all.
        if (!results.position) return;

        referenceLatitude.value = results.position.latitude ?? 0;
        referenceLongitude.value = results.position.longitude ?? 0;
        pickingReference.value = false;
    });
}

function goTo(viewpoint: Viewpoint, durationS: number) {
    api?.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        angularViewpoint: viewpoint.angularViewpoint,
        altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
        maxAltitude: 2e7,
        minTilt: 0,
        maxTilt: Math.PI,
        goToAnimation: {
            duration: durationS,
            easingFunction: HrzProtocol.EasingFunctions.EASE_INOUT,
            easingExponent: 2,
            trajectoryType: HrzProtocol.TrajectoryType.INTERPOLATED,
        },
    });
}

let animationFrame: number | undefined;
let previousFrameMs: number = 0;

function onAnimationFrame(frameMs: number) {
    // The step is clamped because browsers stop running animation frames on a
    // hidden tab: coming back to it should not jump hours ahead.
    const elapsedHours = (Math.min(frameMs - previousFrameMs, 100) / 1000) * rate.value;
    previousFrameMs = frameMs;

    instantMs.value += elapsedHours * 3600 * 1000;
    animationFrame = requestAnimationFrame(onAnimationFrame);
}

watch(direction, () => {
    // The modes that have no clock cannot be left running, and a pick armed for
    // the reference position no longer has anything to set.
    if (!usesRealDate.value) playing.value = false;
    pickingReference.value = false;
});

watch(reference, () => {
    pickingReference.value = false;
});

watch(playing, (value) => {
    if (value && animationFrame === undefined) {
        previousFrameMs = performance.now();
        animationFrame = requestAnimationFrame(onAnimationFrame);
    } else if (!value && animationFrame !== undefined) {
        cancelAnimationFrame(animationFrame);
        animationFrame = undefined;
    }
});

onUnmounted(() => {
    if (animationFrame !== undefined) {
        cancelAnimationFrame(animationFrame);
    }
});

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler_: MessageHandler) {
    await applySceneTemplate(api_, "aws_terrain_tiles", false);
    await applyDefaultOrthoBaseLayer(api_);
    await applySceneTemplate(api_, "rennes_buildings", false);

    const buildings = await getLayerByName(api_, "Rennes buildings");
    if (buildings) {
        await HrzApi.VectorTilesLayerPathBuilder.create(buildings)
            .clamping()
            .set(api_, { method: HrzProtocol.VectorClampMode.ANCHOR });
    }

    await HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
        .ambient()
        .set(api_, { ...AMBIENT_SETTINGS, sun: { ...SUN_SETTINGS, ...sunDirection.value } });

    api = api_;
    msgHandler = msgHandler_;

    goTo(VIEWPOINTS[0], 0);
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Sun position</h1>
                <p>
                    Where the Sun sits in the sky is part of the
                    <a href="../doc/ambient_settings/">ambient settings</a> of the scene view. There
                    are four ways of setting its position: three that take a date, and one that
                    places it at set angles.
                </p>
                <hr />
                <h3>Viewpoint</h3>
                <p>
                    <span class="flex gap-1 flex-wrap">
                        <TextButton
                            v-for="viewpoint in VIEWPOINTS"
                            :key="viewpoint.name"
                            color="onSurface"
                            @click="goTo(viewpoint, 2)"
                            >{{ viewpoint.name }}</TextButton
                        >
                    </span>
                </p>
                <hr />
                <h3>Sun direction</h3>
                <p>
                    <select v-model="direction">
                        <option v-for="choice in DIRECTIONS" :key="choice.id" :value="choice.id">
                            {{ choice.label }}
                        </option>
                    </select>
                </p>
                <p>{{ currentDirection.description }}</p>
                <template v-if="isDateDirection">
                    <template v-if="direction === 'unixTimeMs'">
                        <p>
                            <label>Unix time (ms)</label>
                            <input type="number" step="1" v-model.number="unixTimeInput" />
                        </p>
                        <p>{{ instantLabel }}</p>
                    </template>
                    <template v-else>
                        <p v-if="direction === 'solarDate'">
                            <label>
                                <input type="checkbox" v-model="atPrimeMeridian" />
                                At the prime meridian
                            </label>
                        </p>
                        <p v-if="usesTimeZone">
                            <label>Date</label>
                            <input type="date" v-model="dateInput" />
                        </p>
                        <p v-else>
                            <label
                                >Day of year ({{ dayOfYearInput }},
                                {{ formatDayOfYear(dayOfYearInput) }})</label
                            >
                            <input
                                type="range"
                                min="0"
                                max="364"
                                step="1"
                                v-model.number="dayOfYearInput"
                            />
                        </p>
                        <p>
                            <label>{{ timeLabel }}</label>
                            <input type="time" step="1" v-model="timeInput" />
                            <input
                                type="range"
                                min="0"
                                max="24"
                                step="any"
                                v-model.number="timeOfDay"
                            />
                        </p>
                        <p v-if="usesTimeZone">
                            <label>Time zone</label>
                            <select v-model="timeZone">
                                <option
                                    v-for="zone in timeZoneOptions"
                                    :key="zone.id"
                                    :value="zone.id"
                                >
                                    {{ zone.label }}
                                </option>
                            </select>
                        </p>
                        <p v-if="usesTimeZone">
                            Horizon is never told a time zone, only an offset, and daylight saving
                            time is the caller's business: the list above holds one zone per offset,
                            since any two that share one are the same Sun. Change the date across a
                            transition and watch the offsets move with it.
                        </p>
                        <p>
                            <label>Jump to</label>
                            <span class="flex gap-1 flex-wrap mt-1">
                                <TextButton color="onSurface" @click="setNow()">Now</TextButton>
                                <TextButton color="onSurface" @click="setDayOfMonth(3, 20)"
                                    >March equinox</TextButton
                                >
                                <TextButton color="onSurface" @click="setDayOfMonth(6, 21)"
                                    >June solstice</TextButton
                                >
                                <TextButton color="onSurface" @click="setDayOfMonth(12, 21)"
                                    >December solstice</TextButton
                                >
                            </span>
                        </p>
                    </template>
                </template>
                <template v-else>
                    <p>
                        <label>Reference</label>
                        <select v-model="reference">
                            <option :value="HrzProtocol.AngularDirection.Frame.FRAME_ENU">
                                North
                            </option>
                            <option
                                :value="HrzProtocol.AngularDirection.Frame.FRAME_CAMERA_HEADING"
                            >
                                Camera heading
                            </option>
                            <option :value="HrzProtocol.AngularDirection.Frame.FRAME_CAMERA">
                                Camera
                            </option>
                            <option value="geographicPosition">Geographic position</option>
                        </select>
                    </p>
                    <template v-if="reference === 'geographicPosition'">
                        <p>
                            <label>Reference latitude (°)</label>
                            <input
                                type="number"
                                min="-90"
                                max="90"
                                step="any"
                                v-model.number="referenceLatitude"
                            />
                        </p>
                        <p>
                            <label>Reference longitude (°)</label>
                            <input
                                type="number"
                                min="-180"
                                max="180"
                                step="any"
                                v-model.number="referenceLongitude"
                            />
                        </p>
                        <p>
                            <TextButton
                                color="onSurface"
                                @click="pickingReference = !pickingReference"
                                >{{
                                    pickingReference
                                        ? "Cancel"
                                        : "Pick reference location on the map"
                                }}</TextButton
                            >
                        </p>
                        <p v-show="pickingReference" class="italic">
                            Click anywhere on the ground to put the reference position there.
                        </p>
                    </template>
                    <p>
                        <label
                            >Azimuth ({{ degrees(azimuth) }}°, clockwise from
                            {{ azimuthFrom }})</label
                        >
                        <input
                            type="range"
                            min="0"
                            max="6.2831"
                            step="any"
                            v-model.number="azimuth"
                        />
                    </p>
                    <p>
                        <label>Altitude ({{ degrees(altitude) }}° above {{ altitudeFrom }})</label>
                        <input
                            type="range"
                            min="-1.5707"
                            max="1.5707"
                            step="any"
                            v-model.number="altitude"
                        />
                    </p>
                </template>
                <template v-if="usesRealDate">
                    <hr />
                    <h3>Animating</h3>
                    <p>
                        The settings can be set at every frame to animate the Sun’s position in the
                        sky.
                    </p>
                    <p class="flex flex-row gap-4 flex-wrap">
                        <ColorButton
                            :icon="playing ? 'pause' : 'play_arrow'"
                            @click="playing = !playing"
                            >{{ playing ? "Pause" : "Play" }}</ColorButton
                        >
                    </p>
                    <p>
                        <label>Rate ({{ formatRate(rate) }} per second)</label>
                        <input
                            type="range"
                            min="-2"
                            max="3"
                            step="any"
                            v-model.number="rateExponent"
                        />
                    </p>
                </template>
                <hr />
                <h3>Protocol value</h3>
                <pre>{{ sunDirectionPreview }}</pre>
                <hr />
                <p>
                    <FullscreenSource file="source/SunPosition.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" @clickAt="onCanvasClick" />
        </template>
    </SplitView>
</template>
