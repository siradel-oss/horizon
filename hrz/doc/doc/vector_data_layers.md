---
Title: Vector data layers
Category: Vectors
---

Vector data is made of features, geographically described by geometry data (polygons or points for example), to which attribute values can be associated. Because the same features can be displayed using multiple different representations at the same time, customised with several attributes, the same vector data set can be used by multiple layers simultaneously. In order to avoid duplication of the definitions in the API and to avoid unneeded downloads and memory consumption, vector data is declared by the means of a vector data layer.

[Vector data layers](HrzProtocol.VectorDataLayer.html) are virtual layers meant to describe where to fetch vector data from and what it contains. Contrarily to most other layers, vector data layers do not have direct visual representations, instead they make data available to other layers. These other layers refer to a vector data layer through a globally unique identifier, in the form of an integer. Therefore, in order to display vector data you have to declare (at least) two layers: a vector data layer and one (or more) visible layer that uses the vector data.

## Vector data sources

One vector data layer corresponds to one geometry data set. A data set is composed of one geometry source (identified using the `has_geometry` field) and zero or more attribute sources. See the documentation of [[VectorDataSource]].

* The geometry source allows retrieving the geographical definition of the features: points, polylines, or polygons.
* Attribute values are arbitrary values attached to each feature. These values can be used for styling [vector tile layers](vector_tile_layers.html).

Each [[VectorDataSource]] has a provider, which specifies how its data is retrieved. Multiple options are available:

