<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import TextButton from "@/component/TextButton.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyDefaultOrthoBaseLayer, applySceneTemplate } from "@/utils/scenes";
import { MessageHandler } from "@/utils/messages";
import { ref } from "vue";

let api: HrzApi.AsyncApi;
let tileLayerHandle: HrzProtocol.LayerHandle | undefined;
let urlVersion = 1;

const labelTemplate = ref("LOD={z}, {x}-{y}");

function tileUrlPattern(version: number): string {
    return `client:/tile/${version}/{z}/{x}/{y}.png`;
}

function applyTemplate(template: string, z: number, x: number, y: number): string {
    return template
        .replace(/{z}/g, String(z))
        .replace(/{x}/g, String(x))
        .replace(/{y}/g, String(y));
}

function generateTileBytes(z: number, x: number, y: number, label: string): Promise<Uint8Array> {
    return new Promise((resolve) => {
        const canvas = document.createElement("canvas");
        canvas.width = 256;
        canvas.height = 256;
        const ctx = canvas.getContext("2d")!;

        const hue = (((x * 37 + y * 17 + z * 7) % 360) + 360) % 360;
        ctx.fillStyle = `hsl(${hue}, 60%, 65%)`;
        ctx.fillRect(0, 0, 256, 256);

        ctx.strokeStyle = `hsl(${hue}, 60%, 35%)`;
        ctx.lineWidth = 2;
        ctx.strokeRect(1, 1, 254, 254);

        ctx.fillStyle = "rgba(0, 0, 0, 0.85)";
        ctx.font = "bold 18px monospace";
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText(label, 128, 128);

        canvas.toBlob((blob) => {
            blob!.arrayBuffer().then((buf) => resolve(new Uint8Array(buf)));
        }, "image/png");
    });
}

async function reloadTiles() {
    if (!api || !tileLayerHandle) return;
    urlVersion++;
    await HrzApi.ImageryRasterLayerPathBuilder.create(tileLayerHandle)
        .raster()
        .provider()
        .tiled()
        .urlPattern()
        .set(api, tileUrlPattern(urlVersion));
}

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler: MessageHandler) {
    api = api_;

    await applyDefaultOrthoBaseLayer(api);
    await applySceneTemplate(api, "initial_viewpoint_france", true);

    tileLayerHandle = await api.LayerService.createLayer({
        type: HrzProtocol.LayerType.IMAGERY_RASTER,
    });

    await HrzApi.ImageryRasterLayerPathBuilder.create(tileLayerHandle).set(api, {
        visible: true,
        group: HrzProtocol.RasterGroup.TOP_RASTER_GROUP,
        sceneViews: { bits: 1 },
        slot: 0,
        raster: {
            provider: {
                tiled: {
                    urlPattern: tileUrlPattern(urlVersion),
                    imageFormat: HrzProtocol.ImageFormat.SRGBA_8,
                    geometry: {
                        projection: {
                            descriptorType: HrzProtocol.SrsDescriptorType.SRID_DESCRIPTOR,
                            descriptor: "EPSG:3857",
                        },
                    },
                    tilingScheme: {
                        globalTiling: {
                            tileSize: 256,
                            levelZeroTileCountX: 1,
                            levelZeroTileCountY: 1,
                            maxLevel: 19,
                        },
                    },
                },
            },
            sampling: {
                alphaChannelUsage: HrzProtocol.AlphaChannelUsage.IGNORE_ALPHA_CHANNEL,
                nodataHandling: HrzProtocol.NodataHandling.IGNORE_NODATA,
                filtering: HrzProtocol.TextureFiltering.BILINEAR,
            },
            blending: { opacity: 0.5 },
            displayBounds: { west: -180, south: -90, east: 180, north: 90 },
        },
    });

    msgHandler.watch((msg) => {
        if (msg.payload == "assetRequest") {
            const request = msg.assetRequest;
            // Capture the template at request time so tiles stay consistent within a reload batch.
            const template = labelTemplate.value;
            void (async () => {
                const path = decodeURI(request.url ?? "");
                // URL path: "/tile/{version}/{z}/{x}/{y}.png"
                const parts = path.split("/");
                const z = parseInt(parts[3]);
                const x = parseInt(parts[4]);
                const y = parseInt(parts[5]);
                const label = applyTemplate(template, z, x, y);
                const bytes = await generateTileBytes(z, x, y, label);
                await api.ClientDataService.provideAssetData({
                    data: bytes,
                    ticket: request.ticket,
                    mimeType: "image/png",
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
                <h1>Procedural raster tiles</h1>
                <p>
                    Raster tile URLs can use the <code>client:</code> scheme, which routes each tile
                    request through the message queue instead of fetching it from a server. The
                    application receives an <code>AssetRequestMessage</code> for every tile and
                    responds with the image bytes via
                    <code>ClientDataService.provideAssetData()</code>.
                </p>
                <p>
                    In this demo, tiles are generated on the fly using an offscreen
                    <code>&lt;canvas&gt;</code>: each tile gets a deterministic color derived from
                    its coordinates. Zoom in to see finer tiles being requested and rendered.
                </p>
                <p>
                    <label
                        >Tile label, use <code>{z}</code>, <code>{x}</code>, <code>{y}</code> as
                        placeholders</label
                    >
                    <input type="text" v-model="labelTemplate" />
                </p>
                <p>
                    <TextButton color="onSurface" @click="reloadTiles">Reload tiles</TextButton>
                </p>
                <p>
                    <FullscreenSource file="source/ProceduralTiles.vue" />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
