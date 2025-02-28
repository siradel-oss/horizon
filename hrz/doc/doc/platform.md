---
Title: Hardware & platform
Category: General
---

## General-purpose memory

Some platforms Horizon runs on have drastic limitations regarding memory. Some only have a limited amount of memory available, some do not have virtual memory, and some combine the two. Low memory availability has obvious drawbacks in that not a lot of data can be loaded at any given time, putting limits on how complex the scenes can be. The absence of virtual memory induces fragmentation of the memory space, with the effect of making the application occupy a lot more memory than it actively uses. The two limitations can compound to make environments where memory management becomes tricky, especially for larger scenes.

The web build, using WASM, has no virtual memory. Mobile device usually only allow a single web page to use a few hundreds of megabytes of memory. Loading a web page containing a Horizon instance on a mobile device has the most limitations.

To combat fragmentation, Horizon stores all the scene data it downloads and transforms into a dedicated region of memory, in the form of “blobs”. Using this region avoids fragmenting memory with scene data. The counterpart is that it puts a hard limit on how much data can be loaded for scenes at a given time, but that also has the benefit of giving a better control of the overall memory usage. Completely filling the memory for blobs at runtime is not a serious problem. The engine just stops loading more data, as long as what is already loaded isn’t unloaded (by changing the camera point of view, or unloading layers for example).

Most scenes can be loaded with 250 to 500 MiB of blob space (set with the `blob_memory_pool_size` property of [[ViewerOptions]]). For mobile devices with a limited amount of memory, lower values, such as 200 MiB or even 150 MiB, can be used. Heavy scenes will not be able to load completely though.

For technical reasons, creating a web instance also requires having a limit on the overall maximum (WASM) memory usage (blobs and non-blob), which is set up through the `max_wasm_memory_size` property of [[ViewerOptions]]. As `max_wasm_memory_size` is the size of the whole memory block, and the space for blobs take up `blob_memory_pool_size`, this leaves `max_wasm_memory_size` minus `blob_memory_pool_size` for non-blob data. It is discouraged to leave less than 300 MiB for non-blob data, as some scenes may run into out-of-memory situations, and exceeding the declared maximum WASM memory at runtime is a fatal error.

In practice, only Safari cares about being able to actually allocate `max_wasm_memory_size` bytes, so for other browsers a value of 4 GiB can be used. On Safari, a value close to the maximum amount of memory a single web page can used on the device should be used.

## Threads

Horizon uses multiple threads to carry out the tasks needed to load and display scenes. Ideally there is a thread for each core of the CPU, as this is the configuration that gives the best performance. The threads include the main thread, the actor thread, and worker threads. However, each worker needs some amount of non-blob memory (50 MiB to 100 MiB) and depending on their number this memory usage can become significant. The number of workers should be limited according to the amount of memory available on the device. It can be configured through the `worker_count` property of [[ViewerOptions]].

!!! warning "Avoid too many workers"
    Having too many workers can, counter-intuitively, make the engine run more slowly, or make its framerate more erratic. This is because the main thread can become overloaded with too many worker interactions, and because the application embedding Horizon, the browser when running on the web, or the operating system itself can become starved for CPU time.

    By default the maximum number of workers is 8.

Additionally the actors can be run from the main thread instead of a dedicated one, through the `run_actors_on_main_thread` property of [[ViewerOptions]]. It is not recommended to use this option in typical production contexts as it is targeted towards debugging usage.

## Video memory

Horizon uploads data into video memory, through the WebGL API, as it runs. Just like regular memory, all devices have a set amount of video memory, and it isn’t possible to allocate more than it. However, there is no reliable way to know the size of the video memory from a web page. Moreover, instead of returning an out-of-memory error when uploading data, most environments instead crash when the video memory is full. Due to these issues, it can be vital to set a hard limit to how much data Horizon is allowed to upload to video memory.

## Automatic configuration

When the memory settings and worker count are left to their default value (`0`), Horizon tries to configure itself with reasonable values, taking the platform into account. Some browsers (Chrome and its derivatives) expose the amount of memory that is available to the web page (through the [`navigator.deviceMemory`](https://developer.mozilla.org/en-US/docs/Web/API/Navigator/deviceMemory) property). For other browsers, the values are determined from the browser type and the OS. Give values to the settings if the knowledge of the device or the scenes can help determine more appropriate values.

Default values on the web:

| Platform        | Device memory | Max WASM memory | Blob memory pool size | Worker count | Max video memory    |
|-----------------|---------------|:---------------:|:---------------------:|:------------:|:-------------------:|
| *               | 0.25 GiB      | 256 MiB         | 128 MiB               | 2            | 512 MiB             |
| *               | 0.5 GiB       | 512 MiB         | 256 MiB               | 4            | 512 MiB             |
| *               | 1 GiB         | 1024 MiB        | 320 MiB               | 8            | 512 MiB             |
| *               | ≥ 2 GiB       | device memory   | 320 MiB               | 8            | device memory * 0.5 |
| Android         | _n/a_         | 640 MiB         | 256 MiB               | 5            | 1024 MiB            |
| Safari on iOS   | _n/a_         | 640 MiB         | 256 MiB               | 5            | 600 MiB             |
| Safari on macOS | _n/a_         | 1024 MiB        | 320 MiB               | 8            | no hard limit       |

(In all cases the worker count is further limited by the value of [`navigator.hardwareConcurrency`](https://developer.mozilla.org/en-US/docs/Web/API/Navigator/hardwareConcurrency).)

It is also possible to avoid allocating blobs in a dedicated area of memory, and instead use the system allocator. This can be achieved by setting `use_system_allocator_for_blobs` on [[ViewerOptions]] to `true`. It is desirable on platforms that have virtual memory (usually, native builds). In this case `blob_memory_pool_size` can be given a large value, to avoid putting hard limits on what can be loaded.
