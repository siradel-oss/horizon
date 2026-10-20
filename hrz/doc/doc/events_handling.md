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
To this extent Horizon provides a list of actions that can be bound to keys. The bindings are specified in the `keyBindings` field of the [ViewerOptions]($proto) passed at startup, and cannot be changed afterwards. See [KeyAction]($proto) for the full list of key-bindable actions.

> [!warning] No key is bound by default
> If `keyBindings` is left empty, no key triggers any action, including the modifier key described below.

### Modifier key

The `MOD_KEY` action defines the modifier key that switches some camera interactions to an alternative mode while it is held. Without a `MOD_KEY` binding, users without a middle mouse button (e.g. on a laptop touchpad) have no way to rotate the camera with the mouse. Almost every application should therefore bind it, usually to <kbd>Ctrl</kbd>:

```ts
{ key: HrzProtocol.Key.K_CTRL, action: HrzProtocol.KeyAction.MOD_KEY }
```

### Recommended bindings

The following bindings are a sensible starting point for most applications:

| Action | Suggested key | Purpose |
|---|---|---|
| `MOD_KEY` | `K_CTRL` | Alternative camera interactions (see above). Strongly recommended. |
| `RESET_NORTH` | `K_R` | Resets the camera bearing and tilt. |
| `TOGGLE_DEV_UI` | `K_P` | Shows or hides the developer UI. Useful during development, may be omitted in production. |

### Example

```ts
let options = HrzProtocol.ViewerOptions.create({
    // ...
    keyBindings: {
        bindings: [
            {
                key: HrzProtocol.Key.K_CTRL,
                action: HrzProtocol.KeyAction.MOD_KEY
            },
            {
                key: HrzProtocol.Key.K_R,
                action: HrzProtocol.KeyAction.RESET_NORTH
            },
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
