<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol, HrzProtocolHelper } from "@siradel-oss/horizon-protocol";
import { MessageHandler } from "@/utils/messages";
import { ref } from "vue";

let api: HrzApi.AsyncApi;
let singleModelHandle: HrzProtocol.LayerHandle | undefined;

let droppedFileEntries: FileSystemFileEntry[] = [];
let droppedModelFileEntries: FileSystemFileEntry[] = [];

const isDragging = ref(false);
const hasModel = ref(false);
const errorMessage = ref<string | null>(null);

let dragCounter = 0;

function getAsEntry(item: DataTransferItem): FileSystemEntry | null {
    return (item as any).getAsEntry?.() ?? (item as any).webkitGetAsEntry?.() ?? null;
}

function pushFileEntry(entry: FileSystemFileEntry) {
    droppedFileEntries.push(entry);
    if (entry.name.endsWith(".gltf") || entry.name.endsWith(".glb")) {
        droppedModelFileEntries.push(entry);
    }
}

function findDroppedEntry(path: string): FileSystemFileEntry | null {
    return droppedFileEntries.find((e) => e.fullPath === path) ?? null;
}

function readDirectory(dir: FileSystemDirectoryEntry): Promise<void> {
    return new Promise((resolve, reject) => {
        dir.createReader().readEntries((entries) => {
            for (const e of entries) {
                if (e.isFile) pushFileEntry(e as FileSystemFileEntry);
            }
            resolve();
        }, reject);
    });
}

function onDragEnter(e: DragEvent) {
    e.preventDefault();
    dragCounter++;
    isDragging.value = true;
}

function onDragOver(e: DragEvent) {
    e.preventDefault();
}

function onDragLeave() {
    dragCounter--;
    if (dragCounter === 0) isDragging.value = false;
}

