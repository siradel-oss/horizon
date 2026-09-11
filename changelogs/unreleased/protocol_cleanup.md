# Changed

* In [BoundsView]($proto), the min and max altitude fields are now wrapped in the `bounds` fields which is now a geographic volume.
* Camera translation movements encoded by [CameraTranslate]($proto) have had their default factor doubled.

# Upgrade notes

* In [BoundsView]($proto), move the `min_altitude` and `max_altitude` fields to the `min_height` and `max_height` fields of the `bounds` field.
* In [CameraTranslate]($proto), halve the `angle_factor_x` and `angle_factor_y` values to get the same movement as before.
