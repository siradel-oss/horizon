# Added

* **Sun settings**
    * The Sun position in the sky can now be set to a fixed position with regard to the planet in [SunSettings]($proto), either through a date in an idealised year (no leap years) at the prime meridian ([SolarDate]($proto) with `at_prime_meridian` set), an actual date in a time zone ([CalendarDate]($proto)), a Unix timestamp, or an arbitrary angle, measured from anywhere on Earth ([AngularDirection]($proto) with `geographic_position` set).
    * The Sun position can also be fixed with respect to the camera, through the `FRAME_CAMERA` camera frame of [AngularDirection]($proto).
    * The eccentricity of the Earth’s orbit around the Sun is now taken into account when computing the Sun’s position in the sky.

# Removed

* The `SunDirectionMode` enum has been deleted, instead the Sun direction mode is selected through the `direction` `oneof` of [SunSettings]($proto).

# Upgrade notes

* **Sun settings**
    * The mode must now be selected by setting a value to the `direction` `oneof` of [SunSettings]($proto) instead of using the `SunDirectionMode` enum.
    * `SUN_DIRECTION_RELATIVE_TO_DATE` has been replaced with [SolarDate]($proto), with `at_prime_meridian` set to `false`.
    * `SUN_DIRECTION_RELATIVE_TO_CARDINAL_FRAME` has been replaced with [AngularDirection]($proto), with `camera_frame` set to `FRAME_ENU`.
    * `SUN_DIRECTION_RELATIVE_TO_TANGENTIAL_FRAME` has been replaced with [AngularDirection]($proto), with `camera_frame` set to `FRAME_CAMERA_HEADING`.
    * `azimuth` is now measured clockwise.
