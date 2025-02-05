#pragma once

#include "camera/hrz_core_camera_types.h"
#include "lin_maths.h"

#include <hrz_common_proto_maths.h>
#include <hrz_fnd_class.h>
#include <hrz_protocol_all.h>

namespace hrz
{
struct Render;

namespace grid
{
struct GridParams
{
    float extent;
    float cell_size;
    lm::vec4 color;
    uint32_t scene_views_bitset;
    hrz_proto::UiSizeUnit extent_unit;
};

class GridRenderable;

class Grid
{
    struct GridInstance
    {
        GridParams params;
    };

    std::unique_ptr<GridRenderable> _renderable;

public:
    Grid();
    HRZ_DELETE_COPY_MOVE(Grid);
    ~Grid();

    void init_rendering(Render*);
    void deinit_rendering(Render*);
    bool is_rendering_initialized() const;

    /**
     * Adds a grid instance with the specified `params` to the drawing queue for this frame.
     * The drawing queue is cleared after every call to `draw()`.
     */
    void schedule_draw(
        const GridParams& params,
        const lm::dvec3& ecef_pos,
        const lm::dvec3& ecef_cc_pos,
        const lm::vec3& axis_x,
        const lm::vec3& axis_y,
        const lm::vec2& offset,
        const CameraViewInfo& view_info,
        uint32_t scene_views);

    void draw(Render*);
};
} // namespace grid

inline grid::GridParams from_proto(const hrz_proto::Grid& grid)
{
    grid::GridParams grid_params;
    grid_params.extent = grid.extent();
    grid_params.cell_size = grid.cell_size();
    grid_params.color = hrz::to_lm(grid.color());
    grid_params.scene_views_bitset = grid.scene_views().bits();
    grid_params.extent_unit = grid.extent_unit();
    return grid_params;
}
} // namespace hrz
