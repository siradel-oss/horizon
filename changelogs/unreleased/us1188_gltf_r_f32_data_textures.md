# Changed

* **3D models**
    * The [`SIRADEL_data_texture`](SIRADEL_data_texture.html) glTF extension has been extended to detail how data bits from colour images are reinterpreted as scalar values. (What [[ImageFormat]] is used for in raster layers.) Entries of the `textures` array in glTF JSONs for data textures must now have a `SIRADEL_data_texture` extension object with a value for the `dataInterpretation` property. The only accepted value is `rgba8BitsToFloat`, which corresponds to the `R_F32` image format.
    * The [`SIRADEL_data_texture`](SIRADEL_data_texture.html) glTF extension now allows defining a sampler for glTF data textures. Linear filtering and mipmaps must not be used. The default sampler (when no ID is given in the glTF JSON) is unchanged.

# Deprecated

* **3D models**
    * Using the `SIRADEL_data_texture` glTF extension without specifying a value for the `dataInterpretation` property is deprecated. Encoding scalar values in glTF data textures in the `R_F32_SILICIUM` format is deprecated.

# Upgrade notes

* **3D models**
    * Models conforming to the previous version of the `SIRADEL_data_texture` glTF extension, including its non-documented expectations, remain usable. Users are strongly advised to generate new models according to the updated version of the extension, and to convert existing models if possible.
