+++
title = "Getting started"
+++

# Getting started

## The Core backend

In Horizon, a backend is an implementation of the API. The Core backend is the viewer itself. It is possible to implement custom backends, as will be shown in [the next chapter](custom_backend.html), but first we will explain how to initialise the Core backend, and thus the Horizon viewer itself.

All communications with this backend must always happen on the same thread the engine was initialised from. Most of the time this will be the main application thread, but on native platforms it is also possible to run the engine in a dedicated thread, in which case it is up to you to properly synchronize communications between your application and this thread, and all API calls to Horizon must happen on this thread.

## Initialising the viewer (TypeScript)

The Core backend is made available to integrators through the `@siradel-oss/horizon-core` npm package. This package contains some JavaScript and TypeScript definition files, that are used directly by the bundler (such as Webpack), as well as files that must be available to the browser through HTTPS requests at runtime. The latter group of files comprises `hrz_core.js` and `hrz_core.wasm`.

A browser supporting [WebGL 2](https://caniuse.com/webgl2), [shared array buffers](https://caniuse.com/sharedarraybuffer), and [atomics](https://caniuse.com/mdn-javascript_builtins_atomics) is required in order to run Horizon.

To embed Horizon in a web page, first you need a canvas.

```html
<html>
    <head>
        <style type="text/css">
            #canvas {
                background-color: black;
                width: 80%;
                height: 50em;
                margin: 0 auto;
            }
        </style>
    </head>
    <body>
        <canvas id="example-canvas"></canvas>
    </body>
</html>
```

When the canvas does not have an `id`, a random one will be randomly generated and assigned to it.

The complete URLs for the runtime files is a concatenation of the base URL and the file name. The responses to the queries must comply with the [same-origin policy](https://developer.mozilla.org/en-US/docs/Web/Security/Same-origin_policy), either directly or through [CORS](https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS). WASM files must be served with the `application/wasm` MIME type.

Horizon requires a [secure context](https://developer.mozilla.org/en-US/docs/Web/Security/Secure_Contexts) to run. To get one, the HTML file of the page containing the Horizon instance must be served with the [COOP](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Cross-Origin-Opener-Policy) and [COEP](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Cross-Origin-Embedder-Policy) headers as such:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Two files must be available at runtime: `hrz_core.js` and `hrz_core.wasm`. Here is an example of how to copy them at build-time using Vite.

```ts
import { viteStaticCopy } from "vite-plugin-static-copy";

export default defineConfig({
    // ...
    plugins: [
        // ...
        viteStaticCopy({
            targets: [
                {
                    src: "node_modules/@siradel-oss/horizon-core/dist/hrz_core.js",
                    dest: "<fill this in>",
                },
                {
                    src: "node_modules/@siradel-oss/horizon-core/dist/hrz_core.wasm",
                    dest: "<fill this in>",
                },
            ],
        }),
    ],
});
```

Alternatively, bundlers can detect references to assets and automatically include them in the build. When used in combination with the runtime file location callback in `HrzCoreBackend.init` (see below), the two files can be handled like any other asset. This includes allowing the bundler to rename the files.

All the files must be served with the HTTPS protocol.

The initialisation of the core is performed by calling the `HrzCoreBackend.init()` function, which takes four parameters:

 * The canvas HTML element where the planet will be drawn,
 * The base address of Horizon’s runtime files (which can be relative to the current page’s), or a callback to locate runtime files,
 * The viewer options, of type [ViewerOptions]($proto).
 * A callback function, whose parameters are:
   * A reference to the backend, of type `HrzCoreBackend`, which implements both `HrzApi.AsyncBackend` and `HrzApi.SyncBackend`,
   * An initialisation status, of type [ViewerInitStatus]($proto).

```ts
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzCoreBackend, HrzCoreRuntimeFile } from "@siradel-oss/horizon-core";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";

HrzCoreBackend.init(
    myCanvas: HTMLCanvasElement,
    runtimeFilesBaseUrl: string | ((file: HrzCoreRuntimeFile) => string),
    options: HrzProtocol.ViewerOptions,
    function(backend: HrzCoreBackend | null, initStatus: HrzProtocol.ViewerInitStatus) {
        // You got the backend here, do what you want with it.
        // It implements both SyncBackend and AsyncBackend.
    });
```

Here is an example of a runtime file location callback, that uses the bundler’s ability to resolve file locations from their asset paths:

```ts
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
    myOptions,
    () => { /* ... */}
);
```

Once the page is loaded, a WebGL 2 context will then be created by Horizon. Unlike in C++, the user is not responsible for calling `frame`, this is already done by the browser's event loop.

## Initialising the viewer (C++)

In C++, initializing the engine happens by calling the `hrz_core::Backend::create` function with some windowing system integration data, and a [ViewerOptions]($proto) instance.

### On Windows

```cpp
#include <windows.h>
#include "hrz/core/backend.h"
#include "hrz/protocol/viewer/options.h"
#include "hrz/protocol/viewer/init_status.h"

void run_horizon(HINSTANCE instance, HWND window, const hrz_proto::ViewerOptions& options)
{
    // Initialize the backend
    std::shared_ptr<hrz_core::Backend> backend = hrz_core::Backend::create(instance, window, options);

    // Check the status
    hrz_proto::ViewerInitStatus init_status = backend->init_status();

    // This is your event loop
    while (true)
    {
        // `frame` must be called often.
        // This is the only way the viewer can make progress (such as loading
        // assets or updating the scene), and this renders an image to the screen.
        backend->frame();
    }

    // Cleanup before destroying
    backend->cleanup();
}

```

### On Linux with X11

On Linux with X11, everything happens the exact same as on Windows expect that the `HINSTANCE` is instead a `Display`, and `HWND` becomes a `Window`, both from [Xlib](https://www.x.org/releases/current/doc/libX11/libX11/libX11.html).

### On Linux with EGL

[EGL](https://www.khronos.org/egl) can also be used on Linux, instead of X11. In this case, the two parameters are pointers to respectively `EGLDisplay` and `EGLSurface` structures. Note that user interaction is not supported with EGL, as its primary use within Horizon is automated testing.

## Post-initialisation checks

If the initialisation status is not `HrzProtocol.ViewerInitStatus.INIT_SUCCESS`, there has been an error, likely due to the client machine not meeting the platform requirements. (For example, not having a usable GPU.) In case of an error, the backend is unusable, so it can be disposed of. (You can simply delete the canvas.) The error value can be used to display an appropriate message to the user.

You can use the reference to the backend to interact with the Horizon instance. Most of the interactions are achieved through the [API objects](using_api.html). Both async and sync versions are available in TypeScript. Use the flavor that works best with your use case.

At startup, Horizon spends time initializing shaders. During this time, the viewer can't display a scene. A user should [listen to messages](message_queue.html) and wait for the `viewer_ready` message to know when the engine is ready to render visual content. Any interaction with the Horizon instance before receiving the message is queued and will be processed afterwards. During this loading process messages of type `viewer_loading_progress` are sent to report on the loading progress. When enabled in the [ViewerOptions]($proto), a loading screen is shown.

## Debug menu

Some information about the internal state of Horizon can be obtained in the debug menu. It can be shown either programmatically with a call to `ViewerService.ToggleDevUi()`, or toggled through a key if a binding has been specified in the viewer options.
