# Changed

* **Vector tiles**
    * The flat overlay representation has been split into three distinct representations: [FlatOverlayPolygonVectorRepr]($proto), [FlatOverlayPolylineVectorRepr]($proto), and [FlatOverlayPointVectorRepr]($proto).
    * Polygon outlines are now represented by the [FlatOverlayPolylineVectorRepr]($proto) representation.
    * In the flat polyline and cylinder representations, all dashes parameters have been consolidated in the [Dashes]($proto) message.
    * In the flat polygon representation, all polygon pattern parameters have been consolidated in the [PolygonPattern]($proto) message.

# Upgrade notes

* **Vector tiles**
    * Any instance of `FlatOverlayVectorRepr` must be replaced by one or more of the new representations: [FlatOverlayPolygonVectorRepr]($proto), [FlatOverlayPolylineVectorRepr]($proto), and [FlatOverlayPointVectorRepr]($proto).
        * The name of some fields have been shortened in the new representations as they did not need disambiguation anymore.
    * Scenes that used a single `FlatOverlayVectorRepr` with multiple geometry types (points, polylines, and polygons) must now use multiple representations, and the `fork` instruction in styling scripts with the feature type special attribute to emit those representations.
    * All dashes parameters of [CylinderVectorRepr]($proto) must be moved to the new [`dashes`](reference/HrzProtocol.Dashes) field.
        * The `line_empty_color` field has been renamed to `secondary_color`.
        * The `dash_length` and `dash_length_unit` fields have been renamed to `primary_segment_length` and `primary_segment_length_unit`, respectively.
    * All dashes parameters of `FlatOverlayVectorRepr` must be moved to the [`dashes`](reference/HrzProtocol.Dashes) field of [FlatOverlayPolylineVectorRepr]($proto).
        * The same renaming as above applies.
    * All polygon pattern parameters of `FlatOverlayVectorRepr` must be moved to the [`pattern`](reference/HrzProtocol.PolygonPattern) field of [FlatOverlayPolygonVectorRepr]($proto).
