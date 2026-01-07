#pragma once

// Should match "HrzProtocol.PolygonPatternSizeUnit" enum variants
#define POLYGON_PATTERN_SIZE_IN_METERS 0u
#define POLYGON_PATTERN_SIZE_IN_PIXELS 1u
#define POLYGON_PATTERN_SIZE_RELATIVE_TO_SPRITE_IN_METERS 2u
#define POLYGON_PATTERN_SIZE_RELATIVE_TO_SPRITE_IN_PIXELS 3u

// Should match "HrzProtocol.PolygonPatternTilingType" enum variants
#define POLYGON_PATTERN_FAVOR_GRID 0u
#define POLYGON_PATTERN_FAVOR_SIZE 1u

// Should match "HrzProtocol.PolygonPatternReferenceLatitudeType" enum variants
#define POLYGON_PATTERN_FIXED_REFERENCE_LATITUDE 0u
#define POLYGON_PATTERN_DYNAMIC_REFERENCE_LATITUDE 1u

