+++
title = "Visibility constraints"
+++

# Visibility constraints

All layers with visual content can be made visible or hidden based on constraints. Horizon currently supports two types of [visibility constraints](reference/HrzProtocol.LayerVisibilityConstraint): an altitude constraint and a bounds constraint.

With the altitude constraint the layer can be configured to be visible (or hidden) when the camera's altitude relative to the ellipsoid is below or above a given threshold.

The bounds constraint makes it so that the layer is visible (or hidden) when the camera is inside (or outside) the given bounds.

{{< gallery-card "visibilityConstraints" >}}
