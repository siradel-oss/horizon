import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import * as Core from "./hrz_core";

enum Os {
    Android,
    Ios,
    Linux,
    MacOs,
    Windows,
    Other,
}

enum Browser {
    Chrome,
    EdgeChromium,
    EdgeLegacy,
    Firefox,
    InternetExplorer,
    Opera,
    Safari,
    SamsungInternet,
    Other,
}

interface Platform {
    os: Os;
    browser: Browser;
}

// From https://stackoverflow.com/a/38241481
function getOs(): Os {
    const iosPlatforms = ["iPhone", "iPad", "iPod"];
    const macOsPlatforms = ["Macintosh", "MacIntel", "MacPPC", "Mac68K"];
    const windowsPlatforms = ["Win32", "Win64", "Windows", "WinCE"];

    const userAgent = (window.navigator as any).userAgent;
    const platform = (window.navigator as any).userAgentData?.platform || window.navigator.platform;

    if (macOsPlatforms.indexOf(platform) !== -1) {
        return Os.MacOs;
    } else if (iosPlatforms.indexOf(platform) !== -1) {
        return Os.Ios;
    } else if (windowsPlatforms.indexOf(platform) !== -1) {
        return Os.Windows;
    } else if (/Android/.test(userAgent)) {
        return Os.Android;
    } else if (/Linux/.test(platform)) {
        return Os.Linux;
    }
    return Os.Other;
}

// From https://developer.mozilla.org/en-US/docs/Web/API/Window/navigator
function getBrowser(): Browser {
    const userAgent = window.navigator.userAgent;

    // The order matters here, and this may report false positives for unlisted browsers.

    if (userAgent.includes("Firefox")) {
        // "Mozilla/5.0 (X11; Linux i686; rv:104.0) Gecko/20100101 Firefox/104.0"
        return Browser.Firefox;
    } else if (userAgent.includes("SamsungBrowser")) {
        // "Mozilla/5.0 (Linux; Android 9; SAMSUNG SM-G955F Build/PPR1.180610.011) AppleWebKit/537.36 (KHTML, like Gecko) SamsungBrowser/9.4 Chrome/67.0.3396.87 Mobile Safari/537.36"
        return Browser.SamsungInternet;
    } else if (userAgent.includes("Opera") || userAgent.includes("OPR")) {
        // "Mozilla/5.0 (Macintosh; Intel Mac OS X 12_5_1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/104.0.0.0 Safari/537.36 OPR/90.0.4480.54"
        return Browser.Opera;
    } else if (userAgent.includes("Trident")) {
        // "Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 10.0; WOW64; Trident/7.0; .NET4.0C; .NET4.0E; .NET CLR 2.0.50727; .NET CLR 3.0.30729; .NET CLR 3.5.30729)"
        return Browser.InternetExplorer;
    } else if (userAgent.includes("Edge")) {
        // "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36 Edge/16.16299"
        return Browser.EdgeLegacy;
    } else if (userAgent.includes("Edg")) {
        // "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/104.0.0.0 Safari/537.36 Edg/104.0.1293.70"
        return Browser.EdgeChromium;
    } else if (userAgent.includes("Chrome")) {
        // "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/104.0.0.0 Safari/537.36"
        return Browser.Chrome;
    } else if (userAgent.includes("Safari")) {
        // "Mozilla/5.0 (iPhone; CPU iPhone OS 15_6_1 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/15.6 Mobile/15E148 Safari/604.1"
        return Browser.Safari;
    } else {
        return Browser.Other;
    }
}

function getPlatform(): Platform {
    return {
        os: getOs(),
        browser: getBrowser(),
    };
}

// Max WASM memory size is the value we cannot physically go beyond.
// Target WASM memory size is a value we try not to exceed, by
// limiting the number of workers. (But if we happen to need to go
// beyond that, we want to take a chance at allocating the memory,
// instead of crashing without even trying because the maximum
// declared size of the WASM memory has been reached.)
interface MemorySettings {
    initialWasmMemorySize: number;
    targetWasmMemorySize: number;
    maxWasmMemorySize: number;
    blobMemoryPoolSize: number;
}

