---
Title: The scene model
Category: General
---

*Most examples below are given in TypeScript, but they are similar in all target languages.*

## Layers

The scene model is a tree structure that describes an entire scene. The tree can have many different roots. Layers are a type of root nodes that essentially declare how and what is going to be displayed in the scene. A layer is only described by its [type](HrzProtocol.LayerType.html), and identified by its [handle](HrzProtocol.LayerHandle.html). They are created, destroyed, and queried by using the [Layer service](HrzProtocol.LayerService.html).

Each layer type has an associated message type that describes it entirely. Similarly, each layer instance has an associated message instance that describes it entirely. Below is an example of the structure of such a message.

```text
SingleModelLayer
  ├── url
  ├── position
  │   ├── latitude
  │   ├── longitude
  │   └── altitude
  └── transform
      ├── offset
      ├── scale
      └── rotation
```

## Scene view settings

The scene model is provided with a root node, called [[SceneViewSettings]], that is used to configure general aspects of a scene view. It uses the scene view index as parameter. This is shown in the example below (read the following sections for more details on the use of paths).

```ts
let path = HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0);
let sceneViewSettings = await path.get(api);
```

## Scene settings

The scene settings can be configured through the [[SceneSettings]] root. This node can be seen as general options configuration therefore it does not take any parameters. Those settings configure what views are in use.

```ts
let path = HrzApi.SceneSettingsPathBuilder.create();
let sceneSettings = await path.get(api);
```

## Camera settings

Similarly to scene view settings, there is a scene model root for [[CameraSettings]], that is used to configure view-independent aspects of the camera, for each camera.

```ts
let path = HrzApi.CameraSettingsPathBuilder.create(HrzProtocol.CameraIndex.CAMERA_0);
let cameraSettings = await path.get(api);
```

## Addressing the scene model with paths

The scene model is never accessed directly via the [[SceneModelService]]. Instead, the user is expected to use the scene model API, which allows read and write access to the scene model in a type-safe way.

At the heart of this API are paths. Paths describe a location in the scene model. They all start from a [root](HrzProtocol.PathRoot.html).

Paths would be a bit unwieldy to build by hand, so the API exposes types enabling their construction with a fluent API, in the form of path builders.

For instance, a path to the scene model of a single 3D model must be created with `SingleModelLayerPathBuilder`, using the layer handle. The example below creates a path that points to the entire `SingleModelLayer` structure.

```ts
const pathBuilder = HrzApi.SingleModelLayerPathBuilder.create(handle);
```

Then, the path can be made to point to one of its fields by calling the corresponding method on the builder. A new path builder is returned, with a type corresponding to the newly pointed property. The reference to the original builder is consumed and must not be reused.

```ts
const offsetPathBuilder: HrzApi.Vec3fPathBuilder = pathBuilder.transform().offset();
```

Because the types reflect the property tree, it is only ever possible to point to valid properties.

Moving deeper into the tree consumes paths. If you need diverging paths, the `clone()` method can be used. Reusing a path builder is illegal and will lead to an error.

```ts
// Do
const transform = HrzApi.SingleModelLayerPathBuilder.create(handle).transform();
const offset = transform.clone().offset(); // A clone is created, `transform` is still valid.
const scale = transform.scale(); // `transform` is consumed.

// Don't
const transform = HrzApi.SingleModelLayerPathBuilder.create(handle).transform();
const offset = transform.offset(); // `transform` is consumed.
const scale = transform.scale(); // Error: Reuse of `transform`.
```

## Accessing the scene model

When the user reaches the field they want to work with, they can retrieve or mutate its value using the `get[Sync]` and `set[Sync]` methods. All terminal methods take in the API instance. Here `api` is an instance of `HrzApi.AsyncApi`. The `Sync` variants use a `HrzApi.SyncApi`.

```ts
// path points to SingleModelLayer.transform.offset
const offset = pathBuilder.clone().get(api);
offset.x += 2;
path.set(api, offset);
```

Because path builders are used to create paths, and the methods used to manipulate values are directly on the builders and return promises, paths are not used directly when interacting with Horizon through its API.

Fields that are repeated (arrays) act differently from other fields:

- They do not have a `get` or `set` method.
- Their path is built by passing the index in the array to access.
- They have a `<field>Count` method.
- They have an `add<Field>` and a `remove<Field>` method for adding and removing elements that also return the new number of elements.

```ts
pathBuilder.clone().myArrayCount(api);         // Let's suppose it returns 0
pathBuilder.clone().addMyArray(api, obj1);     // Returns 1
pathBuilder.clone().addMyArray(api, obj2);     // Returns 2
pathBuilder.clone().myArrayCount(api);         // Returns 2
pathBuilder.clone().myArray(1).get(api);       // Returns obj2
pathBuilder.clone().removeMyArray(api, 0);     // Returns 1
pathBuilder.clone().myArray(0).get(api);       // Returns obj2
pathBuilder.clone().myArray(0).set(api, obj3); // Sets the first object in myArray to obj3
```

When using the synchronous API in TypeScript, all the terminal methods on paths have `Sync` appended to their names. They become `getSync`, `setSync`, `countSync`, `addSync`, and `removeSync`.

```ts
pathBuilder.clone().myArrayCountSync(api);
pathBuilder.clone().addMyArraySync(api, obj1);
pathBuilder.clone().myArray(1).getSync(api);
```

## Dumping and loading a scene

A utility library called `horizon-scene-dump` is provided to dump an entire scene to JSON, or reload a scene from a dump. It is only available for TypeScript. The provided methods are `loadScene(Obj|Json|Bin|Base64)(Sync|Async)` and `dumpScene(Obj|Json|Bin|Base64)(Sync|Async)`.

It can be useful for debugging the internal state of the scene or for reporting issues, but it shouldn't be relied upon by integrating applications because its format and the structure of the scene model may change at any point.

```ts
// Dump a scene.
let dump = await dumpSceneObjAsync(api, "My scene name", /* viewpoints = */ []);

// Load a dumped scene.
// It will also delete all existing layers from the instance.
let loadedInfo = await loadSceneObjAsync(api, dump,
  /* cameraAnimationOptions = */ {},
  /* clearScene */ = true);
```
