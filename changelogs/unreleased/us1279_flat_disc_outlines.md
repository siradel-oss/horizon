# Added

* Flat overlay point outline colour and width can now be set per instance.

# Upgrade notes

* In order to maintain the appearance of flat overlay points, the representations ([[FlatOverlayPointVectorRepr]]) must be updated as such:
    - The value of the former `outline_color` property must be set on the new `outline_color.default_value`.
    - The value of the former `outline_width` property must be set on the new `outline_width.default_value`.
    - The value of `radius_unit` property must be copied to `outline_width_unit`.
