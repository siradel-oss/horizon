// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

/**
 * Pattern 04: Screen picking
 *
 * Picking is asynchronous and ticket-based:
 *   1. ViewerService.pickScreen() — queues the pick request, returns a ticket
 *   2. The engine processes the pick during a future frame
 *   3. A PICK_MESSAGE arrives in the message queue with the matching ticket
 *
 * The ticket correlates the request to its response. If you have multiple
 * concurrent picks, use the ticket to match them.
 *
 * You must have an active message pump running (see 01_init.ts) or the
 * pick result will never arrive.
 */

import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";

export async function pickAtScreenCoords(
    api: HrzApi.AsyncApi,
    x: number,
    y: number
): Promise<HrzProtocol.PickResults | null> {
    const requestResult = await api.ViewerService.pickScreen({
        coords: { x, y },
        includedRasters: [], // empty = include all raster layers
    });

    if (!requestResult.hasATicket || !requestResult.ticket) {
        return null; // viewer not ready or picking disabled
    }

    const ticket = requestResult.ticket;

    // Poll until the matching PICK_MESSAGE arrives. In a real app this logic
    // lives in the main message pump rather than a dedicated loop per pick.
    return new Promise((resolve) => {
        const poll = setInterval(async () => {
            const result = await api.MessageQueueService.dequeueMessages({
                maxMessageCount: 100,
            });
            for (const msg of result.messages ?? []) {
                if (msg.payload == "pick" && msg.pick.ticket?.opaque === ticket.opaque) {
                    clearInterval(poll);
                    resolve(HrzProtocol.PickResults.create(msg.pick.results || {}));
                }
            }
        }, 50);
    });
}

// Example: use pick results
export function processPickResults(
    results: HrzProtocol.PickResults,
    targetLayerHandle: HrzProtocol.LayerHandle.$Properties
): void {
    for (const result of results.results ?? []) {
        // result.layer?.handle identifies which layer was hit
        if (result.layer?.handle?.opaque === targetLayerHandle.opaque) {
            // Geographic position of the hit point
            const pos = results.position;
            console.log("Hit at", pos?.latitude, pos?.longitude);

            // For vector layers, feature ID is in result.vector.featureId
            const featureId = result.vector?.featureId;
            if (featureId) {
                console.log("Feature attributes:", featureId.attributes);
            }
        }
    }
}
