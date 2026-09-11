+++
title = "Blend modes"
+++

# Blend modes

For images in [`symbol`](symbols.html) and [`3D model`](instanced_models.html) representations, as well as [single model](single_model.html) and [3D Tiles](3d_tiles.html) layers, it is possible to select a particular [blend mode](reference/HrzProtocol.BlendMode), which determines how the colour property of the sprite (or model) is combined with its original colour.

The four blend modes that can be selected are the following:

* `Normal` replaces the original colour with the colour property
* `Multiply` darkens the overall image based on the colour property (the impact is stronger on brighter pixels)
* `Screen` brightens the overall image based on the colour property (the impact is stronger on darker pixels)
* `Overlay` applies `Multiply` on dark pixels and `Screen` on light pixels

More information on those blend modes [can be found here](https://en.wikipedia.org/wiki/Blend_modes).

Unlike traditional image editing software like Adobe Photoshop, the alpha value of the colour property controls the opacity of the feature, but not the ratio of the two colours used for the blending. Instead, this ratio is controlled by the dedicated per-layer (or per-representation) property `feature_color_blend_strength` (from 0 which is the unmodified feature colour, to 1 which is the blended colour).

{{< tabbed-figure "Normal" "Multiply" "Screen" "Overlay" >}}

![](blend_normal.png "Normal mode")

![](blend_multiply.png "Multiply mode")

![](blend_screen.png "Screen mode")

![](blend_overlay.png "Overlay mode")

{{< /tabbed-figure >}}
