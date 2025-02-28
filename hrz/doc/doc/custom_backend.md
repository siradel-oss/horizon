---
Title: Implementing a backend
Category: General
---

To implement a backend, the user must implement an interface, provided by the API package. The only required method is one that takes in two integers representing the service and method identifiers, and a raw buffer representing the serialized message, and returns a raw buffer that is the serialized return message. The service and method identifiers describe the types of both the input and output messages.

For C++, there is only one interface, named `Backend`. However there are two interfaces in TypeScript: `HrzApi.SyncBackend` and `HrzApi.AsyncBackend`(in the `@siradel/horizon-api` package).

Typically, the Horizon instance is local to the browser, and the responses to the API calls can be obtained immediately. This is where `HrzApi.SyncBackend` is used. The return value of the method is the raw buffer.

However, some backends may take some time before responding, for example if they send messages over the network to drive a distant Horizon instance. In most languages, a thread can be dedicated to waiting for the messages to come back, so a blocking API suffices. But in TypeScript it is not possible to wait on a blocking function call. `HrzApi.AsyncBackend` is meant for this kind of backends. The return value of the method is a `Promise` of the raw array. (Using `await` on the asynchronous API enables writing code that is fairly similar to the synchronous version, but it forces all the functions in the call stack to be `async`, which can be inconvenient. Hence the split into two interfaces.)

A `SyncBackend` can be turned into an `AsyncBackend`. (A wrapper, named `SyncBackendWrapper`, is provided for this.) But the reverse is not possible. So for maximum compatibility, you should use the `AsyncBackend` interface in the client application. (Conversely, when implementing a backend, the compatibility is maximised by using the `SyncBackend` interface.) The default backend, `HrzCoreBackend`, implements both interfaces.

## Example: A WebSocket backend

Here is the implementation in TypeScript of a backend that sends and receives the serialized messages via websockets to a remote instance. The only difficult thing here is ordering the calls. To do so we associate each message that is sent with a unique identifier that we then use to resolve the promise associated with the command.

Because the responses to the calls are not immediate, the `AsyncBackend` is used.

```ts
import { HrzApi } from "@siradel/horizon-api";

class WebSocketsBackend implements HrzApi.AsyncBackend {
    private ws: WebSocket;
    private rpcIndex: number = 0;
    private awaitingRpc: Map<number, any>;

    public static init(address: string, cb: {(WebSocketsBackend): void}) {
        new WebSocketsBackend(address, cb);
    }

    private constructor(address: string, cb: {(WebSocketsBackend): void}) {
        this.awaitingRpc = new Map<number, Promise<Uint8Array>>();

        this.ws = new WebSocket(address);
        this.ws.binaryType = "arraybuffer";

        let backend = this;
        this.ws.onopen = function() {
            cb(backend);
        };

        this.ws.onclose = function() {
            console.log("Connection closed!");
            stopMessagePump();
        };

        let awaitingRpc = this.awaitingRpc;
        this.ws.onmessage = function(msg) {
            // The first 4 bytes of the returned message contain the query ID.
            let u32 = new Uint32Array(msg.data.slice(0, 4));
            let rpcIndex = u32[0];

            if (rpcIndex in awaitingRpc) {
                let u8 = new Uint8Array(msg.data.slice(4));
                awaitingRpc[rpcIndex](u8);
                awaitingRpc.delete(rpcIndex);
            }
        };
    }

    // This is the method we have to implement
    rpc(service: number, method: number, input: Uint8Array): Promise<Uint8Array> {
        let headerBuffer = new Uint32Array(3);

        let rpcIndex = this.rpcIndex;
        this.rpcIndex += 1;

        headerBuffer[0] = rpcIndex;
        headerBuffer[1] = service;
        headerBuffer[2] = method;

        let headerU8 = new Uint8Array(headerBuffer.buffer);

        let buffer = new ArrayBuffer(input.byteLength + 12);
        let u8 = new Uint8Array(buffer);
        u8.set(headerU8);
        u8.set(input, 12);

        let promise = new Promise<Uint8Array>((resolve, reject) => {
            this.awaitingRpc[rpcIndex] = resolve;
        });

        this.ws.send(buffer);

        return promise;
    }
}
```