function computeDefaultMemorySettings(platform: Platform): MemorySettings {
    let settings: MemorySettings = {
        initialWasmMemorySize: 469762048, // 448 MiB
        targetWasmMemorySize: 1073741824, // 1024 MiB
        maxWasmMemorySize: 4294967296, // 4096 MiB
        blobMemoryPoolSize: 335544320, // 320 MiB
    };

    const deviceMemory = (window.navigator as any).deviceMemory;
    if (deviceMemory > 0) {
        if (deviceMemory === 0.25) {
            settings.maxWasmMemorySize = 268435456; // 256 MiB
            settings.blobMemoryPoolSize = 134217728; // 128 MiB
            console.warn("Only 256 MiB of memory available. This is too low for most scenes.");
        } else if (deviceMemory === 0.5) {
            settings.maxWasmMemorySize = 536870912; // 512 MiB
            settings.blobMemoryPoolSize = 268435456; // 256 MiB
        } else {
            // deviceMemory >= 1.0
            settings.targetWasmMemorySize = deviceMemory * 1073741824; // 1024 MiB
        }
    } else {
        if (platform.os === Os.Android || platform.os === Os.Ios) {
            // Tight memory limits on mobile devices.
            settings.targetWasmMemorySize = 671088640; // 640 MiB
            settings.blobMemoryPoolSize = 268435456; // 256 MiB
        }

        if (platform.browser === Browser.Safari) {
            // Safari cares about the max WASM memory size,
            // so we cannot use an unreachable value.
            settings.maxWasmMemorySize = settings.targetWasmMemorySize;
        }
    }

    // The WASM VM runs in 32 bits, so we cannot go further than 4 GiB.
    settings.maxWasmMemorySize = Math.min(settings.maxWasmMemorySize, 4294967296); // 4096 MiB
    settings.targetWasmMemorySize = Math.min(
        settings.targetWasmMemorySize,
        settings.maxWasmMemorySize
    );
    settings.initialWasmMemorySize = Math.min(
        settings.initialWasmMemorySize,
        settings.targetWasmMemorySize
    );

    return settings;
}

function computeMemorySettings(
    options: HrzProtocol.IViewerOptions,
    platform: Platform
): MemorySettings {
    let settings = computeDefaultMemorySettings(platform);

    let optionMaxWasmMemorySize = Math.max(Number(options.maxWasmMemorySize || 0), 0);
    if (optionMaxWasmMemorySize > 0) {
        settings.maxWasmMemorySize = optionMaxWasmMemorySize;
    }
    settings.maxWasmMemorySize = Math.min(
        Math.max(settings.maxWasmMemorySize, settings.initialWasmMemorySize),
        4294967296 // 4096 MiB
    );

    let optionBlobMemoryPoolSize = Math.max(Number(options.blobMemoryPoolSize || 0), 0);
    if (optionBlobMemoryPoolSize > 0) {
        settings.blobMemoryPoolSize = optionBlobMemoryPoolSize;
    }

    const minRequiredMemory = settings.blobMemoryPoolSize + 134217728; // 128 MiB
    if (settings.initialWasmMemorySize < minRequiredMemory) {
        settings.initialWasmMemorySize = minRequiredMemory;
    }
    if (settings.maxWasmMemorySize < minRequiredMemory) {
        settings.maxWasmMemorySize = minRequiredMemory;
        console.warn(
            "Max WASM memory size too low for blob memory pool size, increasing to " +
                minRequiredMemory +
                "."
        );
    }

    if (settings.maxWasmMemorySize - settings.blobMemoryPoolSize < 314572800 /* 300 MiB */) {
        console.warn(
            "Less than 300 MiB of non-blob memory available. Loading heavy scenes could lead to a crash."
        );
    }

    settings.targetWasmMemorySize = Math.min(
        settings.targetWasmMemorySize,
        settings.maxWasmMemorySize
    );

    return settings;
}

function computeWorkerCount(
    options: HrzProtocol.IViewerOptions,
    memorySettings: MemorySettings
): number {
    let defaultWorkerCount = Math.min(
        Math.max(
            1,
            Math.min(
                Math.floor(
                    (memorySettings.targetWasmMemorySize - memorySettings.blobMemoryPoolSize) /
                        67108864 /* 64 MiB */
                ),

                navigator.hardwareConcurrency - 2
            )
        ),
        8
    );
    let optionWorkerCount = Math.max(Number(options.workerCount || 0), 0);

    if (optionWorkerCount > 0) {
        return optionWorkerCount;
    }

    return defaultWorkerCount;
}

