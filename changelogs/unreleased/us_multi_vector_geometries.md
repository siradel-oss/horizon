# Added

* Added support for vector features made of a collection of geometries of different types.
    * Each representation renders the geometries it supports, and ignores the others.

# Changed

* **Vector tiles**
    * Multi-geometry features are no longer split into multiple features in vector tiles.
    * [FlatOverlayPointVectorRepr]($proto), [HeatmapVectorRepr]($proto), and [ModelVectorRepr]($proto) now render one instance per point, including every point of multi-points and geometry collections.
    * Instanced models are no longer placed on polyline and polygon geometries.
    * [SymbolVectorRepr]($proto) symbols are now placed at the anchor of their feature, for any geometry type.
    * The anchor placement rules have been updated. See the detailed [anchor placement rules](/doc/vector_tile_layers.md#feature-anchors).
    * The [feature type special attribute](/doc/vector_tile_layers.html#feature-type) now has an undefined value for features containing geometries of different types.
    * The `InMemoryVectorFeature` and `ClientFeature` messages have been merged into the [VectorFeature]($proto) message.
    * The `InMemoryVectorFeatureGeometry` and `ClientFeatureGeometry` messages have been merged into the [VectorFeatureGeometry]($proto) message.
    * [VectorFeature]($proto) now contains an array of [VectorFeatureGeometry]($proto), allowing it to represent multi-polygons and geometry collections.

# Fixed

* Fixed [ExtrudedGeometryVectorRepr]($proto) drawing a wall between consecutive linestrings of the same polyline feature.
* Fixed non-first points in multi-points Geobuf features not being placed at their correct location.
* Fixed styling random functions not being deterministic between builds when evaluated without feature IDs.

# Upgrade notes

* Replace instances of `InMemoryVectorFeature` and `ClientFeature` with [VectorFeature]($proto).
* Replace instances of `InMemoryVectorFeatureGeometry` and `ClientFeatureGeometry` with [VectorFeatureGeometry]($proto).
