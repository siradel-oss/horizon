// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

/**
 * Pattern 06: Camera animation
 *
 * CameraService.setOrbit() positions the camera relative to a geographic target.
 * Without goToAnimation, it snaps instantly. With goToAnimation, it animates.
 *
 * The bounds-based overload fits the camera to a bounding box —
 * useful when you know the area of interest but not the exact altitude.
 *
 * The pose-based overload gives direct control over position (lat/lon/alt),
 * bearing (compass heading in radians), and tilt (angle from nadir, in radians).
 *
 * CameraIndex.CAMERA_0 is the default single camera.
 * Multi-view setups may use CAMERA_1 for a second viewport.
 */

import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";

// Fit camera to a bounding box with a smooth animation
export function flyToBounds(
    api: HrzApi.AsyncApi,
    west: number,
    south: number,
    east: number,
    north: number,
    durationSeconds: number = 2.0
): void {
    api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        bounds: {
            bounds: { west, south, east, north },
            tilt: 0,
        },
        maxAltitude: 1e8,
        minTilt: 0,
        maxTilt: Math.PI,
        goToAnimation: {
            duration: durationSeconds,
            easingExponent: 2,
            easingFunction: HrzProtocol.EasingFunctions.EASE_INOUT,
            trajectoryType: HrzProtocol.TrajectoryType.BALLISTIC,
        },
    });
}

// Position the camera at a precise lat/lon/altitude with bearing and tilt
export function flyToPose(
    api: HrzApi.AsyncApi,
    latitude: number,
    longitude: number,
    altitudeMeters: number,
    bearingRadians: number = 0,
    tiltRadians: number = -Math.PI / 2, // -PI/2 = looking straight down
    durationSeconds: number = 2.0
): void {
    api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        pose: {
            position: { latitude, longitude, altitude: altitudeMeters },
            bearing: bearingRadians,
            tilt: tiltRadians,
        },
        maxAltitude: 1e8,
        minTilt: 0,
        maxTilt: Math.PI,
        goToAnimation: {
            duration: durationSeconds,
            easingExponent: 2,
            easingFunction: HrzProtocol.EasingFunctions.EASE_INOUT,
            trajectoryType: HrzProtocol.TrajectoryType.BALLISTIC,
        },
    });
}

// Snap to position without animation (e.g. for initial view)
export function snapToBounds(
    api: HrzApi.AsyncApi,
    west: number,
    south: number,
    east: number,
    north: number
): void {
    api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        bounds: {
            bounds: { west, south, east, north },
            tilt: 0,
        },
        maxAltitude: 1e8,
        minTilt: 0,
        maxTilt: Math.PI,
        // No goToAnimation → instant
    });
}