async function onDrop(e: DragEvent) {
    e.preventDefault();
    dragCounter = 0;
    isDragging.value = false;
    errorMessage.value = null;

    if (!api || !singleModelHandle || !e.dataTransfer) {
        console.warn("LocalAssets: drop ignored — viewer not ready yet");
        return;
    }

    droppedFileEntries = [];
    droppedModelFileEntries = [];

    const promises: Promise<void>[] = [];
    for (let i = 0; i < e.dataTransfer.items.length; i++) {
        const entry = getAsEntry(e.dataTransfer.items[i]);
        if (!entry) continue;
        if (entry.isFile) pushFileEntry(entry as FileSystemFileEntry);
        else if (entry.isDirectory) promises.push(readDirectory(entry as FileSystemDirectoryEntry));
    }
    await Promise.all(promises);

    console.log(
        `LocalAssets: ${droppedFileEntries.length} file(s) collected,`,
        `${droppedModelFileEntries.length} model file(s)`
    );

    if (droppedModelFileEntries.length === 0) {
        errorMessage.value = "No .gltf or .glb file found. Try a different file or folder.";
        return;
    }

    const model = droppedModelFileEntries[0];
    console.log(`LocalAssets: loading "${model.fullPath}"`);
    await HrzApi.SingleModelLayerPathBuilder.create(singleModelHandle)
        .url()
        .set(api, encodeURI("client:" + model.fullPath));
    hasModel.value = true;
}

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler: MessageHandler) {
    api = api_;

    HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
        .terrain()
        .terrainColor()
        .set(api, { r: 0.96, g: 0.96, b: 0.96, a: 1 });

    singleModelHandle = await api.LayerService.createLayer({
        type: HrzProtocol.LayerType.SINGLE_MODEL,
    });

    // All fields must be explicit — proto defaults (0/false) would make the layer invisible.
    await HrzApi.SingleModelLayerPathBuilder.create(singleModelHandle).set(api, {
        visible: true,
        clipId: -1,
        sceneViews: { bits: 1 },
        color: { r: 1, g: 1, b: 1, a: 1 },
        transform: {
            scale: { x: 10, y: 10, z: 10 },
            rotation: { x: 0, y: 0, z: 0, w: 1 },
            frame: {
                up: HrzProtocol.Axis.POS_Y,
                front: HrzProtocol.Axis.NEG_Z,
                handedness: HrzProtocol.Handedness.RIGHT,
            },
        },
        lighting: {
            enableLighting: true,
            castShadows: true,
            receiveShadows: true,
        },
    });

    await api.CameraService.setFixedTarget({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        angularViewpoint: {
            target: { latitude: 0, longitude: 0, altitude: 1 },
            bearing: 0.8,
            tilt: 0.9,
            distance: 400,
        },
        minTilt: 0,
        maxTilt: 1.5,
        minDistance: 0.01,
        maxDistance: 100000,
        goToAnimation: { duration: 0 },
    });

    msgHandler.watch((msg) => {
        if (msg.payload == "assetRequest") {
            const request = msg.assetRequest;
            void (async () => {
                const path = decodeURI(request.url ?? "");
                const rangeStart = HrzProtocolHelper.uint64AsNumber(request.rangeStart || 0);
                const rangeSize = HrzProtocolHelper.uint64AsNumber(request.rangeSize || 0);
                console.log(`LocalAssets: asset request for "${path}"`);
                const entry = findDroppedEntry(path);
                if (!entry) {
                    const msg = `Missing file "${path}". Drop the folder containing all required assets.`;
                    console.warn("LocalAssets:", msg);
                    errorMessage.value = msg;
                    return;
                }
                entry.file(async (file) => {
                    const blob =
                        rangeSize === 0 ? file : file.slice(rangeStart, rangeStart + rangeSize);
                    await api.ClientDataService.provideAssetData({
                        data: new Uint8Array(await blob.arrayBuffer()),
                        ticket: request.ticket,
                    });
                    console.log(`LocalAssets: served "${path}"`);
                });
            })();
        }
        return false;
    });
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Local assets</h1>
                <p>
                    Horizon can load 3D models provided directly by the client application, without
                    any server involved. Drop a <code>.glb</code>, a <code>.gltf</code> file, or a
                    folder containing one, onto the canvas to display it.
                </p>
                <p>
                    When Horizon needs a file (such as an external texture referenced by a
                    <code>.gltf</code>), it sends an <code>AssetRequestMessage</code> through the
                    message queue. The application responds with the file contents via
                    <code>ClientDataService.provideAssetData()</code>.
                </p>
                <p>
                    You can check the console logs for details on the loading process, including any
                    missing files.
                </p>
                <p
                    v-if="errorMessage"
                    class="text-onErrorContainer bg-errorContainer p-3 rounded-sm"
                >
                    {{ errorMessage }}
                </p>
                <p>
                    <FullscreenSource file="source/LocalAssets.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <div
                class="relative w-full h-full"
                @dragenter="onDragEnter"
                @dragover="onDragOver"
                @dragleave="onDragLeave"
                @drop="onDrop"
            >
                <Viewer @ready="onHorizonReady" />
                <Transition name="fade">
                    <div
                        v-if="!hasModel"
                        class="absolute inset-0 flex items-center justify-center pointer-events-none bg-scrim/50"
                    >
                        <div
                            class="text-center text-white px-10 py-8 rounded-2xl border-2 border-dashed transition-opacity border-white"
                        >
                            <p class="text-base font-medium">
                                Drop a <code>.glb</code> or <code>.gltf</code><br />file or folder
                                here
                            </p>
                        </div>
                    </div>
                </Transition>
                <Transition name="fade">
                    <div
                        v-if="hasModel && isDragging"
                        class="absolute inset-0 flex items-center justify-center pointer-events-none bg-scrim/50"
                    >
                        <div
                            class="text-center text-white px-10 py-8 rounded-2xl border-2 border-dashed border-white"
                        >
                            <p class="text-base font-medium">Drop to replace the model</p>
                        </div>
                    </div>
                </Transition>
            </div>
        </template>
    </SplitView>
</template>
<style scoped>
.fade-enter-active,
.fade-leave-active {
    transition: opacity 0.2s ease;
}
.fade-enter-from,
.fade-leave-to {
    opacity: 0;
}
</style>
