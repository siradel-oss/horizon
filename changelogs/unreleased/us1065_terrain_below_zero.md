# Added

* Elevations below zero are now supported for DTM layers. They can be overground location below sea level, or undersea terrain (i.e. bathymetry).

# Changed

* The camera’s minimum height above ground is immediately respected when switching to the orbit manipulator after an animation has finished.

# Fixed

* DTM layers are now refined at the camera’s location, to avoid issues such as the camera getting stuck below ground, or the surrounding terrain not loading if the camera does not point downwards.
* Fixed camera angular viewpoints when the camera is below sea level.
