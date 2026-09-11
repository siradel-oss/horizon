// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

/**
 * Pattern 05: Client vector data provider
 *
 * Instead of serving data from a URL, the client can respond to on-demand
 * data requests from the engine. This is useful for live data, computed data,
 * or data already in memory.
 *
 * Setup (done once in the VectorDataLayer source):
 *   providerType: CLIENT_VECTOR_DATA_PROVIDER — marks this source as client-provided
 *   selectionType: FEATURE_ID_SELECTION — engine requests by feature ID
 *                  (alternative: TILE_SELECTION — engine requests by tile coordinates)
 *
 * Runtime flow:
 *   1. Engine sends VECTOR_DATA_REQUEST_MESSAGE when it needs data
 *   2. Client calls ClientDataService.provideVectorData() with matching ticket
 *   3. To push updated data, call ClientDataService.invalidateVectorData()
 *      which triggers a new VECTOR_DATA_REQUEST_MESSAGE
 *
 * The message pump (see 01_init.ts) must dispatch these messages to a handler.
 */

import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";

const CLIENT_DATA_SOURCE_INDEX = 0; // index of the client source within the layer

// Example attribute data keyed by feature ID
const DATA: Record<number, number> = {
    1: 42.5,
    2: 87.3,
    3: 12.1,
};

export async function respondToDataRequest(
    api: HrzApi.AsyncApi,
    msg: HrzProtocol.VectorDataRequestMessage.$Properties
): Promise<void> {
    const response = HrzProtocol.VectorDataRequestResponse.create({
        ticket: msg.ticket,
        features: [],
    });

    // The engine specifies exactly which features it needs. Respond in the
    // same order as the request — the engine joins sources by position.
    for (const featureId of msg.featureIdSelection?.featureIds ?? []) {
        const idAttr = featureId.attributes?.[0]?.value;
        const id = idAttr?.numberValue ?? 0;

        response.features!.push({
            attributeValues: [{ numberValue: DATA[id] ?? 0 }],
        });
    }

    await api.ClientDataService.provideVectorData(response);
}

// Call from your message pump dispatch loop:
export function handleMessageForClientData(
    api: HrzApi.AsyncApi,
    msg: HrzProtocol.TypedMessage.$Shape
): void {
    if (msg.payload === "vectorDataRequest") {
        const req = msg.vectorDataRequest;
        // Guard: only respond to requests for our specific layer + source
        if (
            req.vectorDataLayerId === 0 && // the numeric ID assigned in VectorDataLayer
            req.vectorDataSourceIndex === CLIENT_DATA_SOURCE_INDEX &&
            req.featureIdSelection
        ) {
            respondToDataRequest(api, req);
        }
    }
}

// Trigger a re-request of all data (e.g. when underlying data changes):
export function invalidateAllData(api: HrzApi.AsyncApi, vectorDataLayerNumericId: number): void {
    api.ClientDataService.invalidateVectorData({
        vectorDataLayerId: vectorDataLayerNumericId,
        vectorDataSourceIndex: CLIENT_DATA_SOURCE_INDEX,
        everything: {}, // invalidate all tiles/features
    });
}
