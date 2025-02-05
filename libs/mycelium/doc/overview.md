Mycelium overview
======================

### Backend

- Abstraction over the graphics API.
- Handles resource management (creation and destruction) via the resource context.
- Handles commands (draw, update, bind, etc.) via the render context.

### Renderer

- Allow binning renderable objects.
- Can draw objects in multiple views.
- Culls objects against each view.

### Resource binder

- Stack-like structure that allows binding and overriding textures and uniform buffers.
- States can be saved and restored.

### Render graph

- Schedules a set of render passes through dependencies.
- Automatically allocated resources and resizes them with the backbuffer.
