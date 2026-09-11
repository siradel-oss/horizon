+++
title = "Loading priorities"
+++

# Loading priorities

All layers that download data have a `loading_priority` parameter, whose default value is `0`. This parameter determines the order in which the requests for downloading the data of the layer are emitted, in relation to the requests of other layers.

By default all the downloads are coordinated in a fashion that balances the needs of all layers, and prevents one layer from monopolising all the bandwidth. (The requests of one given layer are ordered so as to load the most visually important data first.)

Setting non-zero values to the loading priority parameter allows creating a strict ordering of requests. All the required download requests (for the current viewpoint) of a highly prioritised layer must be emitted before loading the data of the lower-priority layers can start.

A positive value allows loading a layer as fast as possible, a negative value makes the layer load only when the rest is done. In the middle, all the layers with a loading priority of `0` are handled normally.

Be careful when setting positive values, as a particularly heavy or slow-to-download layer can block the other layers.

Values must be between `-128` and `127` (inclusive).
