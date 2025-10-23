# Added

* Static sky colours, as well as underground colour, now support transparency through their alpha channel. This can be used to show the web page containing the Horizon instance around the planet.

# Changed

* Areas not covered by any scene view viewport (typically when using multiview) now show the web page below the canvas, instead of solid black.

# Upgrade notes

* Set the canvas as child to a `div` whose `background-color` is black to preserve the previous behaviour when there are areas not covered by any scene view viewport. According to the integrating client’s requirements, any other colour can be used, or none if showing the web page below is desired.
