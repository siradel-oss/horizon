Mycelium: Resource binder
======================

The resource binder is a simple utility class that can be used to handle state inheritance and overriding when binding shader resources (textures and uniform buffers). It is created by its static `create` method and must be destroyed with the `delete` operator.

It has a stack-like mechanism through its push and pop methods to save and restore the current state.

The current state can be queried and then used in a draw call. However the `my::ResourceBinder::State` structure contains data that is owned by the resource binder, and thus should not be mutated or taken ownership of by an outside system.

```cpp
void draw_something(my::ResourceBinder* rb, my::RenderContext* r)
{
    rb->push_state();
    rb->bind(/* ... */);

    auto state = rb->get_current_state();

    r->draw(/* ... */
        state.ubos, state.textures);

    rb->pop_state();
}

```
