<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { ref, reactive, onUnmounted, computed, watch } from "vue";
import { useInstantOrContinuousAction } from "../utils/instantOrContinuousAction";
import { useWindowEvent } from "../utils/windowEvents";
import Icon from "./Icon.vue";

let api: HrzApi.AsyncApi;
let pollIntervalId: number | undefined;
let orientation = ref<number>(0);
let altitude = ref<number>(0);
let altitudeUnit = ref<string>("m");

function radToDeg(rad: number) {
    return (rad * 180) / Math.PI;
}

function init(apiInstance: HrzApi.AsyncApi) {
    api = apiInstance;
}

function startPolling() {
    pollIntervalId = window.setInterval(() => {
        api.CameraService.getCameraPose({ camera: HrzProtocol.CameraIndex.CAMERA_0 }).then(
            (pose) => (orientation.value = radToDeg(pose.bearing))
        );

        api.CameraService.getSceneViewScaleAndAltitude({
            sceneView: HrzProtocol.SceneViewIndex.SCENE_VIEW_0,
        }).then((scaleAndAltitude) => {
            altitude.value = scaleAndAltitude.altitudeAbsolute;
            altitudeUnit.value = "m";

            altitude.value = Math.round(altitude.value / 10) * 10;

            if (altitude.value > 9999) {
                altitude.value /= 1000;
                altitudeUnit.value = "km";
            }

            altitude.value = Math.round(altitude.value);
        });
    }, 50);
}

function resetNorth() {
    api.CameraService.resetNorth({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        resetTilt: true,
        animationOptions: {
            duration: 1,
            easingExponent: 2,
            easingFunction: HrzProtocol.EasingFunctions.EASE_INOUT,
            trajectoryType: HrzProtocol.TrajectoryType.INTERPOLATED,
        },
    });
}

function move(dx: number, dy: number) {
    const speed = 0.5;
    api.CameraService.move({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        translation: {
            angleFactorX: dx * speed,
            angleFactorY: dy * speed,
            frame: HrzProtocol.CameraFrame.CAMERA_FRAME_TANGENTIAL,
        },
    });
}

function beginContinuousMove(dx: number, dy: number) {
    const speed = 1.0;
    api.CameraService.beginContinuousMovement({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        translation: {
            angleFactorX: dx * speed,
            angleFactorY: dy * speed,
            frame: HrzProtocol.CameraFrame.CAMERA_FRAME_TANGENTIAL,
        },
    });
}

function endContinuousMove() {
    api.CameraService.endContinuousMovement({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        translation: {},
    });
}

function zoom(dz: number) {
    const speed = 0.75;
    api.CameraService.move({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        zoom: {
            ratio: Math.pow(2, dz * speed),
        },
    });
}

function beginContinuousZoom(dz: number) {
    const speed = 1.0;
    api.CameraService.beginContinuousMovement({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        zoom: {
            ratio: Math.pow(2, dz * speed),
        },
    });
}

function endContinuousZoom() {
    api.CameraService.endContinuousMovement({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        zoom: {},
    });
}

const { onMouseDown: moveOnMouseDown, onMouseUp: moveOnMouseUp } = useInstantOrContinuousAction(
    move,
    beginContinuousMove,
    endContinuousMove
);
const { onMouseDown: zoomOnMouseDown, onMouseUp: zoomOnMouseUp } = useInstantOrContinuousAction(
    zoom,
    beginContinuousZoom,
    endContinuousZoom
);

useWindowEvent("mouseup", moveOnMouseUp);
useWindowEvent("mouseup", zoomOnMouseUp);

const joystickMaxOffset = 20;
const joystickSpeedFactor = 1;
const joystickOffset = reactive({ x: 0, y: 0 });
let draggingJoystick = false;
let joystickStartPos = { x: 0, y: 0 };

const joystickSpeed = computed(() => {
    return {
        x: (joystickOffset.x / joystickMaxOffset) * joystickSpeedFactor,
        y: (joystickOffset.y / joystickMaxOffset) * joystickSpeedFactor,
    };
});

function rotateEnabled(speed: { x: number; y: number }) {
    const threshold = 0.1;
    const speedMagnitude = Math.sqrt(speed.x * speed.x + speed.y * speed.y);
    return speedMagnitude > threshold;
}

function joystickStart(event: MouseEvent) {
    draggingJoystick = true;
    joystickStartPos = { x: event.clientX, y: event.clientY };
}

function joystickEnd(event: Event) {
    const mouseEvent = event as MouseEvent;
    if (mouseEvent.button !== 0) return;
    draggingJoystick = false;
    joystickOffset.x = 0;
    joystickOffset.y = 0;
}

