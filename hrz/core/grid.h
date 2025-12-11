#pragma once

#include "hrz/core/camera/types.h"
#include "hrz/fnd/class.h"
#include "hrz/protocol/layer/grid.pb.h"
#include "hrz/protocol/size_unit.pb.h"

#include <lin_maths.h>

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

grid::GridParams from_proto(const hrz_proto::Grid& grid);
} // namespace hrz
