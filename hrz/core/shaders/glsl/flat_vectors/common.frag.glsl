#pragma once

#ifdef FLAT_VISUAL
uvec3 build_feature_picking_id()
{
    return uvec3(hrz_tile.layer_picking_id, v_feature_id);
}
#endif

#ifdef FLAT_PICKING
uvec2 build_picking_id()
{
    uvec2 picking_id = hrz_tile.picking_id;
    picking_id.g += v_feature_index;
    return picking_id;
}
#endif
