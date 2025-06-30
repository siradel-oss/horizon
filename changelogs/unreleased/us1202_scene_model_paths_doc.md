# Changed

* Return message types for [[SceneModelService]] methods have been changed. `SceneModelGet` has been replaced with [[BytesValue]], and `SceneModelArrayCount` with [[UInt32Value]].

# Upgrade notes

* If you use the [[SceneModelService]] directly instead of using path objects, calls to the service need to be updated in order to conform to the new return types. The new types are functionally the same but the value field is named `value` instead of `payload` and `count`.
