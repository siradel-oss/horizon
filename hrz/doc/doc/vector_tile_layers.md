---
Title: Vector tile layers
Category: Vectors
---

Vector tile layers are used to display vector data with three-dimensional shapes in the scene. They use data from a vector data layer.

They are meant to be used with tiled data, with different levels of details, selected using the distance to the camera.

## Definition

At the bare minimum, a [vector tile layer](HrzProtocol.VectorTilesLayer.html) needs a reference to a vector data layer, as well as bounds. (You can use the same bounds as the referenced vector data layer, unless you only want to use a subset of its bounds.)

Then, styling and representation information can be added to define how the vector data is styled, filtered, and displayed.

## Feature anchors

Every feature has an associated geographical position: its anchor. The anchor can be used to attach punctual representations, for clamping, or to use as virtual attributes. Depending on the feature type, the anchor is computed in different ways:

- For points, the anchor is the point's position.
- For polylines, the anchor is the middle of the polyline.
- For polygons, the anchor is the centroid of the outside ring.

## Clamping

Vector data can be clamped using three strategies:

* `NO_CLAMPING`: disables clamping. Sets an altitude value of 0 to every point. Features *will* appear under the terrain if there is any.
* `ANCHOR`: every point of a feature will have the same clamp value as the clamp of the feature's anchor.
* `PER_VERTEX`: a clamp value is computed and used for every point of the feature.

In every case, the user can also decide wether or not to add the Z component of feature's points to its altitude.

## Representations

Representations define what type of visual objects are used to display the features. Multiple representations (including of the same type) can be combined on the same layer, for the same features, to create complex visualizations.

Currently, the representations include:

* [`Extruded vector`](extruded_vectors.html), which extrudes polygons and polylines vertically.
* [`Instanced 3D model`](instanced_models.html), which represents points with 3D models.
* [`Cylinder`](cylinders.html), which represents polylines with cylinders.
* [`Flat overlay`](flat_overlays.html), which projects polygons, polylines and points on the ground.
* [`Symbol`](symbols.html), which draws a composition of texts, images, backgrounds, etc.
* [`Heatmap`](heatmaps.html), which draws aggregations of point features onto the ground.

Representations can be added, removed, and updated at any time using the appropriate methods from the scene model manipulation API.

Representations expose properties, which are values that can be set by [styling scripts](styling_api.md), which is how features are styled by attributes.

## Attributes

### From the vector data layer

Attributes loaded from the vector data layer used by the current vector tiles layer can be referenced inside the styling script (see below). To do so, use the `attributes` field of [[VectorTilesStyle]] to declare a mapping from an attribute ID (declared in the vector data layer), to the name you want to use inside the styling script.

### Anchor Z component

Sometimes it can be useful to access information about a feature's geometry inside a styling script. For now we only give access to the Z component of the anchor of each feature. This component corresponds to the geometry straight out of the vector data (before clamping). To use it, fill in the `anchor_z_attribute_name` field of [[VectorTilesStyle]]. The anchor Z will then be accessible in the styling script just like any other attribute using this name. For instance if `anchor_z_attribute_name` is set to `featureZ`:

```
set "world_offset_z" = attr("featureZ");
```

### Anchor angle component

The anchor of each feature has an intrinsic angle. For points and polygons it is 0. For polylines, it is the angle of the line segment at the anchor in Web Mercator space. 0 is east and the angle is given counter-clockwise, in radians. This value can be used to align a representation with a polyline. Use the `anchor_angle_attribute_name` to assign an attribute to retrieve this value in styling scripts.

```
set "rotation_z" = attr("anchor_angle");
```

### Feature type

When a vector data source contains geometries of different types, it can be useful to be able to distinguish between them. This is made possible using the `feature_type_attribute_name` field of [[VectorTilesStyle]]. Just like the attributes above, this gives a name to a special attribute in the styling script that can be used to know what type the current feature is. It is an enumerated attribute whose values are from [[VectorGeometryType]]. For instance if `feature_type_attribute_name` is set to `featureType`:

```
if (attr("featureType") == enum("VectorGeometryType", "POINT_GEOMETRY")) {
    emit "myPointRepr";
}
elif (attr("featureType") == enum("VectorGeometryType", "POLYLINE_GEOMETRY")) {
    emit "myLineRepr";
}
elif (attr("featureType") == enum("VectorGeometryType", "POLYGON_GEOMETRY")) {
    emit "myPolygonRepr";
}
```

!!! note "Using special attributes"
    Having special attributes (anchor Z, anchor angle, or feature type) enabled incurs a runtime cost. Avoid enabling them when they are not used in the styling script.

### Invalidation

When attribute values from the vector data layer are invalidated, the affected tiles are automatically updated to reflect the new situation. However this functionality has a runtime performance and memory cost. If a layer is never meant to have data updates, which is often the case for base map layers, tracking of invalidated data can be deactivated by setting the `static_tiles` property of [[VectorTilesLayer]] to `true`.

## Styling scripts

Styling scripts take attribute values and use them to adjust properties of the representations. They are evaluated for each feature and use the attribute values of the currently evaluated feature.

After the styling script is executed for a feature, the properties of each representation should have been set.

The attributes that are used in the script must be declared alongside the script. They are given a name, through which they are accessed in the script.

[See here](styling_api.html) for further explanations on styling vector features.

## Resolution

Vector tiles layers let the user customize when tiles will be refined through a resolution factor parameter. The higher the value the more the user will need to zoom closer to the geometry before tiles get refined. The other way around, when the resolution factor is high tiles will get refined when further from the geometry.

The parameter must be greater than `0`. The ideal value varies from one dataset to another and from the desired visual effect but a
value around `4` is a good start.

!!! important
    Be careful not to set the parameter too small as it could lead to a lot of tile downloads.

