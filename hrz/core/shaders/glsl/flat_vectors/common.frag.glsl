#pragma once

#ifdef FLAT_VISUAL
uvec3 build_feature_reference()
{
    return hrz_tile.feature_reference | uvec3(0, v_feature_id);
}
#endif

#ifdef FLAT_PICKING
uvec2 build_object_reference()
{
    return hrz_tile.object_reference | uvec2(0, v_feature_index);
}
#endif