function allocateWasmMemory(
    initialWasmMemorySize: number,
    maxWasmMemorySize: number
): {
    memory: WebAssembly.Memory;
    initialWasmMemorySize: number;
    maxWasmMemorySize: number;
} | null {
    let lastError = undefined;
    while (maxWasmMemorySize >= 469762048 /* 448 MiB */) {
        const wasmPageSize = 65536;
        try {
            let memory = new WebAssembly.Memory({
                initial: initialWasmMemorySize / wasmPageSize,
                maximum: maxWasmMemorySize / wasmPageSize,
                shared: true,
            });
            if (memory.buffer) {
                return {
                    memory: memory,
                    initialWasmMemorySize: initialWasmMemorySize,
                    maxWasmMemorySize: maxWasmMemorySize,
                };
            }
        } catch (error) {
            lastError = error;
        }

        console.warn(
            "Could not allocate WASM memory with an initial size of ",
            initialWasmMemorySize,
            "bytes and a maximum size of",
            maxWasmMemorySize,
            "bytes."
        );

        maxWasmMemorySize -= 33554432; // 32 MiB
        initialWasmMemorySize = Math.min(initialWasmMemorySize, maxWasmMemorySize);
    }

    if (lastError) {
        console.error("Could not allocate WASM memory:", lastError);
    } else {
        // 448 MiB of max Wasm memory, along with 128 MiB of blob memory, leaves
        // enough non-blob memory to load scenes with unoptimised datasets.
        console.error("Could not allocate WASM memory, 448 MiB or more are required.");
    }
    return null;
}

export class HrzCoreBackend implements HrzApi.AsyncBackend, HrzApi.SyncBackend {
    nativeApi: Core.NativeApi;

    private constructor(nativeApi: Core.NativeApi) {
        this.nativeApi = nativeApi;
    }

