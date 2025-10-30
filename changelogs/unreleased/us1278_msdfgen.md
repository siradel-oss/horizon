# Changed

* **Symbols**
    * Improved text legibility at small sizes.
    * Text outline widths are now automatically capped at the maximum value supported by the renderer (6/28 ≈ 0.21 ems).

# Fixed

* **Symbols**
    * Character weight now matches the font’s definition.
    * Fixed a crash that could happen when generating text elements.

# Upgrade notes

* Because Horizon used to exaggerate character weight in text elements, some symbols may need to be adjusted to use a font with a larger weight in order to maintain the previous appearance.
