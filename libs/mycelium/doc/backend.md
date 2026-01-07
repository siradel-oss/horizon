Mycelium : Backend
==============================

Variants
-------------------------

Mycelium comes in different flavors depending on the graphics API that is targeted. Currently the supported graphics APIs are:

 - OpenGL 3.3 Core
 - OpenGL ES 3.0
 - WebGL 2.0

The API is selected by enabling preprocessor constants at compile-time. Respectively:

- `MYCELIUM_CORE_33 = 1`
- `MYCELIUM_ES_30 = 1`
- `MYCELIUM_WEBGL_2 = 1`

`my::Instance`
-------------------------

This class correspond to a graphics API instance. Only one should be created per application.

The instance should be first initialized, then it can be created. It should be destroyed using the `delete` operator.

```cpp
if (!my::Instance::init())
{
    // Error
}

my::Instance* instance = my::Instance::create();

// Do work

delete instance;
```

Alternatively, the user must sometimes pass a function pointer to `init` that takes in the name of a function as `const char*` and outputs a `void*` to that function. This is only useful when using Mycelium in OpenGL ES 3.0 mode on desktop.

`my::ResourceContext`
-------------------------

A resource context is obtained directly through an instance `my::Instance`. It is used to allocate and deallocate resources (textures, framebuffers, buffers, etc.). The resources that can be created are:

- Buffers
- Shaders (Shader program + initial states)
- Vertex inputs (Description of vertex attributes)
- Textures
- Samplers
- Framebuffers

Additionally, a resource context can also be used to reallocate a buffer or resize a texture. A resource context is not free-threaded.

```cpp
my::ResourceContext* rc = my;

my::TextureResource resource;
// Fill in the resource structure

my::ResourceHandle texture_handle = rc->alloc(&resource);
rc->dealloc(texture_handle);

```

### Resource handles

The resource context returns opaque handles to the created resources. They are the only way to address resources through the API.

### Buffers

They are allocated using `my::BufferResource`. Each buffer can only be used in one context (vertex buffer, index buffer, or uniform buffer). This information is passed when the buffer resource is created.

In addition to its size, the user may pass initial data through the `data` field. When used, the data is copied in the resource context, which means the implementation never takes ownership or the memory the user passed in. Use `nullptr` for initializing to zero. The usage hints may be used by the graphics API to optimize data transfer.

### Vertex inputs

They are allocated using `my::VertexInputResource`. They correspond to OpenGL's vertex array objects. Basically they contain a description of each vertex input to a rasterization pipeline. There can be a maximum of 15 inputs. (Be aware that some data types, such as matrices, consume more than one input at a time.) As with buffers, the context copies the necessary memory and never takes ownership of the memory used by this struct.

### Textures

They are allocated using `my::TextureResource`. They contain a layout (size and format), an array of pointers to initial data (one for each mipmap level if the user does not want to automatically generated mipmaps, only one otherwise) (can be null to initialize to zero), and a flag to generate mipmaps or not.

In this context the depth is the size of a 3D texture or a 2D textures array. For 2D textures, it should be 1. The number of levels is the number of mipmap levels, it should always be at least 1.

The data field should never be null, but can contain array or null pointers. All data is copied to the resource context, ownership is never taken.

The `my::TextureLayout` structure contains helper methods to help the user know the required size of each mipmap level.

### Samplers

They are allocated using `my::SamplerResource`. They contain the information on how to sampler textures in shaders and whether they should use mipmaps or not.

### Framebuffers

They are allocated using `my::FramebufferResource`. They contain a set of texture attachments to be rendered into. There can be a maximum of 8  attachments. For each attachment, the user must speficy the bind point (depth, depth & stencil, color 0, color 1, etc.) and the handle to the associated texture. It is up to the user to make sure the texture formats and sizes are compatible.

All data is copied to the resource context, ownership of the values of this struct is never taken.

### Shader

They are allocated using `my::ShaderResource`. They contain a shader program composed of a vertex and a fragment source code, and an initial state.

In mycelium, most render states are static except a few that can be changed via the render context such as viewports and scissors. It is the initial state of the shader that sets the rasterization pipeline state, for instance blending, rasterization modes, depth and stencil tests, etc.

The shader resource must also define the indices of different named resource in the shader programs. The user must associate an index to a name, those indices will later be used for binding resources, as binding is never done by name. The resources that must be indexed are:

