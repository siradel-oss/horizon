// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { eqLong } from "./utils.js";

export class MessageHandler {
    private watchers = new Array<
        (msg: HrzProtocol.TypedMessage.$Properties & HrzProtocol.TypedMessage.$Shape) => boolean
    >();

    constructor(api: HrzApi.AsyncApi, intervalMs: number = 200) {
        setInterval(() => this.checkMessages(api), intervalMs);
    }

    private async checkMessages(api: HrzApi.AsyncApi) {
        while (true) {
            let dequeue = await api.MessageQueueService.dequeueMessages({ maxMessageCount: 100 });

            for (let msg of dequeue.messages || []) {
                this.watchers = this.watchers.filter((watcher) => !watcher(msg));
            }

            if ((dequeue.queueSize?.messageCount || 0) == 0) {
                break;
            }
        }
    }

    awaitPickResult(
        ticket: HrzProtocol.PickTicket.$Properties,
        handleFn: (
            result: HrzProtocol.PickResults.$Properties & HrzProtocol.PickResults.$Shape
        ) => void
    ) {
        this.watch((msg) => {
            if (msg.payload == "pick") {
                if (eqLong(ticket.opaque, msg.pick.ticket?.opaque)) {
                    handleFn(msg.pick.results || {});
                    return true;
                }
            }
            return false;
        });
    }

    awaitRasterDataFetchResult(
        ticket: HrzProtocol.RasterDataFetchTicket.$Properties,
        handleFn: (
            result: HrzProtocol.PickLayerResult.$Properties[] & HrzProtocol.PickLayerResult.$Shape[]
        ) => void
    ) {
        this.watch((msg) => {
            if (msg.payload == "rasterDataFetch") {
                if (eqLong(ticket.opaque, msg.rasterDataFetch.ticket?.opaque)) {
                    handleFn(msg.rasterDataFetch.results || []);
                    return true;
                }
            }
            return false;
        });
    }

    watch(
        handleFn: (
            msg: HrzProtocol.TypedMessage.$Properties & HrzProtocol.TypedMessage.$Shape
        ) => boolean
    ) {
        this.watchers.push(handleFn);
    }

    watchForever(
        handleFn: (
            msg: HrzProtocol.TypedMessage.$Properties & HrzProtocol.TypedMessage.$Shape
        ) => void
    ) {
        this.watchers.push((m) => {
            handleFn(m);
            return false;
        });
    }

    watchAttributions(
        handleFn: (
            result: HrzProtocol.AttributionsMessage.$Properties &
                HrzProtocol.AttributionsMessage.$Shape
        ) => void
    ) {
        this.watchForever((msg) => {
            if (msg.payload == "attributions") {
                handleFn(msg.attributions);
            }
        });
    }
}
