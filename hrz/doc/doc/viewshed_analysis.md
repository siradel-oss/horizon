+++
title = "Viewshed"
+++

# Viewshed

Viewshed analysis is used to visualise visibility from a specified point of view with a perspective projection.
Think of it as a way to visualise what would be visible from a camera.
It is defined in the scene view settings by [ViewshedSettings]($proto) by:

- A position for the viewpoint.
- An orientation (using bearing and tilt).
- What colors are applied to the visible and hidden parts of the scene.
- The horizontal field of view and aspect ratio which define the shape of the view cone.
- The max distance which is how far the view cone goes, and the start factor which gives how close to the maximum distance we start the view cone: 0 is at the viewpoint and 1 is at the max distance. For instance with a maximum distance of 1000m and a start factor of 0.25, the view cone would see objects starting 250m away from the camera, and up to 1000m away.

Additionally a wireframe can be drawn to help with visualising the viewshed volume. It respects the `start` and `max_distance` property values by defaults, and therefore outlines a truncated pyramid shape (because the `start` value must be greater than 0). However the `draw_wireframe_from_position` property can be set to make the lines reach the viewpoint position.

{{< gallery-card "viewshed" >}}
