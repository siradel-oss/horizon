import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { eqLong } from "./utils";

export class MessageHandler {
    private watchers = new Array<(msg: HrzProtocol.ITypedMessage) => boolean>();

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
        ticket: HrzProtocol.IPickTicket,
        handleFn: (result: HrzProtocol.IPickResults) => void
    ) {
        this.watch((msg) => {
            if (msg.type == HrzProtocol.MessageType.PICK_MESSAGE && msg.pick) {
                if (eqLong(ticket.opaque, msg.pick.ticket?.opaque)) {
                    handleFn(msg.pick.results || {});
                    return true;
                }
            }
            return false;
        });
    }

    awaitRasterDataFetchResult(
        ticket: HrzProtocol.IRasterDataFetchTicket,
        handleFn: (result: HrzProtocol.IPickLayerResult[]) => void
    ) {
        this.watch((msg) => {
            if (
                msg.type == HrzProtocol.MessageType.RASTER_DATA_FETCH_MESSAGE &&
                msg.rasterDataFetch
            ) {
                if (eqLong(ticket.opaque, msg.rasterDataFetch.ticket?.opaque)) {
                    handleFn(msg.rasterDataFetch.results || []);
                    return true;
                }
            }
            return false;
        });
    }

    watch(handleFn: (msg: HrzProtocol.ITypedMessage) => boolean) {
        this.watchers.push(handleFn);
    }

    watchForever(handleFn: (msg: HrzProtocol.ITypedMessage) => void) {
        this.watchers.push((m) => {
            handleFn(m);
            return false;
        });
    }

    watchAttributions(handleFn: (result: HrzProtocol.IAttributionsMessage) => void) {
        this.watchForever((msg) => {
            if (msg.type == HrzProtocol.MessageType.ATTRIBUTIONS_MESSAGE && msg.attributions) {
                handleFn(msg.attributions);
            }
        });
    }
}
