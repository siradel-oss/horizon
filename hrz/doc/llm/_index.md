+++
layout = "single"

[cascade]
    outputs = ["markdown"]
    exclude_from_search_index = true
+++

# Horizon TypeScript API — Key Concepts

The TypeScript API is generated from the Protocol Buffers definitions. Understanding these patterns is essential for working with it correctly.

## 1. Path builders and operations

The generated `HrzApi` namespace exposes a **path builder** per scene model root. Path builders provide a type-safe, fluent API that mirrors the proto message tree.

Entry points:
- Layer roots: `HrzApi.ImageryRasterLayerPathBuilder.create(layerHandle)`, `HrzApi.VectorTilesLayerPathBuilder.create(layerHandle)`, etc.
- Settings roots: `HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)`, `HrzApi.CameraSettingsPathBuilder.create(HrzProtocol.CameraIndex.CAMERA_0)`, etc.

Operations on scalar fields:
```ts
path.fieldName().get(api)        // AsyncApi → Promise<T>
path.fieldName().set(api, value)
path.fieldName().getSync(api)    // SyncApi
path.fieldName().setSync(api, value)
```

Operations on repeated fields (arrays) — there is no `get`/`set` on arrays:
```ts
path.myArrayCount(api)           // element count
path.addMyArray(api, value)      // append, returns new count
path.removeMyArray(api, index)   // remove by index, returns new count
path.myArray(n)                  // navigate to element n, then chain further
// Sync variants: myArrayCountSync, addMyArraySync, removeMyArraySync
```

**Granularity guidance** (two rules that sometimes conflict):
- Touch as little as possible — the engine doesn't diff state; it tracks what path was touched. Touching a high-level path can trigger expensive reloads. For a frequently-changing single value (e.g. opacity), chain all the way down to that field.
- Batch related fields — when setting several fields together, set their common parent once rather than making N separate calls.

## 2. Message queue requirement

The engine communicates results and events back to the client exclusively through a **message queue**. You must poll it continuously:

```ts
setInterval(async () => {
    const result = await api.MessageQueueService.dequeueMessages({ maxMessageCount: 100 });
    for (const msg of result.messages ?? []) {
        // dispatch on msg.payload
    }
}, 100);
```

If you stop polling, async operations (picking, vector data requests, frame capture) stall — their results will never arrive. The pump must run for the entire lifetime of the viewer. See [`api_index.md`](api_index.md) for all message types.

## 3. Two-layer vector architecture

Vector visualization requires **two layers created as a pair**:

- **`VECTOR_DATA` layer** — non-visual. Defines data sources (tiled MVT, GeoJSON, in-memory, client-provided, etc.), the attribute schema, and feature IDs. Given a user-assigned numeric `id`.
- **`VECTOR_TILES` layer** — visual. References the data layer via `source.vectorDataLayerId`, adds a styling script, and defines visual representations.

One `VectorDataLayer` can feed multiple `VectorTilesLayer`s with different styles.

## 4. Initialization sequence

```
HrzCoreBackend.init(canvas, wasmBaseUrl, options, callback)
  └─ callback(backend, INIT_SUCCESS)
       └─ new HrzApi.AsyncApi(backend)
            └─ start message pump (setInterval)
                 └─ receive ViewerReadyMessage
                      └─ now safe to issue scene commands
```

Commands sent before `ViewerReadyMessage` are silently ignored. The message pump must start before waiting for `ViewerReadyMessage` — not after.

## 5. Styling scripts

Styling scripts are a custom DSL used in the `stylingScript` field of `VectorTilesLayer` and 3D Tiles layers. They run per-feature and control filtering, coloring, and which representations to emit.

<!-- Condensed from /doc/styling_api.md — update both when the language changes. -->

**Instructions:**
```
set "propName" = <expr>;        // write a property into the current context
emit <id_or_name_expr>;         // instantiate a representation and stop this context
discard;                        // filter out the feature (stop without emitting)
fork { ... }                    // duplicate context, execute block, parent continues after
if (<expr>) { ... } elif (<expr>) { ... } else { ... }
```

**Value sources (read-only):**
```
attr("name")         // feature attribute declared in the layer
prp("name")          // property set earlier in this context (or its default value)
uniform("tile_z")    // tile depth in pyramid (vector tiles)
uniform("tile_depth")// tile depth in tree (3D Tiles)
enum("EnumName", "VARIANT_NAME")  // protocol enum value as uint
```

**Color functions:**
```
rgb(r,g,b)           rgba(r,g,b,a)       hsl(h°,s,l)        hsla(h°,s,l,a)
colorize("palette", value)               // map value through a named palette
alpha(color, a)      darken(color, amt)   lighten(color, amt)
saturate(color,amt)  desaturate(color,amt) mix_colors(c1,c2,t)
rotate_hue(color, degrees)               invert_color(color)
```
Color literals: `#rgb` `#rgba` `#rrggbb` `#rrggbbaa` (sRGB, **not** `0x...` integers).

**Numeric functions:** `add` `sub` `mul` `div` `mod` `neg` `inv` `abs` `lerp` `min` `max` `round` `floor` `ceil` `is_nan`

**Random (seeded per feature ID for stability across tiles):** `rand_unif_f(min,max)` `rand_unif_i` `rand_norm_f(mean,std)` `rand_norm_i`

**Utilities:** `fmt("{} has {} people", attr("city"), attr("pop"))` · `is_null(v)` · `value_or(a, b)` · `to_int` `to_number` `to_string` `to_color`

**Example:**
```
// Color polygons by population; discard features with no data; outline in darker shade.
if (is_null(attr("population"))) { discard; }
set "fill"    = colorize("populationPalette", attr("population"));
set "outline" = darken(prp("fill"), 0.4);
fork {
    set "opacity" = lerp(0.4, 0.9, min(1, div(attr("population"), 1000000)));
    emit "polygon";
}
emit "outline";
```

Full reference (all functions, grammar, type conversions): [`styling_api.md`](/doc/styling_api.md)
