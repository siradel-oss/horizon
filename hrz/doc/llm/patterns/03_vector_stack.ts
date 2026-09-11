// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

/**
 * Pattern 03: Vector data layer + vector tiles layer
 *
 * Horizon splits vector visualization into two separate layers:
 *
 *   VectorDataLayer — non-visual. Defines WHERE data comes from (sources)
 *   and WHAT attributes exist. Given a numeric ID used to link the pair.
 *
 *   VectorTilesLayer — visual. References the data layer by its numeric ID,
 *   adds a styling script, and defines visual representations.
 *
 * Always create them as a pair. One VectorDataLayer can feed multiple
 * VectorTilesLayers with different styles.
 *
 * The `id` on VectorDataLayer is a user-assigned integer (local namespace),
 * not the layer handle. VectorTilesLayer references it via `source.vectorDataLayerId`.
 */

import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";

const VECTOR_DATA_LAYER_ID = 1; // arbitrary user-assigned ID

export async function createTiledVectorStack(
    api: HrzApi.AsyncApi,
    mvtUrlPattern: string // e.g. "https://example.com/tiles/{z}/{x}/{y}.mvt"
): Promise<{ dataLayer: HrzProtocol.LayerHandle; tilesLayer: HrzProtocol.LayerHandle }> {
    // ── Step 1: Vector data layer ──────────────────────────────────────────
    // Defines the data source and attribute schema. Not rendered directly.

    const dataLayerHandle = await api.LayerService.createLayer({
        name: "Vector data",
        type: HrzProtocol.LayerType.VECTOR_DATA,
    });

    HrzApi.VectorDataLayerPathBuilder.create(dataLayerHandle).set(api, {
        id: VECTOR_DATA_LAYER_ID,
        sources: [
            {
                hasGeometry: true,
                tiledDataProvider: {
                    urlPattern: mvtUrlPattern,
                    layerName: "default", // which MVT layer to read; omit to combine all
                },
                attributes: [
                    {
                        id: 1,
                        isFeatureId: true,
                        sourceName: "id",
                    },
                    {
                        id: 2,
                        sourceName: "population",
                    },
                ],
            },
        ],
    });

    // ── Step 2: Vector tiles layer ─────────────────────────────────────────
    // Defines how to render the data. References the data layer by its `id`.

    const tilesLayerHandle = await api.LayerService.createLayer({
        name: "Vector tiles",
        type: HrzProtocol.LayerType.VECTOR_TILES,
    });

    HrzApi.VectorTilesLayerPathBuilder.create(tilesLayerHandle).set(api, {
        source: {
            // Link to the data layer by its user-assigned numeric ID,
            // NOT by its LayerHandle.
            vectorDataLayerId: VECTOR_DATA_LAYER_ID,
        },
        style: {
            // Expose data attributes to the styling script by name.
            attributes: [{ stylingName: "population", vectorDataAttrId: 2 }],
            // The styling script is a mini-DSL. attr() reads attributes,
            // prp() reads previously set properties, emit triggers a representation.
            stylingScript: `
                set "fill_color" = colorize("populationPalette", attr("population"));
                emit 0;
            `,
            representations: [
                {
                    id: 0,
                    flatOverlayPolygon: {
                        color: {
                            // `name` binds a styling script property; omit for a fixed color.
                            defaultValue: { r: 0.2, g: 0.5, b: 0.9, a: 0.8 },
                        },
                    },
                },
            ],
        },
    });

    return { dataLayer: dataLayerHandle, tilesLayer: tilesLayerHandle };
}
