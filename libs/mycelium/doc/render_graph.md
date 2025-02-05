
Mycelium: Render graph
===========================

The render graph is a high level system used to schedule render passes based on a dependency graph, allocated render target, resize them, and alias the render targets when possible in order to save memory. It can be created through its static `create` method and must be detroyed with the `delete` operator.

Render passes
---------------------

To declare a render pass, the user must write a class that extends the `my::RenderPass` interface. To register the pass in the graph, they then must use the `add_pass` method on the graph. A render pass must implement three methods.

### Setup

The `setup_pass` method allows a pass to tell the graph what resource it will read, write, or create. The setup context contains three methods:

- `create`: The render pass will generate a resource. For instance, this is the first pass that will write to a render target. The user must fill in the information about the resource: size and format. The size can either be absolute or relative to the swapchain, in which case it will be resized automatically.
- `read`: This tells the render graph that the pass will read a resource. For instance the ambient occlusion can read a previous depth buffer.
- `read_write`: This tells the render graph that the pass will read and write to a render target. For example this is used when a pass write to a depth buffer that was previously rendered into, without clearing its content. In this case the user must inform the render graph of which resource will be read and which will be output.

In a single render graph, there cannot be resources with duplicate names.

### Resource retrieval

In this pass, the pass can retrieve the resource handles of each resource it has declare in the setup method. Then the pass can create the necessary framebuffers for example.

When the backbuffer is resized, the dependent render targets keep the same resource handle.

### Execution

Finally the execute method tells the pass to actually to its rendering. It is given a sort key that can be completed manually of by other systems such as the renderer, and a bunch of pointers to other systems. The instance, resource context and render context are mandatory, the resource binder and renderer are optional.

Here is an example of a render pass that blits a texture to the main framebuffer:

```cpp
class PresentPass: public my::RenderPass
{
    const char*             _to_present;
    my::ResourceHandle      _color;
    my::ResourceHandle      _fbo;

public:
    PresentPass(const char* to_present) :
        _to_present(to_present)
    {
    }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        ctx.read(_to_present);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _color = ctx.retrieve(_to_present);

        my::FramebufferAttachment attachments[] = {
            { my::Attachment::Color0, _color },
        };

        my::FramebufferResource res;
        res.attachment_count    = 1;
        res.attachments         = attachments;

        _fbo = rc->alloc(&res);
    }

    void execute(const my::RenderGraph::ExecutionContext& ctx) override
    {
        ctx.render->blit_framebuffers(
            _fbo,
            my::ResourceHandle::null(),
            my::Rect{ 0, 0, ctx.backbuffer_width, ctx.backbuffer_height },
            my::Rect{ 0, 0, ctx.backbuffer_width, ctx.backbuffer_height },
            my::Aspect_Color,
            my::Sampler::Filter::Nearest);
    }
};

```

Building
---------------------

Once all passes have been registered, the render graph can be built. For this step, it is necessary to inform the system about which render passes should be executed last. The execution order of all other passes is inferred from dependencies.

Building the render graph can fail if there are mistakes such as undeclared resources or cycles.

Building should only be done once. If the user wants to change the render graph in any way, they must create a new one.

Executing
---------------------

The graph can finally be executed at every frame. The user must pass in the execution context that will be fed to the each pass.

Freeing the resources
---------------------

Before deleting the render graph, its resource must be releasing through a resource context using the `free` method.

```cpp
my::RenderGraph* rg = my::RenderGraph::create();

ForwardPass forward_pass;
PresentPass present_pass(forward_pass.output_name);

rg->add_pass("forward", &forward_pass);
auto present_pass_id = rg->add_pass("present", &present_pass);
rg->build(my, rc, 1, &present_pass_id);

// ...

// Each frame:
my::RenderGraph::ExecutionContext ctx;
ctx.backbuffer_width    = vp_w;
ctx.backbuffer_height   = vp_h;
ctx.instance            = my;
ctx.resource            = rc;
ctx.render              = r;
ctx.binder              = rb;
ctx.renderer            = re;
ctx.user_data           = nullptr;
rg->execute(ctx);

// ...

rg->free();
delete rg;
```
