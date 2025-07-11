# Added

* **Vector data**
    * Rules for joining multiple data sources in a single vector data source have been clarified and combinations have been expanded:
        * The first source in the array is now known as the primary source. Following sources are secondary sources.
        * The geometry source can be a secondary source.
        * It is possible to join sources that are requested by tile coordinates.
            * The simpler way is by taking values in the order they are in, for both primary and secondary sources.
            * Features can also be matched by ID, and the values in the tiles be automatically reordered when required. To enable this feature ID attributes must be declared in both primary and secondary sources.
    * Geometries of type `UNKNOWN` are recognised when reading Mapbox vector tiles. Their geometry data, if it exists, is ignored. They can serve as support for attribute-only entries in MVT files.
