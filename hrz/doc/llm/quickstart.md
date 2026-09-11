# Quick integration

Everything a from-scratch Horizon integration needs, in one place. For anything beyond
this, see [Getting started](../getting_started.md), and the
[key concepts](./index.md) / [API reference summary](./api_index.md).

## Packages

- `@siradel-oss/horizon-core` — the WASM/native rendering backend.
- `@siradel-oss/horizon-protocol` — the message protocol used to talk to the backend.
- `@siradel-oss/horizon-api` — the TypeScript API built on top of the protocol.

## Minimal init

```ts
import { HrzCoreBackend, HrzCoreRuntimeFile } from "@siradel-oss/horizon-core";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";

HrzCoreBackend.init(
    myCanvas,
    (file: HrzCoreRuntimeFile) => {
        switch (file) {
            case "hrz_core.js":
                return new URL("/node_modules/@siradel-oss/horizon-core/dist/hrz_core.js", import.meta.url).href;
            case "hrz_core.wasm":
                return new URL("/node_modules/@siradel-oss/horizon-core/dist/hrz_core.wasm", import.meta.url).href;
        }
    },
    options,
    (backend, initStatus) => {
        if (initStatus !== HrzProtocol.ViewerInitStatus.INIT_SUCCESS) return;
        const api = new HrzApi.AsyncApi(backend);
        // api is ready to use once the message pump is running and
        // ViewerReadyMessage has been received — see below.
    });
```

The runtime file location callback lets the bundler detect and handle `hrz_core.js` /
`hrz_core.wasm` as ordinary assets (see gotcha 3 below) — prefer this over hardcoding a
`runtimeFilesBaseUrl` string.

Initialization order matters: backend init → `AsyncApi` instantiation → message pump
start → wait for `ViewerReadyMessage` before issuing any commands. See [key
concepts](./index.md) for the full sequence and the message-queue polling requirement.

## Three things that silently break an integration if missed

1. **Secure context required.** The page must be served over HTTPS with COOP and COEP
   headers set, and the browser must support WebGL2, SharedArrayBuffer, and Atomics.
2. **WASM MIME type.** `hrz_core.wasm` must be served with `Content-Type: application/wasm`.
3. **Runtime files must reach the served output.** `hrz_core.js` and `hrz_core.wasm` ship
   inside `node_modules/@siradel-oss/horizon-core/dist/`. Prefer the runtime file location
   callback shown above (`new URL(..., import.meta.url)`) over a static-copy plugin:
   it uses the bundler's own asset-resolution mechanics, so it keeps working (including
   through file renaming/hashing) across bundlers and bundler upgrades, rather than
   depending on a bundler-specific plugin like `vite-plugin-static-copy`.