* [Tiled vector data provider](HrzProtocol.TiledVectorDataProviderParams.html) to download vector data from tiled data sets, in [Mapbox Vector Tile](https://github.com/mapbox/vector-tile-spec), [GeoJSON](https://geojson.org/), or [Geobuf](https://github.com/mapbox/geobuf) formats. The data must be tiled with the [XYZ tiling scheme](https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames).
* [Untiled vector data provider](HrzProtocol.UntiledVectorDataProviderParams.html) to download vector data from untiled data sets (in [GeoJSON](https://geojson.org/) or [Geobuf](https://github.com/mapbox/geobuf) formats) and tile it, simplifying and clipping features as needed.
    * The bounds of the resulting tileset are the intersection of the source file’s data bounds and the bounds in the parameters of the vector data source.
    * Levels of detail are provided for as long as zooming in is necessary to resolve small features. The source’s `max_level` is used as an upper bound. (A large value can be used for `max_level` to let the engine decide when to stop.)
    * The degree of simplification can be tuned with the `tolerance` parameter.
    * When compared to setting up a tiled vector data provider with a single level-0 tile, this option improves performance and eliminates position jitter, at the cost of increased memory usage and loading time. But keep in mind that it is always preferable to perform tiling offline as a pre-processing step (for example, by using a tool such as [Tippecanoe](https://github.com/mapbox/tippecanoe)).
* [TileJSON vector data provider](HrzProtocol.TileJsonVectorDataProviderParams.html) to download vector data from [Mapbox Vector Tile](https://github.com/mapbox/vector-tile-spec) tiled data sets, using [TileJSON](https://github.com/mapbox/tilejson-spec) layer descriptor files to determine how tiles can be retrieved. (Note that although TileJSON files can describe feature attributes, they still have to be declared explicitly in the vector data layer, so that they can be typed and be assigned numerical IDs.)
* [PMTiles vector data provider](HrzProtocol.PmTilesVectorDataProviderParams.html) to download data from a [PMTiles version 3](https://github.com/protomaps/PMTiles/tree/main) file, using range requests. Just like the TileJSON provider, attributes must also be declared explicitly.
* [In-memory vector data provider](HrzProtocol.InMemoryVectorDataProviderParams.html) to retrieve data stored in [in-memory vector data layers](client_vector_data.html#in-memory-vector-source).
* [Client vector data provider](HrzProtocol.ClientVectorDataProviderParams.html) to [ask the client](client_vector_data.html#client-vector-data) for attribute values.

Providers must provide the extents of their data set. This includes the minimum and maximum LODs, as well as geographical bounds. They are either declared explicitly in the parameters, or determined automatically from the source data. (One notable exception is the client vector data provider, when it is configured to get data by feature ID: it does not have geographical bounds.) The resulting bounds of the whole vector data layer is the intersection of all the vector data sources’ bounds.

## Feature IDs

Within a vector data set, each feature can be uniquely identified by a tuple of one or multiple attribute values, referred to as its ID. The geometry source associates geometries with IDs, and attribute sources associate the same IDs with additional attribute values.

Each attribute has an `is_feature_id` property. Set it to `true` in order to include the values of that attribute in the feature IDs.

Feature ID are necessary for some functionalities:

* Identifying a picked feature,
* Selecting and highlighting features,
* Joining attribute values from multiple sources in the same vector data layer,
* Using attribute values from vector data layers to style 3D Tiles.

All sources of a same vector data layer must use the same definition of a feature ID. Otherwise the IDs cannot match and the data from multiple sources cannot be combined.

When a vector data layer has a single source, and picking or selection/highlighting are not needed, it isn’t necessary to declare feature IDs.

## Attributes

Just like a vector data layer is identified by a unique integer, so is each attribute. When another layer needs attribute values (for example when styling feature representations), it refers to the attribute by this integer ID. Accordingly, the feature ID is defined by a list of attribute IDs. The client is responsible for choosing these integer identifiers. Horizon does not care about the values themselves, only that the references are consistent inside the scene. Attribute IDs are local to their vector data layer: the same IDs can be used in multiple vector data layers without any risk of mixing the attribute values.

<p style="text-align:center;">
    <img style="height: 450px" src="img/vector_data_layer_ids.svg" alt="Vector data layer IDs" />
</p>

See the [vector attributes](vector_attributes.html) page for information about attributes typing and generally how they work.

GeoJSON, MVT, PMTiles, and Geobuf attributes are named with strings. For each attribute, fill the property `source_name` with the name of the attribute inside the data source file. In-memory and client vector sources use the attribute IDs directly, so there is no need to specify this property.

GeoJSON, MVT, PMTiles, and Geobuf files can define IDs for each feature as a virtual attribute that has no name, and thus cannot be addressed using `source_name`. You can load them using the `is_source_feature_ids` field instead of `source_name`. You will still need to set the `is_feature_id` field to specify that is it part of the feature ID inside Horizon.

<gallery-card demo="csvData"></gallery-card>

## Example

Let's consider an example where we have two data sources. One contains data about cities in different countries, as well as their footprint given by a polygon, the other contains some metadata about these cities.

Features are cities. So feature IDs are the tuple formed by the country code and the zip code.

### Source #1

| Country code | Zip code | Name |
|--------------|---------|------|
| FRA          | 35000   | Rennes |
| FRA          | 35830   | Betton |
| UK           | BN1     | Brighton |
| US           | 90001   | Los Angeles |

```
has_geometry = true

Country Code: is_feature_id = true
Zip Code:     is_feature_id = true
Name:         is_feature_id = false
```

### Source #2

| Country code | Zip code | Is weather nice |
|--------------|---------|------|
| FRA          | 35000   | No |
| UK           | BN1     | No |
| US           | 90001   | Yes |

```
has_geometry = false

Country Code:       is_feature_id = true
Zip Code:           is_feature_id = true
Is weather nice:    is_feature_id = false
```

### Resulting dataset

| Feature ID | Country code | Zip code | Name | Is weather nice |
|-------------|--------------|---------|------|-----------|
| (FRA, 35000) | FRA          | 35000   | Rennes | No |
| (FRA, 35830) | FRA          | 35830   | Betton |  |
| (UK, BN1) | UK           | BN1     | Brighton | No |
| (US, 90001) | US           | 90001   | Los Angeles | Yes |
