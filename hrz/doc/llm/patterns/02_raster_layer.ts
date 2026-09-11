// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

/**
 * Pattern 02: Imagery raster layer
 *
 * Creating a layer is a two-step operation:
 *   1. LayerService.createLayer() — allocates the layer, returns a handle
 *   2. ImageryRasterLayerPathBuilder.create(handle).set() — configures it
 *
 * The path builder mirrors the proto message structure for ImageryRasterLayer.
 * You can set the whole layer at once (as here) or use granular paths to update
 * individual fields later.
 *
 * The "union/variant pattern": raster.provider has a `type` field that selects
 * which provider variant is active. Always set `type` AND the matching variant
 * field together (e.g. WMTS_RASTER_PROVIDER + `wmts: {...}`).
 */

import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";

export async function createWmtsRasterLayer(
    api: HrzApi.AsyncApi,
    name: string,
    wmtsUrl: string,
    layerIdentifier: string
): Promise<HrzProtocol.LayerHandle> {
    const handle = await api.LayerService.createLayer({
        name,
        type: HrzProtocol.LayerType.IMAGERY_RASTER,
    });

    const model = HrzProtocol.ImageryRasterLayer.create({
        visible: true,
        sceneViews: { bits: 1 },
        raster: {
            provider: {
                wmts: {
                    url: wmtsUrl,
                    layerIdentifier,
                    imageFormat: HrzProtocol.ImageFormat.SRGBA_8,
                    missingTilePolicy: HrzProtocol.MissingTilePolicy.USE_LOWER_RESOLUTION,
                },
            },
            sampling: {
                filtering: HrzProtocol.TextureFiltering.BILINEAR,
                alphaChannelUsage: HrzProtocol.AlphaChannelUsage.IGNORE_ALPHA_CHANNEL,
                nodataHandling: HrzProtocol.NodataHandling.IGNORE_NODATA,
            },
            blending: { opacity: 1 },
            displayBounds: { west: -180, south: -90, east: 180, north: 90 },
        },
    });

    // Set the entire model in one call — efficient for initial configuration.
    HrzApi.ImageryRasterLayerPathBuilder.create(handle).set(api, model);

    return handle;
}

// Later, to update only the opacity (granular update — the engine applies
// optimisations for small changes to known-fast properties):
export function setLayerOpacity(
    api: HrzApi.AsyncApi,
    handle: HrzProtocol.LayerHandle.$Properties,
    opacity: number
): void {
    // Use clone() because paths are consumed after terminal operations.
    HrzApi.ImageryRasterLayerPathBuilder.create(handle)
        .clone()
        .raster()
        .blending()
        .opacity()
        .set(api, opacity);
}
