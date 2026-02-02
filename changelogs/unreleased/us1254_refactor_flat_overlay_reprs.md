# Changed

* **Vector tiles**
    * The flat overlay representation has been split into three distinct representations: [[FlatOverlayPolygonVectorRepr]], [[FlatOverlayPolylineVectorRepr]], and [[FlatOverlayPointVectorRepr]].
    * Polygon outlines are now represented by the [[FlatOverlayPolylineVectorRepr]] representation.
    * In the flat polyline and cylinder representations, all dashes parameters have been consolidated in the [[Dashes]] message.
    * In the flat polygon representation, all polygon pattern parameters have been consolidated in the [[PolygonPattern]] message.

# Upgrade notes

* **Vector tiles**
    * Any instance of `FlatOverlayVectorRepr` must be replaced by one or more of the new representations: [[FlatOverlayPolygonVectorRepr]], [[FlatOverlayPolylineVectorRepr]], and [[FlatOverlayPointVectorRepr]].
        * The name of some fields have been shortened in the new representations as they did not need disambiguation anymore.
    * Scenes that used a single `FlatOverlayVectorRepr` with multiple geometry types (points, polylines, and polygons) must now use multiple representations, and the `fork` instruction in styling scripts with the feature type special attribute to emit those representations.
    * All dashes parameters of [[CylinderVectorRepr]] must be moved to the new [`dashes`](HrzProtocol.Dashes.html) field.
        * The `line_empty_color` field has been renamed to `secondary_color`.
        * The `dash_length` and `dash_length_unit` fields have been renamed to `primary_segment_length` and `primary_segment_length_unit`, respectively.
    * All dashes parameters of `FlatOverlayVectorRepr` must be moved to the [`dashes`](HrzProtocol.Dashes.html) field of [[FlatOverlayPolylineVectorRepr]].
        * The same renaming as above applies.
    * All polygon pattern parameters of `FlatOverlayVectorRepr` must be moved to the [`pattern`](HrzProtocol.PolygonPattern.html) field of [[FlatOverlayPolygonVectorRepr]].
