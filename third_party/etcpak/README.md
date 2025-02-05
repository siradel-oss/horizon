# etcpak

ETC and DXT compressor.

Upstream project page: [https://github.com/wolfpld/etcpak](https://github.com/wolfpld/etcpak)

The original project is not packaged so that the compression function can easily be integrated into another project. The official stance of the developper is to extract the relevant functions from the etcpak code base:

> Typically I’d expect people to just extract the Compress* functions into their own code, as these functions do all the heavy magic here, i.e. they accept an image buffer with a number of blocks and output the compressed data.

([https://github.com/wolfpld/etcpak/pull/14#issuecomment-819435201](https://github.com/wolfpld/etcpak/pull/14#issuecomment-819435201))

Fortunately developpers of the [Godot](https://github.com/godotengine/godot) game engine have already isolated the relevant functions and copied them into the code base [here](https://github.com/godotengine/godot/tree/de75085c7f2e466441c477c2b2429e23d5e0881e/thirdparty/etcpak).

This repository contains the files of the Godot copy of etcpak, to ease packaging the code into a library, as depending on the whole Godot code base is inconvenient.

Some modifications have been made to the code since its retrieval from the Godot repository:

* `ProcessRGB.{hpp,cpp}` files have been renamed to `ProcessEtc.{hpp,cpp}`,
* All the code has been put in the `etcpak` namespace.
* Warnings have been fixed.
