# Added

* **Ambient settings**
    * More customisation points have been added to the static sky. The atmosphere and space colours can both be set, as well as the distance from the horizon at which the transition between the two occurs. This distance can be expressed in either metres or pixels.

# Changed

* **Ambient settings**
    * The property `static_color` of [[SkySettings]] has been renamed `static_atmosphere_color`.
    * Instead of the whole sky transitioning from black to the static sky colour as the camera zooms in, the static sky now shows the atmosphere colour around the planet when viewed from afar.

# Upgrade notes

* **Ambient settings**
    * Set the static sky colour with the property `static_atmosphere_color` of [[SkySettings]].
    * Set colour transition distances and unit for the static sky on [[SkySettings]]. Values of 15,000 for `static_color_transition_start_distance`, 60,000 for `static_color_transition_end_distance`, and `STATIC_SKY_COLOR_TRANSITION_UNIT_METERS` for `static_color_transition_distance_unit` allow approximating the simulated sky.
