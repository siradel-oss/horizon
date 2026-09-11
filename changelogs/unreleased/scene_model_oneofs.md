# Changed

* The following messages now use a `oneof` instead of a `type` enum field paired with numbered optional fields:
    * **Raster**
        * [NodataValue]($proto): the `type` field has been replaced by the `value_type` `oneof`.
        * [RasterProvider]($proto): the `type` field has been replaced by the `provider_type` `oneof`.
        * [TilingSchemeParams]($proto): the `type` field has been replaced by the `scheme_type` `oneof`. The deprecated `UNKNOWN` value is gone; the oneof's unset state now covers that case.
    * **Vector**
        * [VectorDataSource]($proto): the `provider_type` field is now a `oneof` of the same name.
        * [VectorRepr]($proto): the `type` field has been replaced by the `repr_type` `oneof`.
        * [TransformSymbolComponent]($proto): the `type` field has been replaced by the `component_type` `oneof`.
        * [SymbolElement]($proto): the `type` field has been replaced by the `element_type` `oneof`.
    * **Layers**
        * [LayerVisibilityConstraint]($proto): the `type` field has been replaced by the `constraint_type` `oneof`.
    * **Color**
        * [Palette]($proto): the `type` field has been replaced by the `palette_type` `oneof`.

# Removed

* The `NodataValueType`, `RasterProviderType`, `TilingSchemeType`, `VectorDataProviderType`, `VectorReprType`, `TransformSymbolComponentType`, `SymbolElementType`, and `LayerVisibilityConstraintType` enums have been removed.
* The `InMemoryAttributeValue` message has been removed and replaced with [AttributeValue]($proto).