    public static init(
        canvas: HTMLCanvasElement,
        runtimeFilesBaseUrl: string,
        options: HrzProtocol.IViewerOptions,
        cb: {
            (HrzCoreBackend: HrzCoreBackend | null, initStatus: HrzProtocol.ViewerInitStatus): void;
        }
    ) {
        if (!window["SharedArrayBuffer"]) {
            cb(null, HrzProtocol.ViewerInitStatus.BROWSER_FEATURES_NO_SHARED_MEMORY);
            return;
        }
        if (!window["Atomics"]) {
            cb(null, HrzProtocol.ViewerInitStatus.BROWSER_FEATURES_NO_ATOMICS);
            return;
        }

        runtimeFilesBaseUrl = runtimeFilesBaseUrl || "";
        if (runtimeFilesBaseUrl != "" && !runtimeFilesBaseUrl.endsWith("/")) {
            runtimeFilesBaseUrl += "/";
        }

        const locateFile = function (path: string) {
            if (path.startsWith("/")) {
                return path;
            } else {
                return runtimeFilesBaseUrl + path;
            }
        };

        canvas.tabIndex = 0;

        canvas.addEventListener("mousedown", function (e) {
            (e.target as HTMLElement).focus();
        });

        canvas.addEventListener(
            "contextmenu",
            function (e) {
                e.preventDefault();
            },
            true
        );

        if (canvas.id === undefined) {
            canvas.id = "__horizon_canvas__";
        }

        let selector = "#" + canvas.id;

        const platform = getPlatform();

        // Allocate the WASM memory here instead of letting Emscripten do it.
        // This allows choosing the maximum memory size depending on the device,
        // as Emscripten hardcodes the value at compile time.
        // See https://github.com/emscripten-core/emscripten/issues/13569
        const memorySettings = computeMemorySettings(options, platform);

        let wasmMemory = allocateWasmMemory(
            memorySettings.initialWasmMemorySize,
            memorySettings.maxWasmMemorySize
        );
        if (wasmMemory === null) {
            cb(null, HrzProtocol.ViewerInitStatus.NOT_ENOUGH_MEMORY);
            return;
        }

        if (!(wasmMemory.memory.buffer instanceof SharedArrayBuffer)) {
            console.error(
                "Requested a shared WebAssembly.Memory but the returned buffer is not a " +
                    "SharedArrayBuffer, indicating that while the browser has SharedArrayBuffer " +
                    "it does not have WebAssembly threads support - you may need to set a flag"
            );
            cb(null, HrzProtocol.ViewerInitStatus.BROWSER_FEATURES_NO_SHARED_MEMORY);
            return;
        }

        if (memorySettings.maxWasmMemorySize !== wasmMemory.maxWasmMemorySize) {
            // The max memory size had to be decreased due to failed allocations.
            memorySettings.maxWasmMemorySize = wasmMemory.maxWasmMemorySize;

            if (
                memorySettings.maxWasmMemorySize - memorySettings.blobMemoryPoolSize <
                314572800 /* 300 MiB */
            ) {
                // The space for non-blob memory must be preserved.
                memorySettings.blobMemoryPoolSize = Math.max(
                    memorySettings.maxWasmMemorySize - 314572800 /* 300 MiB */,
                    0
                );
            }
        }

        options.workerCount = computeWorkerCount(options, memorySettings);
        options.maxWasmMemorySize = memorySettings.maxWasmMemorySize;
        options.blobMemoryPoolSize = memorySettings.blobMemoryPoolSize;

        options.maxVideoMemorySize = Math.max(Number(options.maxVideoMemorySize || 0), 0);

        if (
            options.maxVideoMemorySize === 0 &&
            (platform.os === Os.Android || platform.os === Os.Ios)
        ) {
            if (platform.os === Os.Ios) {
                // 600 MiB seems to be a practical max limit for VRAM usage on iOS.
                options.maxVideoMemorySize = 629145600; // 600 MiB
            } else {
                const deviceMemory = (window.navigator as any).deviceMemory;
                if (deviceMemory > 0) {
                    // We have an idea of how much memory the device has.
                    // As the memory is usually shared between the CPU and
                    // the GPU, we assume that we can use half of it.
                    options.maxVideoMemorySize = (deviceMemory * 1024 * 1024) / 2;
                } else {
                    // We have no explicit info on the device's memory size,
                    // set an arbitrary limit of 1 GiB.
                    options.maxVideoMemorySize = 1073741824;
                }

                // There's not much we can do with less than 512 MiB of VRAM, so
                // we allow the engine to go this far.
                options.maxVideoMemorySize = Math.max(options.maxVideoMemorySize, 536870912);
            }
        }

        let params = {
            // If this property is not overriden, the worker loads the bundled JS file.
            mainScriptUrlOrBlob: locateFile("hrz_core.js"),
            locateFile: locateFile,
            wasmMemory: wasmMemory.memory,
            workerCount: options.workerCount,
        };

        Core(params).then((api: Core.NativeApi) => {
            let serialized_options = HrzProtocol.ViewerOptions.encode(options).finish();
            var ptr = api._malloc(serialized_options.length);
            api.HEAPU8.set(serialized_options, ptr);

            let initStatus = api.hrz_init({ data: ptr, size: serialized_options.length }, selector);
            api._free(ptr);

            cb(new HrzCoreBackend(api), initStatus as HrzProtocol.ViewerInitStatus);
        });
    }

    public rpc(service: number, method: number, input: Uint8Array): Promise<Uint8Array> {
        return Promise.resolve<Uint8Array>(this.rpcSync(service, method, input));
    }

    public rpcSync(service: number, method: number, input: Uint8Array): Uint8Array {
        var ptr = this.nativeApi._malloc(input.length);
        this.nativeApi.HEAPU8.set(input, ptr);

        var outputData = this.nativeApi.hrz_rpc(service, method, { data: ptr, size: input.length });

        this.nativeApi._free(ptr);

        var outputBuffer = new Uint8Array(
            this.nativeApi.HEAPU8.slice(outputData.data, outputData.data + outputData.size)
        );

        this.nativeApi.hrz_free_rpc(outputData.data);

        return outputBuffer;
    }
}
