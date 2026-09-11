// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

/**
 * Pattern 01: Initialization
 *
 * The required sequence every Horizon app must follow:
 *   1. Call HrzCoreBackend.init() — starts loading the WASM binary
 *   2. In the callback, check for INIT_SUCCESS then create AsyncApi
 *   3. Start polling the message queue IMMEDIATELY — do not wait
 *   4. Wait for ViewerReadyMessage before sending any scene commands
 *
 * The message pump must keep running for the lifetime of the viewer.
 * Any scene commands sent before ViewerReadyMessage are silently ignored.
 *
 * HrzCoreBackend comes from the @siradel-oss/horizon-core package (WASM wrapper).
 * It is not imported here to keep this file buildable without the WASM toolchain.
 * Usage:
 *
 *   import { HrzCoreBackend } from "@siradel-oss/horizon-core";
 *   HrzCoreBackend.init(canvas, "wasm/", options, (backend, initStatus) => {
 *       if (initStatus !== HrzProtocol.ViewerInitStatus.INIT_SUCCESS) return;
 *       const api = new HrzApi.AsyncApi(backend);
 *       startApp(api);
 *   });
 */

import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";

/**
 * Call this once the backend is ready. Starts the message pump and waits
 * for ViewerReadyMessage before sending scene commands.
 *
 * Pass the AsyncApi created from HrzCoreBackend inside the init callback:
 *   const api = new HrzApi.AsyncApi(backend);
 *   startApp(api);
 */
export async function startApp(api: HrzApi.AsyncApi): Promise<void> {
    // Start message pump before waiting for ViewerReadyMessage.
    // The pump must keep running for the lifetime of the viewer
    // (picking, vector data requests, etc. all arrive via this queue).
    await new Promise<void>((resolve) => {
        const pump = setInterval(async () => {
            const result = await api.MessageQueueService.dequeueMessages({
                maxMessageCount: 100,
            });
            for (const msg of result.messages ?? []) {
                if (msg.viewerReady !== null && msg.viewerReady !== undefined) {
                    clearInterval(pump);
                    resolve();
                    return;
                }
            }
        }, 100);
    });

    // Viewer is ready — issue scene setup commands
    await onViewerReady(api);

    // Keep the pump running for ongoing events (picking, vector data, etc.)
    startMessagePump(api);
}

async function onViewerReady(api: HrzApi.AsyncApi): Promise<void> {
    await api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        bounds: {
            bounds: { west: -5.67, south: 41.8, east: 10.37, north: 52.03 },
            tilt: 0,
        },
        maxAltitude: 1e8,
        minTilt: 0,
        maxTilt: Math.PI,
    });
}

function startMessagePump(api: HrzApi.AsyncApi): void {
    setInterval(async () => {
        const result = await api.MessageQueueService.dequeueMessages({
            maxMessageCount: 100,
        });
        for (const msg of result.messages ?? []) {
            handleMessage(api, msg);
        }
    }, 100);
}

function handleMessage(_api: HrzApi.AsyncApi, msg: HrzProtocol.TypedMessage.$Properties): void {
    switch (msg.payload) {
        case "pick":
            // msg.pick is a PickResultMessage
            break;
        case "vectorDataRequest":
            // msg.vectorDataRequest is a VectorDataRequestMessage
            break;
        case "cameraNotification":
            // msg.cameraNotification is a CameraNotification
            break;
    }
}
