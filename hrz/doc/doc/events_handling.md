+++
title = "Platform events"
+++

# Platform events

Events coming from the platform can be managed in different ways. Horizon comes with some internal platform events support but users can also decide to handle events themselves and feed them to engine.

## Internal event handling

The user can decide to let Horizon handle events received from the platform. In this case there is no particular action to do on the user's side as everything is handled internally.

> [!note]
> There are situations where Horizon should not be handling some events. For instance, a user could want to select multiple features by dragging their mouse to draw a box over them. We would not want the camera to move during this action: as such, [ViewerService]($proto) provides the `SetUserInteractionsEnabled` method, which can be used to dynamically disable or enable back internal event handling.

## External event handling

When Horizon cannot handle the events directly (for instance when it would prevent the integrating application from listening to those events), the application may do the events handling itself and send them to Horizon.
This is done with the `SendEvents` API method available in the [ViewerService]($proto).

```ts
let stream = HrzProtocol.EventsStream.create();

// Convert and append client-handled events to the protobuf events stream for Horizon...

api.ViewerService.sendEvents(stream);
```

> [!note]
> When events are managed outside Horizon, the engine may still process and capture events internally. To completely shutdown internal events management, Horizon provides the `disable_events_capture` options in the [ViewerOptions]($proto).

## Key bindings

In some scenarios it can be useful to rely on internal event handling, but still be able to configure key bindings for some actions. For instance, the shape editor user's experience can be greatly improved by providing key bindings for appending or removing points.
To this extent Horizon provides a list of actions that can be bound to keys. The bindings are specified in the viewer options at startup (see the example below). See [KeyAction]($proto) for the full list of key-bindable actions.

```ts
let options = HrzProtocol.ViewerOptions.create({
    // ...
    keyBindings: {
        bindings: [
            {
                key: HrzProtocol.Key.K_A,
                action: HrzProtocol.KeyAction.EDITOR_APPEND
            },
            {
                key: HrzProtocol.Key.K_S,
                action: HrzProtocol.KeyAction.EDITOR_SELECT
            },
            {
                key: HrzProtocol.Key.K_DELETE,
                action: HrzProtocol.KeyAction.EDITOR_DELETE_SELECTED_POINT
            }
        ]
    }
});
```

> [!note] Dynamic bindings
> All key actions can also be triggered using their corresponding API methods. This allows the application to trigger those actions on other events than key presses, for example by binding them to button presses.
>
> Key bindings are mainly provided for applications that wish to let Horizon handle events.
