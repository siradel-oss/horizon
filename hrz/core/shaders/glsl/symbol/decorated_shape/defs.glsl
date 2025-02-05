#pragma once

// This must match the enum DecoratedShapeType in hrz_scene_model.proto
#define DECORATED_SHAPE_CIRCLE          0u
#define DECORATED_SHAPE_REGULAR_POLYGON 1u
#define DECORATED_SHAPE_RECTANGLE       2u
#define DECORATED_SHAPE_CAPSULE         3u

layout(std140) uniform DecoratedShape
{
    uint z_index;
    uint shape_type;

    uint regular_polygon_sides;
    float regular_polygon_star;
} hrz_decorated_shape;

#define DECORATED_BOX_PADDING 1.1
