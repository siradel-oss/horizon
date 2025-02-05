# Horizon testing kit

This package contains tools to run tests inside of Horizon.

## `viewer`

This tool opens a Horizon scene dump (which it migrates if necessary) or a Mapbox style (which Horizon translates to a scene), takes a screenshot of the scene once loaded, and finally writes it to disk as a PNG file. The size of the screenshot can be configured.

Running the executable without arguments will document the usage of this tool, including its many different possible return codes.

Example:

```
viewer --input my_scene.hrz_scene.pbf --output capture.png --width 512 --height 512 --timeout 30
```

## `comparator`

This tool compares two PNG images (of the same size) and writes a PNG image showing the pixels that are different. The number of pixels that are different is written to the standard output stream. This comparison method can be configured:

* It is possible to filter out small errors (typically isolated pixels or very thin lines),
* The threshold at which a pixel difference is considered an error can be changed.

Running the executable without arguments will document the usage of this tool.

Example:

```
comparator capture.png reference.png difference.png --erode --threshold 0.05
```

## `migrate_dump`

This tool is used to migrate a scene dump to the latest version of the scene model. The resulting scene should be isofunctional to the source one. Any incompatibility will be described in the changelog.

```
migrate_dump my_dump_old.hrz_scene.pbf my_dump_new.hrz_scene.pbf
```

