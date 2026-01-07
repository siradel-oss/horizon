Mycelium: Renderer
===========================

The renderer is a high level system used for binning renderable objects, culling them, and rendering them from multiple views. It can be created through its static `create` method and must be detroyed with the `delete` operator.

Render bins
-----------------

The application can register up to 32 render bins. Each render bin is a bit in a bitmask. For instance a bin can be the opaque world objects, the transparent UI elements, the terrain objects, etc. A single object may be in multiple render bins, but it is then up to the user not to render them multiple times.

When registering the bin, the user specifies the bit number assigned to this bin, and the sorting mode to use (front to back or back to front).

Views
----------------

A view corresponds to a camera, be it perspective or orthographic. Is contains the direction of the depth axis, the position relative to which the near and far planes are defined, and the projection matrix.

It is used to compute the depth of the renderable objects in order to sort them correctly, and to cull them against the views.

The renderer has a main view corresponding most of the time to the main camera. It can also register auxiliary views through the `add_auxiliary_view` method. Those views can be used to render depth maps, environment probes, etc. **They must be registered before any renderable object**. There can be a maximum of 31 auxiliary views registered at once.

When registering a view (main or auxiliary), one may specify the bins usable with the view. This is used to optimize culling when collecting renderables. For instance it is unlikely that a UI renderable will be drawn in a shadow map view, so it makes sense to exclude the UI bin from the shadow map view.

Collecting renderables
----------------

All systems that aim at registering renderable objects must implement the `my::Renderer::Renderable` interface and then injected through the `collect_renderable` method.

In the `collect_render_info` method, the render systems must add all objects they must render.
They can use the culler to query the position of the main camera (useful for level of details), and to test a bounding sphere against the main view or any registered view.

The system can then register the object to render through the `enqueue` method of the queue. This method takes in the bin mask defining every bin the object must be rendered for, a callback that will be used for actual rendering, a pointer to the render data passed to the callback, and a bounding sphere used for culling.

When culling and enqueueing, one may specify a bin mask in order to optimize what objects are culled and enqueued into what views by only taking into account views that were registered with the correct bins.

```cpp
static void draw(
    uint32_t render_type,
    my::RenderContext* r,
    my::ResourceBinder* rb,
    const void* user_data_raw,
    const void* raw)
{
    auto data = (const RenderData*)raw;
    my::ResourceHandle shader;

    switch (render_type)
    {
        case RenderVisual:
            shader = data->shader;
            break;
        case RenderDepth:
            shader = data->shader_depth;
            break;
        default:
            return;
    }

    // Actual drawing through the render context.
}

void collect_render_info(
    my::Renderer::Queue& queue,
    const my::Renderer::Culler& culler) const override
{
    if (culler.is_visible_in_any_view(center, radius, bin_mask))
    {
        queue.enqueue(bin_mask, draw, render_data, center, radius);
    }
}
```

Drawing
--------------

When all objects have been collected, the renderer can be drawn using the `draw` method.

This methods takes in:
- The sort key to be completed by the renderer.
- The render type (a 32-bit unsigned int user-defined value that can be used for instance to inform the renderable object of the context in which it will be rendered: in the example above it's used to inform the object it must be rendered as color of depth only).
- The view id to render. Use `my::MainView` for the main view.
- The passes to render.
- A render context and an optional resource binder. Both will be passed down to the renderable objects callbacks.

The passes are the set of bin bitmasks to render. For instance:

- Having 2 passes, `{ TransparentBin, OpaqueBin }`, will first draw the transparent objects, then the opaque ones.
- Having 1 pass, `{ TransparentBin | OpaqueBin }`, will draw the transparent objects and the opaque ones in the order they are defined. If the opaque bin has bit 0 and the transparent bin has bit 1, it will draw the opaque first, then the transparent.

This mechanism can be used to have more fine-grained control over the draw order by ignoring the definition order of render bins.

Resetting
----------------

The renderer can be reset after it has served its purpose in order to recycle memory. This erases all registered auxiliary views, renderable objects, but not render bins.

```cpp
// Setup...
my::Renderer* re = my::Renderer::create();
re->register_bin(OpaqueBinBit, my::SortMode::FrontToBack);
re->register_bin(TransparentBinBit, my::SortMode::BackToFront);

// Each frame...
re->set_main_view(/* ... */);
scene.collect_views(re); // Add auxiliary views

for (auto renderable: /* scene renderables */)
{
    re->collect_renderable(renderable);
}

my::Renderer::BinMask to_render = OpaqueBin | TransparentBin;
re->draw(
    /* render type */,
    my::MainView,
    {&to_render, 1},
    /* render context */,
    /* resource binder */,
    /* user data */);


// Cleanup
delete re;
```