- Vertex attributes
- Uniform blocks
- Samplers (textures)
- Framebuffer outputs

As with other resources, all data is copied to the render context and to ownership if taken. Here is an example of shader allocation.

When only the initial pipeline state changes, it is possible to allocate a `my::ShaderDerivativeResource` instead, which reuses a shader but changes its initial state.

The shader can also give a hint about its linking preferences. By default, mycelium links shaders on their first use. But, a shader may want to be linked as early as possible. This also introduces the shader cache, when a shader tells mycelium to perform the linking early, the shader is cached and can be retrieve later on. In this case, user must not destroy the shader resource manually.

```cpp
my::IndexName attribs[] = {
    { 0, "i_pos" },
    { 1, "i_normal" },
};

const char* color_outputs[] = { "o_color" };

my::IndexName ubo_bindings[] = {
    { 0, "ViewData" },
    { 1, "ObjectData" },
    { 2, "FrameData" },
};

my::IndexName samplers[] = {
    { 0, "u_sun_depth" },
};

my::ShaderResource res;
res.vertex_source_len               = strlen(VERTEX);
res.vertex_source                   = VERTEX;
res.fragment_source_len             = strlen(FRAGMENT);
res.fragment_source                 = FRAGMENT;
res.attribs                         = attribs;
res.uniform_blocks                  = ubo_bindings;
res.samplers                        = samplers;
res.outputs                         = color_outputs;
res.initial_state.rasterization.cull_mode   = my::RasterizationState::Back;
res.initial_state.depth.test                = true;
res.initial_state.color_blend.enable        = true;
res.initial_state.color_blend.color.src     = my::ColorBlendState::SrcAlpha;
res.initial_state.color_blend.color.dst     = my::ColorBlendState::OneMinusSrcAlpha;
```

`my::RenderContext`
------------------

A render context is responsible for everything related to rendering. It is created directly from a `my::Instance`.

### Clearing commands

The `clear` commands is used to clear any number of target is the currently bound framebuffer. The `my::ClearTarget` values must be filled-in carefully according to the type of target that must be cleared.

### State commands

The `set_viewport` and `set_framebuffer` are used to modify the state of the rasterization pipeline. `my::ResourceHandle::null()` can be used to bind the default framebuffer (backbuffer).

### Resource update commands

Buffers and textures can be updated with `update_buffer` and `update_texture`. These methods return a pointer to a memory region that must be filled with the data to update before the render context is dispatched. It is often useful to set the sort key of these commands to 0 so the resources are updated at the beginning of the frame.

### Draw commands

The `draw` command is used to dispatch a draw call. It must include all infos related to the render job itself (vertex and instance count, index offset, etc.), the elements array (can be null), the shader resource, the vertex input resource, and all textures and uniform buffers to bind. To bind those the user can use the resource binder to facilitate state management.

The `blit` command is used to copy a region of a framebuffer on another one. The aspect flags are used to set which channels should be copied. It does not use the current bound framebuffer however, and doesn't modify it either. It is stateless.

### Texture download

To download color texture data synchronously, is is possible to insert a `color_texture_download_sync` command in the render context. This command will download the data directly to a host buffer, which may cause stalls in the rendering pipeline. Apart from a sort key, this command takes as arguments a download ID, which is the ID that will be used to uniquely identify this download command. The application is responsible for using and recycling unique IDs. Using the same ID for different concurrent download jobs results in undefined behavior. The command is also given the framebuffer containing the texture and the attachment to download, the region that must be downloaded, and the pixel format to return the data in. These formats are much more limited than generic texture formats. The result will become available after the render context has been dispatched.

Synchronous texture download may incur slowdowns as the commands queue may need to be flushed. In order to avoid this, the application can use `color_texture_download_async` instead. With this version, the result is not guaranteed to become available right after the next dispatch. With this version, the application must also provide the handle to a buffer of type `my::BufferResource::TextureDownload`. It is up to the application to make sure this buffer is large enough to contain the downloaded data. Also the application must make sure the buffer is not used or deleted before the data is retrieved with the methods listed below.

To check whether a download is ready to be retrieve, the application must use the `my::Instance::is_texture_download_ready` method with the ID that was previously used to submit the download.

To retrieve the result, the application must use `my::Instance::retrieve_texture_download`, again with the same ID. Be careful, once this method is called with a given ID, the download data ownership is given to the application, even if the download hadn't yet completed. This is why the application must first check availability.
