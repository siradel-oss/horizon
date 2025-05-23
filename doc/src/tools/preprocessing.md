Preprocessing data
==========================

### Mapbox vector tiles from GeoJSON

*The easiest way to use tippecanoe on Windows is through WSL.*

Guess the max level:

```
tippecanoe -zg -e [dest dir] -pC --drop-densest-as-needed --extend-zooms-if-still-dropping [input.geojson]
```

Specify the max level:

```
tippecanoe -z[max level] -e [dest dir] -pC --drop-densest-as-needed --extend-zooms-if-still-dropping [input.geojson]
```

### Extracting OSM vector data

We have a big pmtiles file with all the OSM data, but it's too big!

In order to extract it into smaller datasets, we have a tool, https://redacted.localhost/horizon/tools/extract-mvt, composed of 3 scripts.

- dl_osm.py, downloads a subset of the tiles pyramid.
- cull.py, filters the tiles to keep only some layers, features, and attributes.
- to_pmtiles.py, repackages the result into a pmtiles file.

Note that none of this is production-ready, generic, or configurable. So good luck using it. This paragraph might even be obsolete.

### DTM from tiff raster

Use `gdal2tiles` from https://redacted.localhost/tpetillon/gdal2tiles and `texture_converter` from https://redacted.localhost/siliciumserver/TextureConverter.

```
gdal2tiles.py -f tiff -z [minzoom]-[maxzoom] -r cubic [input file] [output folder]
texture_converter -f tiff -t float -i [input folder] -o [output folder] -n [nodata value]
```

For the tiling step, the `-z` parameter can be omitted to let the tool decide on the best minimum and maximum levels to generate. Also the `maxzoom` can be omitted alone. For example one may ask the tool to generate tiles from level 0 to whatever max level it sees fit with `-z 0-`.

### Using the raster tiler

[Documentation](http://redacted.localhost/api-docs)

#### Upload file

```
scp <file> connect@redacted.localhost:/shared_data/depot/<file>
```

#### Submit computation

```
POST http://redacted.localhost/rasters-tiler/compute
```

```json
{
    "inputFiles": [
        "/shared_data/depot/<file>"
    ],
    "tmsProfile": "global-mercator",
    "type": "single_band_real"
}
```

#### Get status

```
GET http://redacted.localhost/rasters-tiler/{id}/status
```

#### Download result

```
scp -r connect@redacted.localhost:/shared_data/rasters-tiler/results/{id} result
```

#### Delete computation

```
DELETE http://redacted.localhost/rasters-tiler/{id}
```
