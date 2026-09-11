# Changed

* In [ShapeEditorMessage]($proto), the `shape` field has been split into `shape_selection` and `shape_geometry_update` to match their corresponding message types.
* In [ShapeEditorMessage]($proto), the `mode` and `control_point` fields have been renamed to `mode_switch` and `control_point_selection` to match their corresponding message types.

# Removed

* The `MessageType` enum and the `type` field of [TypedMessage]($proto) have been removed.
* The `ShapeEditorUpdateType` enum and the `type` field of [ShapeEditorMessage]($proto) have been removed.

# Upgrade notes

* Messages must now be dispatched on their payload discriminator field instead of a `type` field.
    * In TypeScript, use protobufjs' `payload` discriminator (e.g. `msg.payload == "pick"`) instead of `msg.type == MessageType.PICK_MESSAGE`.
    * In C++, use `TypedMessage::payload_case()` (e.g. `TypedMessage::kViewerReady`) instead of comparing against `MessageType`. Same applies to `ShapeEditorMessage::payload_case()`.
    * Many other messages have been converted to using `oneof`s. See their documentation to learn the new discriminant and variant field names.