function joystickMove(event: Event) {
    if (!draggingJoystick) return;

    const mouseEvent = event as MouseEvent;
    const dx = mouseEvent.clientX - joystickStartPos.x;
    const dy = mouseEvent.clientY - joystickStartPos.y;

    const maxOffset = 20;
    // The 120 / 200 factor scales the coordinates from page pixels to SVG viewbox.
    const distance = (Math.sqrt(dx * dx + dy * dy) * 200) / 120;
    const clampedDistance = Math.min(distance, maxOffset);
    const angle = Math.atan2(dy, dx);

    joystickOffset.x = clampedDistance * Math.cos(angle);
    joystickOffset.y = clampedDistance * Math.sin(angle);
}

useWindowEvent("mousemove", joystickMove);
useWindowEvent("mouseup", joystickEnd);

watch(joystickSpeed, (newSpeed, oldSpeed) => {
    if (rotateEnabled(newSpeed)) {
        api.CameraService.beginContinuousMovement({
            cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
            rotation: {
                bearing: newSpeed.x,
                tilt: -newSpeed.y,
            },
        });
    } else if (rotateEnabled(oldSpeed)) {
        api.CameraService.endContinuousMovement({
            cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
            rotation: {},
        });
    }
});

onUnmounted(() => {
    window.clearInterval(pollIntervalId);
});

defineExpose({ init, startPolling });
</script>

<template>
    <div class="flex flex-row items-center gap-1 select-none">
        <svg
            class="pointer-events-auto"
            style="width: 120px; height: 120px"
            viewBox="-100 -100 200 200"
            xmlns="http://www.w3.org/2000/svg"
        >
            <defs>
                <g id="arrowCircle">
                    <circle r="16" fill="white" />
                    <path
                        d="M -8 3 L 0 -5 L 8 3"
                        stroke="#666"
                        stroke-width="4"
                        stroke-linecap="round"
                        stroke-linejoin="round"
                        fill="none"
                    />
                </g>
                <radialGradient
                    id="middleGradient"
                    gradientTransform="scale(2) translate(-0.32 -0.32)"
                >
                    <stop offset="5%" stop-color="#fff" />
                    <stop offset="100%" stop-color="#444" />
                </radialGradient>
            </defs>
            <filter id="shadowBlur">
                <feGaussianBlur in="SourceGraphic" stdDeviation="10" />
            </filter>
            <g filter="url(#shadowBlur)" opacity="80%">
                <circle cx="0" cy="0" r="30" fill="black" />
                <circle cx="0" cy="0" r="40" fill="none" stroke="black" stroke-width="5" />
                <circle cx="0" cy="0" r="70" fill="none" stroke="black" stroke-width="5" />
            </g>
            <circle cx="0" cy="0" r="70" fill="none" stroke="white" stroke-width="5" />
            <use
                href="#arrowCircle"
                x="0"
                y="-70"
                transform="rotate(0)"
                class="cursor-pointer"
                @mousedown.left.prevent="moveOnMouseDown(0, 1)"
            />
            <use
                href="#arrowCircle"
                x="0"
                y="-70"
                transform="rotate(90)"
                class="cursor-pointer"
                @mousedown.left.prevent="moveOnMouseDown(1, 0)"
            />
            <use
                href="#arrowCircle"
                x="0"
                y="-70"
                transform="rotate(180)"
                class="cursor-pointer"
                @mousedown.left.prevent="moveOnMouseDown(0, -1)"
            />
            <use
                href="#arrowCircle"
                x="0"
                y="-70"
                transform="rotate(270)"
                class="cursor-pointer"
                @mousedown.left.prevent="moveOnMouseDown(-1, 0)"
            />
            <circle cx="0" cy="0" r="40" fill="none" stroke="white" stroke-width="6" />
            <path
                d="M -10 0 L 0 -21 L 10 0 Z"
                fill="#f33"
                :transform="`rotate(${-orientation}) translate(0 -36)`"
            />
            <g
                class="cursor-pointer"
                @dblclick="resetNorth"
                @mousedown.left.prevent="joystickStart"
                :transform="`translate(${joystickOffset.x} ${joystickOffset.y})`"
            >
                <circle cx="0" cy="0" r="30" fill="white" />
                <circle cx="0" cy="0" r="25" fill="url(#middleGradient)" />
            </g>
        </svg>
        <div id="zoomControls" class="flex flex-col items-stretch">
            <button class="text-mTitleLarge" @mousedown.left.prevent="zoomOnMouseDown(1)">
                <Icon icon="add" />
            </button>
            <div class="text-mLabelSmall leading-3">{{ altitude }}<br />{{ altitudeUnit }}</div>
            <button class="text-mTitleLarge" @mousedown.left.prevent="zoomOnMouseDown(-1)">
                <Icon icon="remove" />
            </button>
        </div>
    </div>
</template>

<style scoped>
@reference "../style.css";

#zoomControls {
    @apply shadow-lg pointer-events-auto;

    > * {
        @apply bg-white text-[#666] border-outline p-0 w-8 h-8 text-center
            flex items-center justify-center
            first:rounded-tl-lg first:rounded-tr-lg last:rounded-bl-lg last:rounded-br-lg
            not-first:border-t;
    }
}
</style>
