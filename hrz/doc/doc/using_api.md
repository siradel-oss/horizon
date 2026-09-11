+++
title = "Using the API"
+++

# Using the API

The API is responsible for translating typed method calls to serialized remote procedure calls expected by backends. Thus we can view the API as a simple utility over a backend. This means that when creating an instance of the API, we must compose it with an instance of the backend.

After it has been created, each service is accessible via the API.

Note that no matter the language or platform, the API should always be used from the thread running the engine’s `frame` method.

## C++

In C++, the backend is expected as a shared pointer by the API. Note that other systems such as the scene model expect the API instance itself to be a shared pointer, so it is a good idea to initialize it as one.

In C++ only, both input and output are method arguments. This allows the user to control memory allocation by using a [protobuf arena](https://developers.google.com/protocol-buffers/docs/reference/arenas).

```cpp
#include "hrz/api/api.h"

void main()
{
    // Assuming we already have a std::shared_ptr<hrz_api::Backend>
    // called backend.

    std::shared_ptr<hrz_api::Api> api = hrz_api::Api::create(backend);

    hrz_proto::LayerHandle layer_handle;
    api->layer_service.create_layer(/* ... */, layer_handle);
}
```

## TypeScript

Depending on the interface used (synchronous or asynchronous), either `SyncApi` or `AsyncApi` must be used to access the API.

### Synchronous interface

```ts
import { HrzApi } from "@siradel-oss/horizon-api";

function main() {
    // Assuming we already have a HrzApi.SyncBackend called backend

    const api: HrzApi.SyncApi = new HrzApi.SyncApi(backend);

    const layerHandle = api.LayerService.createLayer(/* ... */);
}
```

### Asynchronous interface

This is very similar to the synchronous interface, except that the methods return promises.

```ts
import { HrzApi } from "@siradel-oss/horizon-api";

async function main() {
    // Assuming we already have a HrzApi.AsyncBackend called backend

    const api: HrzApi.AsyncApi = new HrzApi.AsyncApi(backend);

    const layerHandle = await api.LayerService.createLayer(/* ... */);
}
```
