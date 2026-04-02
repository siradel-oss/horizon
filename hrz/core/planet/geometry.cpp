#include "hrz/core/planet/geometry.h"

#include "hrz/common/blob_allocator.h"
#include "hrz/common/blob_image.h"
#include "hrz/common/geo.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/core/buffer.h"
#include "hrz/core/clock.h"
#include "hrz/core/debug_draw.h"
#include "hrz/core/global_flags.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/jobs/process_feedback.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/defs.h"
#include "hrz/core/render/double_buffered_uniform_buffer.h"
#include "hrz/core/render/profiling.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/render/timed_render_pass.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/core/shadows.h"
#include "hrz/core/sky.h"
#include "hrz/core/vector/flat_overlay.h"
#include "hrz/core/viewsheds.h"
#include "hrz/core/vtex/clipmap_params.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/mem.h"
#include "hrz/fnd/static_vector.h"
#include "hrz/protocol/path_builder/scene/view_settings.h"

#include <float.h>

#include <array>
#include <cmath>
#include <optional>

#define MAX_IMAGERY_GROUP_COUNT HRZ_S_MAX_IMAGERY_GROUP_COUNT

namespace
{

constexpr uint32_t SubdivisionCount = 5;
constexpr uint32_t VerticesPerSide = (1 << SubdivisionCount) + 1;
constexpr uint32_t RootPatchCount = 20;
constexpr uint32_t MaxPatchCount = 512;
constexpr uint32_t MaxGeometryCacheCount = MaxPatchCount + MaxPatchCount / 4;
constexpr uint32_t MaxPatchDepth = 24;
constexpr uint32_t FeedbackDelayMs = 500;
constexpr uint32_t FeedbackSubsample = HRZ_S_PLANET_FEEDBACK_SUBSAMPLE;

// Bins are groups of patches
constexpr uint32_t MaxBinCount = 24;
constexpr uint32_t BinSize = HRZ_S_PLANET_BIN_SIZE;

enum
{
    UboPlanetParams = hrz::vector_flat_overlay::UboVectorOverlayCameras + 1,
    UboRenderBinData,
    UboPrecomputeBinData,
};

enum
{
    SamplerTessellation =
        hrz::vector_flat_overlay::SamplerOverlayStart + HRZ_S_MAX_OVERLAY_CASCADES,
    SamplerHeightLut,
    SamplerNormalLut,
    SamplerImageryStart,
};

constexpr int sampler_imagery_atlas(int unit)
{
    return SamplerImageryStart + unit * 2;
}

constexpr int sampler_imagery_indirection(int unit)
{
    return SamplerImageryStart + unit * 2 + 1;
}

static_assert(MAX_IMAGERY_GROUP_COUNT <= 8, "Oops, I hardcoded a thing :)");

static constexpr const char* sampler_imagery_atlas_name[]{
    "hrz_imagery_atlas[0]", "hrz_imagery_atlas[1]", "hrz_imagery_atlas[2]", "hrz_imagery_atlas[3]",
    "hrz_imagery_atlas[4]", "hrz_imagery_atlas[5]", "hrz_imagery_atlas[6]", "hrz_imagery_atlas[7]",
};

static constexpr const char* sampler_imagery_indirection_name[]{
    "hrz_imagery_indirection[0]", "hrz_imagery_indirection[1]", "hrz_imagery_indirection[2]",
    "hrz_imagery_indirection[3]", "hrz_imagery_indirection[4]", "hrz_imagery_indirection[5]",
    "hrz_imagery_indirection[6]", "hrz_imagery_indirection[7]",
};

constexpr int vertices_count_after_subdivision(int subdivide_count)
{
    return ((1 << subdivide_count) + 1) * ((1 << subdivide_count) + 2) / 2;
}

struct PatchGeometryInfo
{
    uint32_t index_count;
    uint32_t first_index;
};

// 2               2           2
//                            13  14
//                 4   5       4  12   5
//                             8   9  10  11
// 0       1       0   3   1   0   6   3   7  1

// Generates the necessary geometry for tessellating patches encoded in images.
// 'elements' contains the elements arrays for each patch type.
// 'subdivision' contains the subdivision indices used during tessellation.
// Each slot encodes the two indices of the points necessary for tessellation.
// Each slot correspond to a vertex.
// The levels are encoded consecutively.
// For instance the above example is a tessellation with subdivide_count = 2 and
// would give in the subdivision vector something like: (first 3 slots are ignored)
//
//    - - - 0 0 1 0 3 4 4 5 5 4 2 2
//    - - - 1 2 2 3 1 0 3 3 1 5 4 5
//   |-----|-----|-----------------|
//      0     1           2
std::array<PatchGeometryInfo, 8> generate_subdivision_indices(
    std::vector<uint16_t>& elements,
    std::vector<std::pair<uint16_t, uint16_t>>* subdivision)
{
    static constexpr int Size = (1 << SubdivisionCount) + 1;
    std::unique_ptr<uint16_t[]> indices(new uint16_t[Size * Size]);

    std::fill_n(indices.get(), Size * Size, 65535);
    indices[0] = 0;
    indices[Size - 1] = 1;
    indices[(Size - 1) * Size] = 2;

    static constexpr int VerticesCount = vertices_count_after_subdivision(SubdivisionCount);
    int next_index = 3;

    subdivision->resize(VerticesCount);

    uint32_t elements_count[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t max_elements_per_patch = (Size - 1) * (Size - 1) * 3;
    elements.resize(max_elements_per_patch * 8);

    auto write_elements_3 = [&](int patch, uint16_t a, uint16_t b, uint16_t c)
    {
        if (a == b || b == c || a == c) return;
        uint32_t index = patch * max_elements_per_patch + elements_count[patch];
        elements[index++] = a;
        elements[index++] = b;
        elements[index] = c;
        elements_count[patch] += 3;
    };

    int gap = Size - 1;
    while (gap >= 2)
    {
        int inner_gap = gap / 2;
        bool odd_row = true;

        for (int i = 0; i < Size - 1; i += inner_gap)
        {
            int column = 0;

            int j = (odd_row) ? inner_gap : 0;
            for (; j < Size - i; j += inner_gap)
            {
                if (indices[i * Size + j] == 65535)
                {
                    indices[i * Size + j] = next_index;

                    uint16_t a, b;

                    if (odd_row)
                    {
                        a = indices[i * Size + j - inner_gap];
                        b = indices[i * Size + j + inner_gap];
                    }
                    else if (column % 2 == 0)
                    {
                        a = indices[(i - inner_gap) * Size + j];
                        b = indices[(i + inner_gap) * Size + j];
                    }
                    else
                    {
                        a = indices[(i + inner_gap) * Size + j - inner_gap];
                        b = indices[(i - inner_gap) * Size + j + inner_gap];
                    }

                    (*subdivision)[next_index] = std::make_pair(a, b);
                    next_index += 1;
                }

                column += 1;
            }

            odd_row = !odd_row;
        }

        gap = gap / 2;
    }

    // Interior part
    //      C ----- D           .
    //       \    /  \          .
    //        \  /    \         .
    //         A ----- B        .
    for (int i = 1; i < Size - 2; ++i)
    {
        for (int j = 1; j < Size - i - 2; ++j)
        {
            int a = indices[i * Size + j];
            int b = indices[i * Size + j + 1];
            int d = indices[(i + 1) * Size + j];

            for (int patch = 0; patch < 8; ++patch)
            {
                write_elements_3(patch, a, b, d);

                if (j != 1)
                {
                    int c = indices[(i + 1) * Size + j - 1];
                    write_elements_3(patch, a, d, c);
                }
            }
        }
    }

    enum Corner
    {
        Corner_BottomLeft,
        Corner_Top,
        Corner_BottomRight,
        Corner_Count,
    };

    enum CornerKind
    {
        CornerKind_LowLow,
        CornerKind_LowHigh,
        CornerKind_HighLow,
        CornerKind_HighHigh,
        CornerKind_Count,
    };

    enum Edge
    {
        Edge_Left,
        Edge_Right,
        Edge_Bottom,
        Edge_Count,
    };

    enum EdgeKind
    {
        EdgeKind_Low,
        EdgeKind_High,
        EdgeKind_Count,
    };

    uint16_t corner_indices[Corner_Count][7];
    int16_t edge_offsets[Edge_Count][6];
    uint16_t corner_kind_triangles[CornerKind_Count][15];
    uint16_t edge_kind_triangles[EdgeKind_Count][12];

    //  |                           .
    //  f---g                       .
    //  | \ |                       .
    //  c---e                       .
    //  | \ | \                     .
    //  a---b---d---                .
    corner_indices[Corner_BottomLeft][0] = indices[0];
    corner_indices[Corner_BottomLeft][1] = indices[1];
    corner_indices[Corner_BottomLeft][2] = indices[Size];
    corner_indices[Corner_BottomLeft][3] = indices[2];
    corner_indices[Corner_BottomLeft][4] = indices[Size + 1];
    corner_indices[Corner_BottomLeft][5] = indices[Size * 2];
    corner_indices[Corner_BottomLeft][6] = indices[Size * 2 + 1];

    //  a                           .
    //  | \                         .
    //  b---c                       .
    //  | \ | \                     .
    //  d---e---f                   .
    //  |     \ |                   .
    //          g                   .
    corner_indices[Corner_Top][0] = indices[Size * (Size - 1)];
    corner_indices[Corner_Top][1] = indices[Size * (Size - 2)];
    corner_indices[Corner_Top][2] = indices[Size * (Size - 2) + 1];
    corner_indices[Corner_Top][3] = indices[Size * (Size - 3)];
    corner_indices[Corner_Top][4] = indices[Size * (Size - 3) + 1];
    corner_indices[Corner_Top][5] = indices[Size * (Size - 3) + 2];
    corner_indices[Corner_Top][6] = indices[Size * (Size - 4) + 2];

    //   \                          .
    //     d                        .
    //     | \                      .
    // g---e---b                    .
    //   \ | \ | \                  .
    //  ---f---c---a                .
    corner_indices[Corner_BottomRight][0] = indices[Size - 1];
    corner_indices[Corner_BottomRight][1] = indices[Size + Size - 2];
    corner_indices[Corner_BottomRight][2] = indices[Size - 2];
    corner_indices[Corner_BottomRight][3] = indices[Size * 2 + Size - 3];
    corner_indices[Corner_BottomRight][4] = indices[Size + Size - 3];
    corner_indices[Corner_BottomRight][5] = indices[Size - 3];
    corner_indices[Corner_BottomRight][6] = indices[Size + Size - 4];

    //      a                       .
    //     / \                      .
    //    b---c                     .
    //   / \ / \                    .
    //  d---e---f                   .
    //       \ /                    .
    //        g                     .
    // a b c
    corner_kind_triangles[CornerKind_HighHigh][0] = 0;
    corner_kind_triangles[CornerKind_HighHigh][1] = 1;
    corner_kind_triangles[CornerKind_HighHigh][2] = 2;
    // b d e
    corner_kind_triangles[CornerKind_HighHigh][3] = 1;
    corner_kind_triangles[CornerKind_HighHigh][4] = 3;
    corner_kind_triangles[CornerKind_HighHigh][5] = 4;
    // b e c
    corner_kind_triangles[CornerKind_HighHigh][6] = 1;
    corner_kind_triangles[CornerKind_HighHigh][7] = 4;
    corner_kind_triangles[CornerKind_HighHigh][8] = 2;
    // c e f
    corner_kind_triangles[CornerKind_HighHigh][9] = 2;
    corner_kind_triangles[CornerKind_HighHigh][10] = 4;
    corner_kind_triangles[CornerKind_HighHigh][11] = 5;
    // e g f
    corner_kind_triangles[CornerKind_HighHigh][12] = 4;
    corner_kind_triangles[CornerKind_HighHigh][13] = 6;
    corner_kind_triangles[CornerKind_HighHigh][14] = 5;

    //      a                       .
    //     /|\                      .
    //    b | \                     .
    //   / \|  \                    .
    //  d---e---f                   .
    //       \ /                    .
    //        g                     .
    // a b e
    corner_kind_triangles[CornerKind_HighLow][0] = 0;
    corner_kind_triangles[CornerKind_HighLow][1] = 1;
    corner_kind_triangles[CornerKind_HighLow][2] = 4;
    // b d e
    corner_kind_triangles[CornerKind_HighLow][3] = 1;
    corner_kind_triangles[CornerKind_HighLow][4] = 3;
    corner_kind_triangles[CornerKind_HighLow][5] = 4;
    // a e f
    corner_kind_triangles[CornerKind_HighLow][6] = 0;
    corner_kind_triangles[CornerKind_HighLow][7] = 4;
    corner_kind_triangles[CornerKind_HighLow][8] = 5;
    // e g f
    corner_kind_triangles[CornerKind_HighLow][9] = 4;
    corner_kind_triangles[CornerKind_HighLow][10] = 6;
    corner_kind_triangles[CornerKind_HighLow][11] = 5;
    // filler
    corner_kind_triangles[CornerKind_HighLow][12] = 5;
    corner_kind_triangles[CornerKind_HighLow][13] = 5;
    corner_kind_triangles[CornerKind_HighLow][14] = 5;

    //      a                       .
    //     /|\                      .
    //    / | c                     .
    //   /  |/ \                    .
    //  d---e---f                   .
    //       \ /                    .
    //        g                     .
    // a d e
    corner_kind_triangles[CornerKind_LowHigh][0] = 0;
    corner_kind_triangles[CornerKind_LowHigh][1] = 3;
    corner_kind_triangles[CornerKind_LowHigh][2] = 4;
    // a e c
    corner_kind_triangles[CornerKind_LowHigh][3] = 0;
    corner_kind_triangles[CornerKind_LowHigh][4] = 4;
    corner_kind_triangles[CornerKind_LowHigh][5] = 2;
    // c e f
    corner_kind_triangles[CornerKind_LowHigh][6] = 2;
    corner_kind_triangles[CornerKind_LowHigh][7] = 4;
    corner_kind_triangles[CornerKind_LowHigh][8] = 5;
    // e g f
    corner_kind_triangles[CornerKind_LowHigh][9] = 4;
    corner_kind_triangles[CornerKind_LowHigh][10] = 6;
    corner_kind_triangles[CornerKind_LowHigh][11] = 5;
    // filler
    corner_kind_triangles[CornerKind_LowHigh][12] = 5;
    corner_kind_triangles[CornerKind_LowHigh][13] = 5;
    corner_kind_triangles[CornerKind_LowHigh][14] = 5;

    //      a                       .
    //     /|\                      .
    //    / | \                     .
    //   /  |  \                    .
    //  d---e---f                   .
    //       \ /                    .
    //        g                     .
    // a d e
    corner_kind_triangles[CornerKind_LowLow][0] = 0;
    corner_kind_triangles[CornerKind_LowLow][1] = 3;
    corner_kind_triangles[CornerKind_LowLow][2] = 4;
    // a e f
    corner_kind_triangles[CornerKind_LowLow][3] = 0;
    corner_kind_triangles[CornerKind_LowLow][4] = 4;
    corner_kind_triangles[CornerKind_LowLow][5] = 5;
    // e g f
    corner_kind_triangles[CornerKind_LowLow][6] = 4;
    corner_kind_triangles[CornerKind_LowLow][7] = 6;
    corner_kind_triangles[CornerKind_LowLow][8] = 5;
    // filler
    corner_kind_triangles[CornerKind_LowLow][9] = 5;
    corner_kind_triangles[CornerKind_LowLow][10] = 5;
    corner_kind_triangles[CornerKind_LowLow][11] = 5;
    // filler
    corner_kind_triangles[CornerKind_LowLow][12] = 5;
    corner_kind_triangles[CornerKind_LowLow][13] = 5;
    corner_kind_triangles[CornerKind_LowLow][14] = 5;

    //  |   |                       .
    //  e---f--                     .
    //  |   |                       .
    //  c---d--                     .
    //  |   |                       .
    //  a---b--                     .
    //  |   |                       .
    edge_offsets[Edge_Left][0] = 0;
    edge_offsets[Edge_Left][1] = 1;
    edge_offsets[Edge_Left][2] = Size;
    edge_offsets[Edge_Left][3] = Size + 1;
    edge_offsets[Edge_Left][4] = Size * 2;
    edge_offsets[Edge_Left][5] = Size * 2 + 1;

    //   \                          .
    //  --a                         .
    //    | \                       .
    //  --b   c                     .
    //      \ | \                   .
    //      --d   e                 .
    //          \ |\                .
    //          --f                 .
    //             \                .
    //                              .
    edge_offsets[Edge_Right][0] = 0;
    edge_offsets[Edge_Right][1] = -Size;
    edge_offsets[Edge_Right][2] = -Size + 1;
    edge_offsets[Edge_Right][3] = -Size * 2 + 1;
    edge_offsets[Edge_Right][4] = -Size * 2 + 2;
    edge_offsets[Edge_Right][5] = -Size * 3 + 2;

    //  \   \   \                   .
    // --f---d---b--                .
    //     \   \   \                .
    //     --e---c---a--            .
    edge_offsets[Edge_Bottom][0] = 0;
    edge_offsets[Edge_Bottom][1] = Size - 1;
    edge_offsets[Edge_Bottom][2] = -1;
    edge_offsets[Edge_Bottom][3] = Size - 2;
    edge_offsets[Edge_Bottom][4] = -2;
    edge_offsets[Edge_Bottom][5] = Size - 3;

    //     |   |                    .
    //     e---f--                  .
    //     | \ |                    .
    //     c---d--                  .
    //     | \ |                    .
    //     a---b--                  .
    //     |   |                    .
    // a b c
    edge_kind_triangles[EdgeKind_High][0] = 0;
    edge_kind_triangles[EdgeKind_High][1] = 1;
    edge_kind_triangles[EdgeKind_High][2] = 2;
    // b d c
    edge_kind_triangles[EdgeKind_High][3] = 1;
    edge_kind_triangles[EdgeKind_High][4] = 3;
    edge_kind_triangles[EdgeKind_High][5] = 2;
    // c d e
    edge_kind_triangles[EdgeKind_High][6] = 2;
    edge_kind_triangles[EdgeKind_High][7] = 3;
    edge_kind_triangles[EdgeKind_High][8] = 4;
    // d f e
    edge_kind_triangles[EdgeKind_High][9] = 3;
    edge_kind_triangles[EdgeKind_High][10] = 5;
    edge_kind_triangles[EdgeKind_High][11] = 4;

    //     |   |                    .
    //     e---f--                  .
    //     | \ |                    .
    //     |   d--                  .
    //     | / |                    .
    //     a---b--                  .
    //     |   |                    .
    // a b d
    edge_kind_triangles[EdgeKind_Low][0] = 0;
    edge_kind_triangles[EdgeKind_Low][1] = 1;
    edge_kind_triangles[EdgeKind_Low][2] = 3;
    // a d e
    edge_kind_triangles[EdgeKind_Low][3] = 0;
    edge_kind_triangles[EdgeKind_Low][4] = 3;
    edge_kind_triangles[EdgeKind_Low][5] = 4;
    // d f e
    edge_kind_triangles[EdgeKind_Low][6] = 3;
    edge_kind_triangles[EdgeKind_Low][7] = 5;
    edge_kind_triangles[EdgeKind_Low][8] = 4;
    // filler
    edge_kind_triangles[EdgeKind_Low][9] = 4;
    edge_kind_triangles[EdgeKind_Low][10] = 4;
    edge_kind_triangles[EdgeKind_Low][11] = 4;

    CornerKind corner_kind_from_edges[EdgeKind_Count][EdgeKind_Count];
    corner_kind_from_edges[EdgeKind_Low][EdgeKind_Low] = CornerKind_LowLow;
    corner_kind_from_edges[EdgeKind_High][EdgeKind_High] = CornerKind_HighHigh;
    corner_kind_from_edges[EdgeKind_High][EdgeKind_Low] = CornerKind_HighLow;
    corner_kind_from_edges[EdgeKind_Low][EdgeKind_High] = CornerKind_LowHigh;

    // Patch id:
    // bit 0 = bottom edge
    // bit 1 = right edge
    // bit 2 = left edge

    struct EdgeReferencePointParameters
    {
        int start;
        int end;
        int step;
    };

    EdgeReferencePointParameters edge_references[Edge_Count];
    edge_references[Edge_Bottom] = {4, Size - 3, 2};
    edge_references[Edge_Left] = {Size * 2, Size * (Size - 5), Size * 2};
    edge_references[Edge_Right] = {Size * 4 + Size - 5, Size * (Size - 2) + 2, Size * 2 - 2};

    for (int patch = 0; patch < 8; ++patch)
    {
        EdgeKind edges[Edge_Count];
        edges[Edge_Bottom] = ((patch & 1) == 0) ? EdgeKind_Low : EdgeKind_High;
        edges[Edge_Right] = ((patch & 2) == 0) ? EdgeKind_Low : EdgeKind_High;
        edges[Edge_Left] = ((patch & 4) == 0) ? EdgeKind_Low : EdgeKind_High;

        CornerKind corners[Corner_Count];
        corners[Corner_BottomLeft] = corner_kind_from_edges[edges[Edge_Bottom]][edges[Edge_Left]];
        corners[Corner_Top] = corner_kind_from_edges[edges[Edge_Left]][edges[Edge_Right]];
        corners[Corner_BottomRight] = corner_kind_from_edges[edges[Edge_Right]][edges[Edge_Bottom]];

        for (int corner = 0; corner < Corner_Count; ++corner)
        {
            const auto& this_corner_indices = corner_indices[corner];
            const auto& this_indices = corner_kind_triangles[corners[corner]];
            static constexpr int TriangleCount = HRZ_ARRAY_COUNT(this_indices) / 3;

            for (int i = 0; i < TriangleCount; ++i)
            {
                write_elements_3(
                    patch, this_corner_indices[this_indices[i * 3 + 0]],
                    this_corner_indices[this_indices[i * 3 + 1]],
                    this_corner_indices[this_indices[i * 3 + 2]]);
            }
        }

        for (int edge = 0; edge < Edge_Count; ++edge)
        {
            EdgeReferencePointParameters& ref_point = edge_references[edge];

            for (int a = ref_point.start; a <= ref_point.end; a += ref_point.step)
            {
                const uint16_t* this_indices = indices.get() + a;
                const auto& this_edge_offsets = edge_offsets[edge];
                const auto& this_edge_indices = edge_kind_triangles[edges[edge]];
                static constexpr int TriangleCount = HRZ_ARRAY_COUNT(this_edge_indices) / 3;

                for (int i = 0; i < TriangleCount; ++i)
                {
                    write_elements_3(
                        patch, this_indices[this_edge_offsets[this_edge_indices[i * 3 + 0]]],
                        this_indices[this_edge_offsets[this_edge_indices[i * 3 + 1]]],
                        this_indices[this_edge_offsets[this_edge_indices[i * 3 + 2]]]);
                }
            }
        }
    }

    std::array<PatchGeometryInfo, 8> patch_geometry_info;
    for (int i = 0; i < 8; ++i)
    {
        patch_geometry_info[i] = {elements_count[i], max_elements_per_patch * i};
    }

    return patch_geometry_info;
}

my::ResourceHandle create_tessellation(
    hrz::Render* render,
    std::vector<std::pair<uint16_t, uint16_t>>* subdivision,
    std::array<PatchGeometryInfo, 8>* patch_geometry_info)
{
    std::vector<uint16_t> elements;
    *patch_geometry_info = generate_subdivision_indices(elements, subdivision);

    my::BufferResource res(my::BufferResource::Index);
    res.size = sizeof(uint16_t) * elements.size();
    res.data = elements.data();
    res.usage = my::UsageHint::Static;

    return render->rc->alloc(
        &res, hrz::monitoring::systems::PlanetGeometry, {{"contents"_ss, "elements buffer"_ss}});
}

my::ResourceHandle create_nearest_sampler(hrz::Render* render)
{
    my::SamplerResource res;
    res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
    res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
    res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
    res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
    res.sampler.is_shadow = false;
    res.use_mipmaps = false;

    return render->rc->alloc(&res, hrz::monitoring::systems::PlanetGeometry);
}

struct TreeTraverseCtx
{
    lm::dvec3 eye_pos;
    std::pair<double, double> dtm_min_max;
    bool dtm_min_max_has_changed;
    const my::Renderer::Culler* culler;
    lm::dmat4 view_matrix;
    lm::dvec3 horizon_cone_direction;
    lm::dvec2 horizon_cone_half_angle_cos_sin;
    uint32_t reserved_instance_count;
    double terrain_res;
    std::span<const std::pair<uint16_t, uint16_t>> subdivision;
    my::Renderer::ViewMask main_views;
};

my::OrientedBoundingBox compute_patch_oriented_bounding_box(
    lm::dvec3 pa,
    lm::dvec3 pb,
    lm::dvec3 pc,
    const std::pair<double, double>& dtm_min_max)
{
    double min_elevation = dtm_min_max.first;
    double max_elevation = dtm_min_max.second;

    lm::dvec3 vertices_center = (pa + pb + pc) / 3;
    lm::dvec3 center_direction = lm::normalize(vertices_center);
    lm::dvec3 on_sphere_center = center_direction * hrz::EARTH_RADIUS;
    lm::dvec3 center = on_sphere_center + center_direction * (min_elevation + max_elevation) / 2;

    lm::dvec3 u_axis = center_direction;
    lm::dvec3 v_axis = lm::normalize((pa - center) - lm::dot(pa - center, u_axis) * u_axis);
    lm::dvec3 w_axis = lm::cross(u_axis, v_axis);

    double u_half_length = 0.0;
    double v_half_length = 0.0;
    double w_half_length = 0.0;

    auto update_half_lengths = [&](const lm::dvec3& p)
    {
        lm::dvec3 offset = p - center;
        u_half_length = std::max(u_half_length, std::abs(lm::dot(offset, u_axis)));
        v_half_length = std::max(v_half_length, std::abs(lm::dot(offset, v_axis)));
        w_half_length = std::max(w_half_length, std::abs(lm::dot(offset, w_axis)));
    };

    update_half_lengths(on_sphere_center + center_direction * min_elevation);
    update_half_lengths(on_sphere_center + center_direction * max_elevation);

    lm::dvec3 pa_direction = lm::normalize(pa);
    update_half_lengths(pa + pa_direction * min_elevation);
    update_half_lengths(pa + pa_direction * max_elevation);

    lm::dvec3 pb_direction = lm::normalize(pb);
    update_half_lengths(pb + pb_direction * min_elevation);
    update_half_lengths(pb + pb_direction * max_elevation);

    lm::dvec3 pc_direction = lm::normalize(pc);
    update_half_lengths(pc + pc_direction * min_elevation);
    update_half_lengths(pc + pc_direction * max_elevation);

    return my::OrientedBoundingBox{
        center, u_axis, u_half_length, v_axis, v_half_length, w_axis, w_half_length,
    };
}

// This structure stores the patches that tessellate the sphere.
// Each patch is a triangle from a recursively-tessellated icosahedron on the
// surface of a sphere. Thus each patch has 4 children.
//
// There is an instance texture that contains the info for each patch, namely
// its geometry id, its geometry slot, the lat/long coordinates that all vertices'
// lat/long coords are offset by, and the matrix used to put in in view space. (Column major)
// The geometry id is the index in the elements texture.
// The geometry slot is the index in the geometry texture.
//
// The elements texture contains the indices used to tessellate the patches based
// on the geometry of neighboring patches.
// The index are used to sample the geometry texture.
//
// The geometry texture contains all vertices of a fully tessellated patch.
// Each vertex containers its position (offset by its first point, the transform
// matrix from the instance texture is used to correct it), normal, and lat/long
// coordinates (offset by the value in the instance texture). This data is contained
// in 2 texels per vertex.
// || R     | G     | B     | A        || R        | G        | B   | A    || ...
// || pos_x | pos_y | pos_z | normal_x || normal_y | normal_z | lat | long || ...
//
// Each frame, the tree is updated by removing nodes that won't be visited, adding
// new ones, etc. All patches are addressed by their index so the overall memory is
// never reallocated.
// Nodes that are evicted from the tree also get their geometry evicted from the
// geometry texture if present.
// This traversal traverses all potential nodes, but culls them, and limit the depth.
//
// A second traversal of the tree is done when rendering. This time we first traverse
// the nodes closest to the camera and do a breadth-first traversal. This way
// we make sure the entire visible planet is tessellated in the fixed limit of
// patches, while having the finer tessellation close to the camera.
// Some patches are added to the list of patches to render. All other patches
// from the tree get their geometry slot marked as freeable: if we run out of free
// geometry slots, those ones will be recycled, otherwise we don't touch them.
// Finally, we iterate over the patches we have to render, create the geometry when
// needed by recycling the slots, and finally add them to the instance texture.
struct PatchTree
{
    enum
    {
        None = 0xffffU,
    };

    struct PatchVertex
    {
        lm::dvec3 pos;
        lm::dvec3 normal;
        hrz::GeoPosition2 geo;

        static PatchVertex from_geo(const hrz::GeoPosition2& geo)
        {
            lm::dvec3 pos = hrz::geo_to_ecef(geo);
            lm::dvec3 normal = hrz::geo_to_normal(geo);

            return PatchVertex{pos, normal, geo};
        }
    };

    struct Patch
    {
        PatchVertex a, b, c;
        int level = 0;
        uint16_t child_a = None, child_b = None, child_c = None, child_middle = None;
        bool culled = false;

        // This is the row in the vertices texture that stores the tessellation
        // of this patch.
        uint16_t geometry_slot = None;

        // This is the type of geometry used to index the patch. It is the row
        // in the elements texture.
        uint8_t geometry_type;

        // Distance to the camera. Used to give priority to patches close to the camera.
        float distance;

        std::optional<my::OrientedBoundingBox> bbox = std::nullopt;

        // This is the cone that goes through the center of the planet and
        // contains the patch.
        lm::dvec2 cone_half_angle_cos_sin;
        lm::dvec3 cone_direction;

        inline bool has_children() { return child_a != None || child_b != None || child_c != None; }

        inline void set_vertices(
            const PatchVertex& pa,
            const PatchVertex& pb,
            const PatchVertex& pc)
        {
            a = pa;
            b = pb;
            c = pc;

            const double inv_wgs84 = 1.0 / hrz::WGS84_AXES_LENGTH_RATIO;
            lm::dvec3 a_sph = a.pos;
            lm::dvec3 b_sph = b.pos;
            lm::dvec3 c_sph = c.pos;

            a_sph.z *= inv_wgs84;
            b_sph.z *= inv_wgs84;
            c_sph.z *= inv_wgs84;

            cone_direction = lm::normalize((a_sph + b_sph + c_sph) / 3.0);
            lm::dvec3 a_dir = lm::normalize(a_sph);
            lm::dvec3 b_dir = lm::normalize(b_sph);
            lm::dvec3 c_dir = lm::normalize(c_sph);

            double dot_a = lm::dot(a_dir, cone_direction);
            double dot_b = lm::dot(b_dir, cone_direction);
            double dot_c = lm::dot(c_dir, cone_direction);

            double cos_half_patch_angle = std::min(std::min(dot_a, dot_b), dot_c);
            double sin_half_patch_angle =
                std::sqrt(1.0 - cos_half_patch_angle * cos_half_patch_angle);

            cone_half_angle_cos_sin = {cos_half_patch_angle, sin_half_patch_angle};
        }

        inline double compute_average_edge_length()
        {
            return (lm::length(a.pos - b.pos) + lm::length(b.pos - c.pos)
                    + lm::length(c.pos - a.pos))
                / 3;
        }

        void compute_and_set_distance_to_camera(const lm::dvec3& cam)
        {
            distance = hrz::distance_to_triangle(a.pos, b.pos, c.pos, cam);
        }

        void compute_oriented_bounding_box(const std::pair<double, double>& dtm_min_max)
        {
            bbox = compute_patch_oriented_bounding_box(a.pos, b.pos, c.pos, dtm_min_max);
        }
    };

    struct PatchWithNeighbors
    {
        uint16_t patch;
        uint16_t n_ab;
        uint16_t n_bc;
        uint16_t n_ca;
    };

    // Theses structs are what will be inserted in the textures
    // to the GPU.
    struct VertexGeometryData
    {
        lm::vec3 pos;
        lm::vec3 normal;
        lm::vec2 wmerc_rel;
    };

    static_assert(sizeof(VertexGeometryData) == 8 * sizeof(float), "VertexGeometryData size");

    struct RenderInstanceData
    {
        lm::vec2 wmerc_base;
        uint32_t geometry_slot;
        uint32_t _padding;
        lm::vec4 matrix_row0;
        lm::vec4 matrix_row1;
        lm::vec4 matrix_row2;
    };

    HRZ_CHECK_UBO_SIZE(RenderInstanceData);

    struct RenderBinData
    {
        HRZ_UBO_STRUCT_FIELD(RenderInstanceData) instances[BinSize];
    };

    HRZ_CHECK_UBO_SIZE(RenderBinData);

    struct PrecomputeInstanceData
    {
        lm::vec2 wmerc_base;
        uint32_t geometry_slot;
        float wmerc_scale;
        float pos_scale;
        uint32_t _padding2[3];
    };

    HRZ_CHECK_UBO_SIZE(PrecomputeInstanceData);

    struct PrecomputeBinData
    {
        HRZ_UBO_STRUCT_FIELD(PrecomputeInstanceData) instances[BinSize];
    };

    HRZ_CHECK_UBO_SIZE(RenderBinData);

    struct PatchBin
    {
        int patch_type;
        uint32_t patch_count;
        RenderBinData render_data;
        PrecomputeBinData precompute_data;
        size_t render_data_offset;
        size_t precompute_data_offset;
    };

    std::vector<PatchBin> patch_bins;

    uint32_t vertices_per_patch;

    std::vector<Patch> patches;
    std::vector<uint16_t> free_patches;

    std::vector<PatchVertex> geometry;
    std::vector<uint16_t> free_geometry_slots;
    std::vector<uint16_t> patch_could_free_geometry_slot;

    uint16_t to_render_reserved_count;

    template<typename T, my::TextureFormat F>
    class DoubleBufferedDataTexture
    {
        static constexpr size_t ColumnsPerEntry =
            sizeof(T) / my::format_external_pixel_byte_size(F);

        struct Texture
        {
            int first_dirty_row = std::numeric_limits<int>::max();
            int last_dirty_row = std::numeric_limits<int>::lowest();
            my::ResourceHandle handle;

            inline void invalidate_row(int y)
            {
                first_dirty_row = std::min(first_dirty_row, y);
                last_dirty_row = std::max(last_dirty_row, y);
            }

            inline void reset_dirty_rows()
            {
                first_dirty_row = std::numeric_limits<int>::max();
                last_dirty_row = std::numeric_limits<int>::lowest();
            }

            constexpr bool has_dirty_rows() const { return first_dirty_row <= last_dirty_row; }
        };

        hrz::Buffer<T> _data;
        Texture _textures[2];
        mutable int _current_writable = 0;
        mutable uint64_t _last_flip_frame = 0;

    public:
        void initialize(
            hrz::Render* render,
            int w,
            int h,
            hrz::monitoring::systems::Name system,
            std::initializer_list<std::pair<hrz::MetadataString, hrz::MetadataString>> metadata)
        {
            static_assert(!my::is_format_compressed(F), "Cannot use compressed texture formats");
            static_assert(
                sizeof(T) % my::format_external_pixel_byte_size(F) == 0,
                "Internal and external formats sizes don't match");

            _data = hrz::Buffer<T>(w, h);

            my::TextureResource res;
            res.layout.type = my::TextureLayout::Type2D;
            res.layout.format = F;
            res.layout.width = _data.width() * ColumnsPerEntry;
            res.layout.height = h;
            res.layout.depth = 1;
            res.layout.levels = 1;
            res.data = {};
            res.generate_mipmaps = false;

            _textures[0].handle = render->rc->alloc(&res, system, metadata);
            _textures[1].handle = render->rc->alloc(&res, system, metadata);
        }

        void destroy(hrz::Render* render)
        {
            render->rc->dealloc(_textures[0].handle);
            render->rc->dealloc(_textures[1].handle);
        }

        void set(int x, int y, const T& value)
        {
            _data(x, y) = value;
            _textures[0].invalidate_row(y);
            _textures[1].invalidate_row(y);
        }

        bool update(hrz::Render* render)
        {
            auto& texture = _textures[_current_writable];

            if (!texture.has_dirty_rows()) return false;

            // We only update whole lines at a time so that we don't need to do
            // a whole bunch of complicated PixelStore things.
            int h = texture.last_dirty_row - texture.first_dirty_row + 1;
            int y = texture.first_dirty_row;

            render->my->update_texture(
                texture.handle, F, 0, 0, y, 0, _data.width() * ColumnsPerEntry, h, 1,
                std::as_bytes(_data.as_span(0, y)));

            texture.reset_dirty_rows();
            return true;
        }

        my::ResourceHandle get_for_gpu() const
        {
            if (_last_flip_frame != hrz::clock::CurrentFrameNumber)
            {
                _current_writable = 1 - _current_writable;
                _last_flip_frame = hrz::clock::CurrentFrameNumber;
            }
            const auto& texture = _textures[1 - _current_writable];
            assert(!texture.has_dirty_rows());
            return texture.handle;
        }
    };

    DoubleBufferedDataTexture<VertexGeometryData, my::TextureFormat::RGBA32F> geometry_texture;

    hrz::render::DoubleBufferedUniformBuffer<RenderBinData> render_bin_data;
    hrz::render::DoubleBufferedUniformBuffer<PrecomputeBinData> precompute_bin_data;

    // Used when traversing the geometry. They are here so we can recycle memory.
    std::vector<std::pair<uint16_t, float>> patch_distances;
    std::vector<PatchWithNeighbors> to_visit;
    std::vector<uint16_t> to_render;

    void init(hrz::Render* render)
    {
        // Regular icosahedron with vertices at the poles.
        static const double LAT = std::atan(0.5);
        static const double LON_DELTA = lm::radians(36.0);

        static hrz::GeoPosition2 vertices_geo[] = {
            hrz::GeoPosition2(lm::PI / 2, 0),

            hrz::GeoPosition2(LAT, LON_DELTA * 0),  hrz::GeoPosition2(LAT, LON_DELTA * 2),
            hrz::GeoPosition2(LAT, LON_DELTA * 4),  hrz::GeoPosition2(LAT, LON_DELTA * 6),
            hrz::GeoPosition2(LAT, LON_DELTA * 8),

            hrz::GeoPosition2(-LAT, LON_DELTA * 1), hrz::GeoPosition2(-LAT, LON_DELTA * 3),
            hrz::GeoPosition2(-LAT, LON_DELTA * 5), hrz::GeoPosition2(-LAT, LON_DELTA * 7),
            hrz::GeoPosition2(-LAT, LON_DELTA * 9),

            hrz::GeoPosition2(-lm::PI / 2, 0)
        };

        PatchVertex vertices[12];

        for (int i = 0; i < 12; ++i)
        {
            vertices[i] = PatchVertex::from_geo(vertices_geo[i]);
        }

        static Patch root_patches[RootPatchCount];
        root_patches[0].set_vertices(vertices[1], vertices[2], vertices[0]);
        root_patches[1].set_vertices(vertices[2], vertices[3], vertices[0]);
        root_patches[2].set_vertices(vertices[3], vertices[4], vertices[0]);
        root_patches[3].set_vertices(vertices[4], vertices[5], vertices[0]);
        root_patches[4].set_vertices(vertices[5], vertices[1], vertices[0]);
        root_patches[5].set_vertices(vertices[2], vertices[1], vertices[6]);
        root_patches[6].set_vertices(vertices[3], vertices[2], vertices[7]);
        root_patches[7].set_vertices(vertices[4], vertices[3], vertices[8]);
        root_patches[8].set_vertices(vertices[5], vertices[4], vertices[9]);
        root_patches[9].set_vertices(vertices[1], vertices[5], vertices[10]);
        root_patches[10].set_vertices(vertices[6], vertices[7], vertices[2]);
        root_patches[11].set_vertices(vertices[7], vertices[8], vertices[3]);
        root_patches[12].set_vertices(vertices[8], vertices[9], vertices[4]);
        root_patches[13].set_vertices(vertices[9], vertices[10], vertices[5]);
        root_patches[14].set_vertices(vertices[10], vertices[6], vertices[1]);
        root_patches[15].set_vertices(vertices[7], vertices[6], vertices[11]);
        root_patches[16].set_vertices(vertices[8], vertices[7], vertices[11]);
        root_patches[17].set_vertices(vertices[9], vertices[8], vertices[11]);
        root_patches[18].set_vertices(vertices[10], vertices[9], vertices[11]);
        root_patches[19].set_vertices(vertices[6], vertices[10], vertices[11]);
        static_assert(19 == RootPatchCount - 1);

        patches.reserve(MaxPatchCount * 2);
        patches.insert(patches.end(), std::begin(root_patches), std::end(root_patches));

        vertices_per_patch = vertices_count_after_subdivision(SubdivisionCount);

        uint32_t vertices_count = vertices_per_patch * MaxGeometryCacheCount;
        geometry.resize(vertices_count);

        free_geometry_slots.reserve(MaxGeometryCacheCount);
        for (uint16_t i = 0; i < MaxGeometryCacheCount; ++i)
        {
            free_geometry_slots.push_back(i);
        }

        render_bin_data.initialize(
            MaxBinCount, render, hrz::monitoring::systems::PlanetGeometry,
            {{"contents"_ss, "render instances data"_ss}});

        precompute_bin_data.initialize(
            MaxBinCount, render, hrz::monitoring::systems::PlanetGeometry,
            {{"contents"_ss, "precompute instances data"_ss}});

        geometry_texture.initialize(
            render, vertices_per_patch, MaxGeometryCacheCount,
            hrz::monitoring::systems::PlanetGeometry,
            {{"contents"_ss, "planet patch geometry texture"_ss}});
    }

    void destroy(hrz::Render* render)
    {
        geometry_texture.destroy(render);
        render_bin_data.destroy(render);
        precompute_bin_data.destroy(render);
    }

    uint16_t get_free_geometry_slot()
    {
        if (free_geometry_slots.empty())
        {
            assert(!patch_could_free_geometry_slot.empty());
            uint16_t patch_id = patch_could_free_geometry_slot.back();
            patch_could_free_geometry_slot.pop_back();

            assert(patch_id < patches.size());
            Patch& patch = patches[patch_id];

            uint16_t id = patch.geometry_slot;
            patch.geometry_slot = None;
            return id;
        }

        uint16_t id = free_geometry_slots.back();
        free_geometry_slots.pop_back();
        return id;
    }

    uint16_t get_free_patch()
    {
        if (free_patches.empty())
        {
            if (patches.size() >= 65534)
            {
                HRZ_LOG_ERROR("Patches capacity exceeded, things will start breaking down now");
                assert(false && "Patches capacity exceeded");
            }

            patches.resize(patches.size() + 1);
            return (uint16_t)(patches.size() - 1);
        }
        else
        {
            uint16_t id = free_patches.back();
            free_patches.pop_back();
            return id;
        }
    }

    PatchVertex make_patch_vertex(const PatchVertex& a, const PatchVertex& b)
    {
        hrz::GeoPosition2 midpoint = hrz::geodesic_midpoint(a.geo, b.geo);
        return PatchVertex::from_geo(midpoint);
    }

    //              C                           .
    //             / \                          .
    //            /   \                         .
    //           /  2  \                        .
    //          F-------E                       .
    //         / \  3  / \                      .
    //        /   \   /   \                     .
    //       /  0  \ /  1  \                    .
    //      A-------D-------B                   .
    void subdivide(uint16_t patch_id)
    {
        assert(patch_id < patches.size());

        Patch* patch = &patches[patch_id];
        Patch child_a, child_b, child_c, child_middle;

        PatchVertex d = make_patch_vertex(patch->a, patch->b);
        PatchVertex e = make_patch_vertex(patch->b, patch->c);
        PatchVertex f = make_patch_vertex(patch->c, patch->a);

        child_a.set_vertices(patch->a, d, f);
        child_b.set_vertices(d, patch->b, e);
        child_c.set_vertices(f, e, patch->c);
        child_middle.set_vertices(e, f, d);

        child_a.level = patch->level + 1;
        child_b.level = patch->level + 1;
        child_c.level = patch->level + 1;
        child_middle.level = patch->level + 1;

        uint16_t child_a_patch_id = get_free_patch();
        uint16_t child_b_patch_id = get_free_patch();
        uint16_t child_c_patch_id = get_free_patch();
        uint16_t child_middle_patch_id = get_free_patch();

        // refetch reference after potential resize
        patch = &patches[patch_id];

        patch->child_a = child_a_patch_id;
        patch->child_b = child_b_patch_id;
        patch->child_c = child_c_patch_id;
        patch->child_middle = child_middle_patch_id;

        patch = &patches[patch_id];
        patches[patch->child_a] = child_a;
        patches[patch->child_b] = child_b;
        patches[patch->child_c] = child_c;
        patches[patch->child_middle] = child_middle;
    }

    bool geo_equal(const hrz::GeoPosition2& a, const hrz::GeoPosition2& b)
    {
        static const double eps = 0.00001;
        if (std::abs(a.lat - b.lat) < eps && std::abs(a.lon - b.lon) < eps) return true;

        return std::abs(std::cos(a.lat) - std::cos(b.lat)) < DBL_EPSILON
            && std::abs(std::cos(a.lon) - std::cos(b.lon)) < DBL_EPSILON
            && std::abs(std::sin(a.lat) - std::sin(b.lat)) < DBL_EPSILON
            && std::abs(std::sin(a.lon) - std::sin(b.lon)) < DBL_EPSILON;
    }

    //            --C--                             .
    //             / \                              .
    //          1 /0 1\ 0                           .
    //         \ /     \ /                          .
    //        --F-------E--                         .
    //         / \     / \                          .
    //      0 /1  \   /  0\ 1                       .
    //     \ /  0  \ /  1  \ /                      .
    //    --A-------D-------B--                     .
    //       \  1  / \  0  /                        .

    // The indices here are the in_edge_index of the patch relative to itself.
    // The outside indices are obviously inverted compared to the insides ones
    // because they are the inside indices of the neighboring patch.
    // Those indices are used to designate a children across a patch boundary.
    uint16_t get_child_from_neighbor(
        uint16_t to,
        uint16_t in_edge_index,
        const hrz::GeoPosition2& geo0,
        const hrz::GeoPosition2& geo1)
    {
        if (to == None) return None;

        assert(to < patches.size());
        Patch& patch = patches[to];

        if (geo_equal(geo0, patch.a.geo) && geo_equal(geo1, patch.b.geo))
        {
            return (in_edge_index == 0) ? patch.child_a : patch.child_b;
        }
        else if (geo_equal(geo0, patch.b.geo) && geo_equal(geo1, patch.c.geo))
        {
            return (in_edge_index == 0) ? patch.child_b : patch.child_c;
        }
        else if (geo_equal(geo0, patch.c.geo) && geo_equal(geo1, patch.a.geo))
        {
            return (in_edge_index == 0) ? patch.child_c : patch.child_a;
        }
        else
        {
            return None;
        }
    }

    void evict_patch(uint16_t patch_id)
    {
        assert(patch_id < patches.size());

        Patch& patch = patches[patch_id];
        if (patch.has_children())
        {
            evict_patch(patch.child_a);
            evict_patch(patch.child_b);
            evict_patch(patch.child_c);
            evict_patch(patch.child_middle);
        }

        if (patch.geometry_slot != None)
        {
            free_geometry_slots.push_back(patch.geometry_slot);
            patch.geometry_slot = None;
        }

        free_patches.push_back(patch_id);
    }

    void mark_subtree_geometry_slots_as_freeable(uint16_t patch_id)
    {
        assert(patch_id < patches.size());
        Patch& patch = patches[patch_id];

        if (patch.geometry_slot != None)
        {
            patch_could_free_geometry_slot.push_back(patch_id);
        }

        if (patch.has_children())
        {
            mark_subtree_geometry_slots_as_freeable(patch.child_a);
            mark_subtree_geometry_slots_as_freeable(patch.child_b);
            mark_subtree_geometry_slots_as_freeable(patch.child_c);
            mark_subtree_geometry_slots_as_freeable(patch.child_middle);
        }
    }

    void cull_patch(const TreeTraverseCtx& ctx, uint16_t patch_id)
    {
        assert(patch_id < patches.size());

        Patch& patch = patches[patch_id];

        double cos_angle = lm::dot(patch.cone_direction, ctx.horizon_cone_direction);

        // Compute "angle between cones > sum of the half angles of the cones"
        // We get "cos(angle between cones) < cos(half angle patch cone + half angle horizon cone)"
        // Then use the cos(a + b) identity and you get the mess below.

        // First try horizon culling
        if (cos_angle < ctx.horizon_cone_half_angle_cos_sin.x * patch.cone_half_angle_cos_sin.x
                - ctx.horizon_cone_half_angle_cos_sin.y * patch.cone_half_angle_cos_sin.y)
        {
            patch.culled = true;
        }
        // Then frustum culling
        else
        {
            const auto& bbox = patch.bbox.value();
            patch.culled = !ctx.culler->is_visible_in_some_views(bbox, ctx.main_views);

            if (hrz::get_flag(hrz::Flag::DebugDrawTerrainPatchBboxes))
            {
                std::array<lm::dvec3, 2> x_axis_vertices;
                x_axis_vertices[0] = bbox.center + bbox.u_axis * -bbox.u_half_length;
                x_axis_vertices[1] = bbox.center + bbox.u_axis * bbox.u_half_length;
                std::array<lm::dvec3, 2> y_axis_vertices;
                y_axis_vertices[0] = bbox.center + bbox.v_axis * -bbox.v_half_length;
                y_axis_vertices[1] = bbox.center + bbox.v_axis * bbox.v_half_length;
                std::array<lm::dvec3, 2> z_axis_vertices;
                z_axis_vertices[0] = bbox.center + bbox.w_axis * -bbox.w_half_length;
                z_axis_vertices[1] = bbox.center + bbox.w_axis * bbox.w_half_length;
                std::array<lm::dvec3, 5> bottom_vertices;
                bottom_vertices[0] = bbox.center + bbox.u_axis * -bbox.u_half_length
                    + bbox.v_axis * -bbox.v_half_length + bbox.w_axis * -bbox.w_half_length;
                bottom_vertices[1] = bbox.center + bbox.u_axis * -bbox.u_half_length
                    + bbox.v_axis * bbox.v_half_length + bbox.w_axis * -bbox.w_half_length;
                bottom_vertices[2] = bbox.center + bbox.u_axis * -bbox.u_half_length
                    + bbox.v_axis * bbox.v_half_length + bbox.w_axis * bbox.w_half_length;
                bottom_vertices[3] = bbox.center + bbox.u_axis * -bbox.u_half_length
                    + bbox.v_axis * -bbox.v_half_length + bbox.w_axis * bbox.w_half_length;
                bottom_vertices[4] = bbox.center + bbox.u_axis * -bbox.u_half_length
                    + bbox.v_axis * -bbox.v_half_length + bbox.w_axis * -bbox.w_half_length;
                std::array<lm::dvec3, 5> top_vertices;
                top_vertices[0] = bbox.center + bbox.u_axis * bbox.u_half_length
                    + bbox.v_axis * -bbox.v_half_length + bbox.w_axis * -bbox.w_half_length;
                top_vertices[1] = bbox.center + bbox.u_axis * bbox.u_half_length
                    + bbox.v_axis * bbox.v_half_length + bbox.w_axis * -bbox.w_half_length;
                top_vertices[2] = bbox.center + bbox.u_axis * bbox.u_half_length
                    + bbox.v_axis * bbox.v_half_length + bbox.w_axis * bbox.w_half_length;
                top_vertices[3] = bbox.center + bbox.u_axis * bbox.u_half_length
                    + bbox.v_axis * -bbox.v_half_length + bbox.w_axis * bbox.w_half_length;
                top_vertices[4] = bbox.center + bbox.u_axis * bbox.u_half_length
                    + bbox.v_axis * -bbox.v_half_length + bbox.w_axis * -bbox.w_half_length;
                std::array<lm::dvec3, 2> side_vertices_0 = {bottom_vertices[0], top_vertices[0]};
                std::array<lm::dvec3, 2> side_vertices_1 = {bottom_vertices[1], top_vertices[1]};
                std::array<lm::dvec3, 2> side_vertices_2 = {bottom_vertices[2], top_vertices[2]};
                std::array<lm::dvec3, 2> side_vertices_3 = {bottom_vertices[3], top_vertices[3]};
                hrz::debug_draw::polyline(
                    {(const double*)x_axis_vertices.data(), x_axis_vertices.size() * 3},
                    {1, 0, 0, 1}, hrz::debug_draw::Space::Ecef);
                hrz::debug_draw::polyline(
                    {(const double*)y_axis_vertices.data(), y_axis_vertices.size() * 3},
                    {0, 1, 0, 1}, hrz::debug_draw::Space::Ecef);
                hrz::debug_draw::polyline(
                    {(const double*)z_axis_vertices.data(), z_axis_vertices.size() * 3},
                    {1, 1, 0, 1}, hrz::debug_draw::Space::Ecef);
                hrz::debug_draw::polyline(
                    {(const double*)bottom_vertices.data(), bottom_vertices.size() * 3},
                    {1, 1, 1, 1}, hrz::debug_draw::Space::Ecef);
                hrz::debug_draw::polyline(
                    {(const double*)top_vertices.data(), top_vertices.size() * 3}, {1, 1, 1, 1},
                    hrz::debug_draw::Space::Ecef);
                hrz::debug_draw::polyline(
                    {(const double*)side_vertices_0.data(), side_vertices_0.size() * 3},
                    {1, 1, 1, 1}, hrz::debug_draw::Space::Ecef);
                hrz::debug_draw::polyline(
                    {(const double*)side_vertices_1.data(), side_vertices_1.size() * 3},
                    {1, 1, 1, 1}, hrz::debug_draw::Space::Ecef);
                hrz::debug_draw::polyline(
                    {(const double*)side_vertices_2.data(), side_vertices_2.size() * 3},
                    {1, 1, 1, 1}, hrz::debug_draw::Space::Ecef);
                hrz::debug_draw::polyline(
                    {(const double*)side_vertices_3.data(), side_vertices_3.size() * 3},
                    {1, 1, 1, 1}, hrz::debug_draw::Space::Ecef);
            }
        }
    }

    void traverse_patch_culling(const TreeTraverseCtx& ctx, uint16_t patch_id)
    {
        assert(patch_id < patches.size());

        Patch& patch = patches[patch_id];
        bool should_visit_children = true;

        patch.compute_and_set_distance_to_camera(ctx.eye_pos);

        if (!patch.bbox.has_value() || ctx.dtm_min_max_has_changed)
        {
            patch.compute_oriented_bounding_box(ctx.dtm_min_max);
        }

        cull_patch(ctx, patch_id);

        if (patch.culled)
        {
            should_visit_children = false;
        }
        else
        {
            if (patch.distance < ctx.terrain_res * patch.compute_average_edge_length()
                && patch.level < (int32_t)MaxPatchDepth - 1)
            {
                should_visit_children = true;
            }
            else
            {
                should_visit_children = false;
            }
        }

        if (should_visit_children)
        {
            if (!patch.has_children())
            {
                subdivide(patch_id);
            }

            traverse_patch_culling(ctx, patches[patch_id].child_a);
            traverse_patch_culling(ctx, patches[patch_id].child_b);
            traverse_patch_culling(ctx, patches[patch_id].child_c);
            traverse_patch_culling(ctx, patches[patch_id].child_middle);
        }
        else
        {
            if (patch.has_children())
            {
                evict_patch(patch.child_a);
                evict_patch(patch.child_b);
                evict_patch(patch.child_c);
                evict_patch(patch.child_middle);

                patch.child_a = None;
                patch.child_b = None;
                patch.child_c = None;
                patch.child_middle = None;
            }
        }
    }

    void traverse_culling(const TreeTraverseCtx& ctx)
    {
        HRZ_SCOPED_SAMPLE("planet traverse culling");

        for (uint16_t i = 0; i < RootPatchCount; ++i)
        {
            traverse_patch_culling(ctx, i);
        }
    }

    void traverse_patch_render(
        TreeTraverseCtx& ctx,
        uint16_t patch_id,
        uint16_t neighbor_ab,
        uint16_t neighbor_bc,
        uint16_t neighbor_ca)
    {
        assert(patch_id < patches.size());
        Patch& patch = patches[patch_id];

        bool render = false;

        if (patch.culled)
        {
            if (patch.geometry_slot != None)
            {
                patch_could_free_geometry_slot.push_back(patch_id);
            }

            to_render_reserved_count -= 1;
            render = false;
        }
        else if (patch.has_children() && to_render_reserved_count + 3 <= (int32_t)MaxPatchCount)
        {
            to_render_reserved_count += 3; // +4 children - 1 current

            to_visit.push_back(
                PatchWithNeighbors{
                    patch.child_a,
                    get_child_from_neighbor(neighbor_ab, 1, patch.b.geo, patch.a.geo),
                    patch.child_middle,
                    get_child_from_neighbor(neighbor_ca, 0, patch.a.geo, patch.c.geo)
                });

            to_visit.push_back(
                PatchWithNeighbors{
                    patch.child_b,
                    get_child_from_neighbor(neighbor_ab, 0, patch.b.geo, patch.a.geo),
                    get_child_from_neighbor(neighbor_bc, 1, patch.c.geo, patch.b.geo),
                    patch.child_middle
                });

            to_visit.push_back(
                PatchWithNeighbors{
                    patch.child_c, patch.child_middle,
                    get_child_from_neighbor(neighbor_bc, 0, patch.c.geo, patch.b.geo),
                    get_child_from_neighbor(neighbor_ca, 1, patch.a.geo, patch.c.geo)
                });

            to_visit.push_back(
                PatchWithNeighbors{
                    patch.child_middle, patch.child_c, patch.child_a, patch.child_b
                });

            if (patch.geometry_slot != None)
            {
                patch_could_free_geometry_slot.push_back(patch_id);
            }
        }
        else
        {
            render = true;
        }

        if (render)
        {
            if (patch.has_children())
            {
                mark_subtree_geometry_slots_as_freeable(patch.child_a);
                mark_subtree_geometry_slots_as_freeable(patch.child_b);
                mark_subtree_geometry_slots_as_freeable(patch.child_c);
                mark_subtree_geometry_slots_as_freeable(patch.child_middle);
            }

            uint8_t patch_geometry_id = 0;

            if (neighbor_ab != None)
            {
                patch_geometry_id += 1;
            }

            if (neighbor_bc != None)
            {
                patch_geometry_id += 2;
            }

            if (neighbor_ca != None)
            {
                patch_geometry_id += 4;
            }

            patch.geometry_type = patch_geometry_id;
            to_render.push_back(patch_id);
        }
    }

    void tessellate_patch(const TreeTraverseCtx& ctx, uint16_t patch_id)
    {
        HRZ_SCOPED_SAMPLE_A("planet tessellate patch");

        assert(patch_id < patches.size());
        Patch& patch = patches[patch_id];

        patch.geometry_slot = get_free_geometry_slot();

        std::span<PatchVertex> slice(
            geometry.data() + patch.geometry_slot * vertices_per_patch, vertices_per_patch);

        slice[0] = patch.a;
        slice[1] = patch.b;
        slice[2] = patch.c;

        hrz::normalize_longitude(slice[0].geo, slice[1].geo, slice[2].geo);

        for (uint32_t i = 3; i < vertices_per_patch; ++i)
        {
            slice[i] = make_patch_vertex(
                slice[ctx.subdivision[i].first], slice[ctx.subdivision[i].second]);
        }
    }

    void traverse_render(TreeTraverseCtx& ctx)
    {
        HRZ_SCOPED_SAMPLE("planet traverse render");

        static const uint16_t root_neighbors[RootPatchCount][3] = {
            {5, 1, 4},    {6, 2, 0},    {7, 3, 1},    {8, 4, 2},    {9, 0, 3},
            {0, 14, 10},  {1, 10, 11},  {2, 11, 12},  {3, 12, 13},  {4, 13, 14},
            {15, 6, 5},   {16, 7, 6},   {17, 8, 7},   {18, 9, 8},   {19, 5, 9},
            {10, 19, 16}, {11, 15, 17}, {12, 16, 18}, {13, 17, 19}, {14, 18, 15},
        };

        uint16_t level_first = 0;
        uint16_t level_count = RootPatchCount;
        to_visit.clear();
        to_render.clear();
        patch_could_free_geometry_slot.clear();

        to_render_reserved_count = RootPatchCount;

        for (uint16_t i = 0; i < RootPatchCount; ++i)
        {
            to_visit.push_back(
                PatchWithNeighbors{
                    i, root_neighbors[i][0], root_neighbors[i][1], root_neighbors[i][2]
                });
        }

        // Same thing but now for rendering
        // Here we traverse the patches closest to the camera first.
        while (true)
        {
            patch_distances.clear();

            for (uint16_t i = 0; i < level_count; ++i)
            {
                const uint16_t patch_id = to_visit[i + level_first].patch;
                patch_distances.emplace_back(i, patches[patch_id].distance);
            }

            std::ranges::sort(
                patch_distances,
                [](const std::pair<uint16_t, float>& p0, const std::pair<uint16_t, float>& p1)
                { return p0.second < p1.second; });

            for (const auto& p : patch_distances)
            {
                const auto& patch = to_visit[p.first + level_first];
                traverse_patch_render(ctx, patch.patch, patch.n_ab, patch.n_bc, patch.n_ca);
            }

            const auto patch_count = (uint32_t)to_visit.size();
            if (patch_count == level_first + level_count)
            {
                // None added, we are done here
                break;
            }
            else
            {
                level_first = level_first + level_count;
                level_count = patch_count - level_first;
            }
        }

        patch_bins.clear();

        int bin_index_for_patch_type[8];
        std::fill_n(bin_index_for_patch_type, 8, -1);

        auto get_bin_fn = [&](int patch_type) -> PatchBin*
        {
            int bin_index = bin_index_for_patch_type[patch_type];
            if (bin_index < 0 || patch_bins[bin_index].patch_count >= BinSize)
            {
                if (patch_bins.size() >= MaxBinCount) return nullptr;

                PatchBin new_bin;
                new_bin.patch_count = 0;
                new_bin.patch_type = patch_type;
                patch_bins.push_back(new_bin);

                bin_index = (int)patch_bins.size() - 1;
                bin_index_for_patch_type[patch_type] = bin_index;
            }

            assert(bin_index >= 0 && bin_index < (int32_t)MaxBinCount);
            return &patch_bins[bin_index];
        };

        for (uint16_t patch_id : to_render)
        {
            lm::vec2 base_wmerc;
            lm::vec3 base_pos;
            lm::vec3 partial_base_pos;

            lm::dvec2 wmerc0, wmerc1;
            lm::dvec3 pos0, pos1;

            Patch& patch = patches[patch_id];
            if (patch.geometry_slot == None)
            {
                tessellate_patch(ctx, patch_id);

                std::span<const PatchVertex> vertices(
                    geometry.data() + patch.geometry_slot * vertices_per_patch, vertices_per_patch);

                const PatchVertex& first_vertex = vertices[0];

                const static double wmerc_extent = hrz::MERCATOR_TILE_SIZE
                    * (double)std::pow(2.0, (double)hrz::CLIPMAP_LOD_COUNT - 1);

                lm::dvec2 wmerc = hrz::geo_to_web_mercator_pixels(first_vertex.geo);

                lm::vec2 partial_wmerc;
                hrz::split_vec2d(wmerc, base_wmerc, partial_wmerc);
                hrz::split_vec3d(first_vertex.pos, base_pos, partial_base_pos);

                for (uint32_t i = 0; i < vertices_per_patch; ++i)
                {
                    const PatchVertex& p = vertices[i];

                    lm::dvec2 wmerc_vertex = hrz::geo_to_web_mercator_pixels(p.geo);
                    lm::dvec2 wmerc_rel = wmerc_vertex - base_wmerc;

                    if (wmerc_rel.x > wmerc_extent / 2)
                    {
                        wmerc_rel.x -= wmerc_extent;
                    }

                    if (wmerc_rel.x < -wmerc_extent / 2)
                    {
                        wmerc_rel.x += wmerc_extent;
                    }

                    VertexGeometryData geometry_data = {};
                    geometry_data.pos = lm::vec3(p.pos - base_pos);
                    geometry_data.normal = lm::vec3(p.normal);
                    geometry_data.wmerc_rel = lm::vec2(wmerc_rel);
                    geometry_texture.set(i, patch.geometry_slot, geometry_data);
                }

                wmerc0 = wmerc;
                wmerc1 = hrz::geo_to_web_mercator_pixels(vertices[1].geo);
                pos0 = vertices[0].pos;
                pos1 = vertices[1].pos;
            }
            else
            {
                const PatchVertex* vertices =
                    geometry.data() + (patch.geometry_slot * vertices_per_patch);

                wmerc0 = hrz::geo_to_web_mercator_pixels(vertices[0].geo);
                wmerc1 = hrz::geo_to_web_mercator_pixels(vertices[1].geo);
                pos0 = vertices[0].pos;
                pos1 = vertices[1].pos;

                lm::vec2 partial_wmerc;
                hrz::split_vec2d(wmerc0, base_wmerc, partial_wmerc);
                hrz::split_vec3d(vertices[0].pos, base_pos, partial_base_pos);
            }

            RenderInstanceData render_instance_data = {};
            PrecomputeInstanceData precompute_instance_data = {};
            lm::dmat4 matrix = ctx.view_matrix * lm::translation(base_pos);

            render_instance_data.wmerc_base = base_wmerc;
            render_instance_data.geometry_slot = patch.geometry_slot;
            precompute_instance_data.wmerc_base = base_wmerc;
            precompute_instance_data.geometry_slot = patch.geometry_slot;

            lm::mat4 mat_t = lm::transpose(lm::mat4(matrix));
            render_instance_data.matrix_row0 = mat_t.x;
            render_instance_data.matrix_row1 = mat_t.y;
            render_instance_data.matrix_row2 = mat_t.z;

            // This represents the approximate difference in wmerc and world
            // coordinates between each vertex. It's used to approximate the
            // normals.
            double dist = hrz::distance_web_mercator_pixels(wmerc0, wmerc1);
            precompute_instance_data.wmerc_scale = dist / (double)(VerticesPerSide - 1);
            precompute_instance_data.pos_scale =
                lm::length(pos1 - pos0) / (double)(VerticesPerSide - 1);

            auto* bin = get_bin_fn(patch.geometry_type);
            if (!bin) break;

            bin->render_data.instances[bin->patch_count] = render_instance_data;
            bin->precompute_data.instances[bin->patch_count] = precompute_instance_data;
            bin->patch_count += 1;
        }
    }

    hrz::RenderRequest do_data_uploads(hrz::Render* render)
    {
        HRZ_SCOPED_SAMPLE("planet geometry data uploads");

        hrz::RenderRequest render_request;

        {
            HRZ_SCOPED_SAMPLE("planet geometry geometry texture uploads");
            if (geometry_texture.update(render))
            {
                render_request.request_visual_render();
            }
        }

        for (size_t i = 0; i < patch_bins.size(); ++i)
        {
            auto& bin = patch_bins[i];

            precompute_bin_data.set(i, bin.precompute_data);
            render_bin_data.set(i, bin.render_data);

            bin.precompute_data_offset = precompute_bin_data.offset(i);
            bin.render_data_offset = render_bin_data.offset(i);
        }

        {
            HRZ_SCOPED_SAMPLE("precompute bin uniform data upload");
            precompute_bin_data.update(render->my);
        }

        {
            HRZ_SCOPED_SAMPLE("render bin uniform data upload");
            render_bin_data.update(render->my);
        }

        return render_request;
    }
};

class FeedbackPass : public hrz::render::TimedRenderPass
{
    const char* _color_name;
    const char* _depth_name;
    my::ResourceHandle _color_target;
    my::ResourceHandle _depth_target;
    my::ResourceHandle _fbo;
    my::ResourceHandle _pbo;
    size_t _last_pbo_size = 0;
    lm::uvec2 _last_feedback_size;
    bool _scheduled = false;
    uint64_t _texture_download_id{};

public:
    FeedbackPass() :
        TimedRenderPass("planet feedback"),
        _color_name("planet_feedback_color"),
        _depth_name("planet_feedback_depth")
    {
    }

    void schedule(uint64_t texture_download_id)
    {
        _scheduled = true;
        _texture_download_id = texture_download_id;
    }

    lm::uvec2 last_feedback_size() const { return _last_feedback_size; }

    void destroy(my::ResourceContext* rc)
    {
        rc->dealloc(_fbo);

        if (_pbo != my::ResourceHandle::null())
        {
            rc->dealloc(_pbo);
        }
    }

    void setup_pass(my::RenderGraph::SetupContext& ctx) override
    {
        my::RenderGraph::ResourceInfo depth{};
        depth.format = my::TextureFormat::Depth32F;
        depth.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        depth.width = 1.0F / (float)FeedbackSubsample;
        depth.height = 1.0F / (float)FeedbackSubsample;

        my::RenderGraph::ResourceInfo color{};
        color.format = my::TextureFormat::RGBA32UI;
        color.size_class = my::RenderGraph::ResourceInfo::BackbufferRelative;
        color.width = 1.0F / (float)FeedbackSubsample;
        color.height = 1.0F / (float)FeedbackSubsample;

        ctx.create(_color_name, my::RenderGraph::Target, color);
        ctx.create(_depth_name, my::RenderGraph::Target, depth);
    }

    void retrieve_resources(
        my::Instance* my,
        my::ResourceContext* rc,
        const my::RenderGraph::ResourceContext& ctx) override
    {
        _color_target = ctx.retrieve(_color_name);
        _depth_target = ctx.retrieve(_depth_name);

        my::FramebufferAttachment attachments[] = {
            {my::Attachment::Depth, _depth_target},
            {my::Attachment::Color0, _color_target}
        };

        my::FramebufferResource res;
        res.attachments = attachments;

        _fbo =
            ((hrz::GpuResourceContext*)rc)->alloc(&res, hrz::monitoring::systems::PlanetGeometry);

        _pbo = my::ResourceHandle::null();
    }

    void realloc_pbo(size_t new_size, my::Instance* my, my::ResourceContext* rc)
    {
        if (_pbo != my::ResourceHandle::null())
        {
            rc->dealloc(_pbo);
        }

        my::BufferResource res(my::BufferResource::TextureDownload);
        res.size = new_size * sizeof(lm::uvec4);
        res.usage = my::UsageHint::Download;
        res.data = nullptr;

        _pbo = ((hrz::GpuResourceContext*)rc)
                   ->alloc(
                       &res, hrz::monitoring::systems::PlanetGeometry,
                       {{"contents"_ss, "feedback data"_ss}});

        _last_pbo_size = new_size;
    }

    void execute_timed(const my::RenderGraph::ExecutionContext& ctx) override
    {
        if (!_scheduled) return;

        HRZ_SCOPED_SAMPLE("planet feedback pass draw");

        const hrz::SceneViewRenderGraphUserData* user_data =
            (const hrz::SceneViewRenderGraphUserData*)ctx.user_data;

        const my::ViewportState viewport_state = {
            {0, 0, ctx.backbuffer_width / FeedbackSubsample,
             ctx.backbuffer_height / FeedbackSubsample},
            {0, 0, ctx.backbuffer_width / FeedbackSubsample,
             ctx.backbuffer_height / FeedbackSubsample},
        };

        size_t required_pbo_size = viewport_state.viewport.w * viewport_state.viewport.h;
        if (_pbo == my::ResourceHandle::null()
            || required_pbo_size > _last_pbo_size      // Grow if needed
            || required_pbo_size < _last_pbo_size / 2) // Shrink if small enough
        {
            realloc_pbo(required_pbo_size, ctx.instance, ctx.resource);
        }

        ctx.render->set_framebuffer(_fbo, viewport_state);

        static const my::ClearTarget clear_targets[] = {
            {
                my::Attachment::Color0,
                my::ClearValue::make_color_uint(0, 0, 0, 1),
            },
            {
                my::Attachment::Depth,
                my::ClearValue::make_depth(1.0),
            }
        };

        ctx.render->clear(clear_targets);

        my::Renderer::BinMask pass_masks[] = {
            hrz::RenderPlanetBin,
        };

        ctx.renderer->draw(
            hrz::RenderPlanetFeedback, user_data->main_view, pass_masks, ctx.render, ctx.binder,
            ctx.user_data);

        ctx.render->color_texture_download_async(
            _texture_download_id, _fbo, my::Attachment::Color0, viewport_state.viewport,
            my::TextureDownloadFormat::RGBA32UI, _pbo);

        _scheduled = false;
        _last_feedback_size.x = viewport_state.viewport.w;
        _last_feedback_size.y = viewport_state.viewport.h;
    }
};

// In order to compute normals, we need to know the height at each point of the
// geometry. The easiest way of doing this is to precompute them. This is good
// because it reduced the overall number of virtual texture fetches since there
// only needs to be 1 fetch per point (before 1 per point per triangle).
//
// Once this is done, we can compute the normal at each point by looking at
// surrounding elevations.
struct HeightPrecomputation
{
    enum Sampler
    {
        Tessellation = 0,
        Indirection,
        Atlas,
    };

    my::ResourceHandle _quad_vb;
    my::ResourceHandle _quad_vi;

    my::ResourceHandle _height_lut_shader;
    my::ResourceHandle _height_lut;
    my::ResourceHandle _normal_lut;
    my::ResourceHandle _height_lut_fbo;

    uint32_t _height_lut_width = vertices_count_after_subdivision(SubdivisionCount);
    uint32_t _height_lut_height = MaxPatchCount;

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        // Height LUT shader
        static const my::IndexName attribs[] = {
            {0, "i_pos"},
        };

        static const my::IndexName samplers[] = {
            {Sampler::Atlas, "u_dtm_atlas"},
            {Sampler::Indirection, "u_dtm_indirection"},
            {Sampler::Tessellation, "u_tessellation"},
        };

        static const my::IndexName uniform_blocks[] = {
            {UboPlanetParams, "PlanetParams"},
            {UboPrecomputeBinData, "BinData"},
        };

        static const char* outputs[] = {"o_height", "o_normal"};

        my::ShaderResource res{};
        res.name = hrz_shaders::PlanetPrecomputeHeightLut_name;
        res.link_hint = my::ShaderLinkHint::Initial;
        res.vertex_source_len = hrz_shaders::PlanetPrecomputeHeightLut_vert_len;
        res.vertex_source = hrz_shaders::PlanetPrecomputeHeightLut_vert;
        res.fragment_source_len = hrz_shaders::PlanetPrecomputeHeightLut_frag_len;
        res.fragment_source = hrz_shaders::PlanetPrecomputeHeightLut_frag;
        res.attribs = attribs;
        res.uniform_blocks = uniform_blocks;
        res.samplers = samplers;
        res.outputs = outputs;
        res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
        res.initial_state.depth.test = false;
        res.initial_state.depth.write = false;
        res.initial_state.stencil.enable = false;
        res.initial_state.color_blend.enable = false;
        res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;

        rc->alloc(&res, hrz::monitoring::systems::PlanetGeometry);
    }

    void initialize(hrz::GpuResourceContext* rc)
    {
        {
            my::TextureResource res;
            res.layout.type = my::TextureLayout::Type2D;
            res.layout.format = my::TextureFormat::R32F;
            res.layout.width = _height_lut_width;
            res.layout.height = _height_lut_height;
            res.layout.depth = 1;
            res.layout.levels = 1;
            res.data = {};
            res.generate_mipmaps = false;

            _height_lut = rc->alloc(
                &res, hrz::monitoring::systems::PlanetGeometry, {{"contents"_ss, "height LUT"_ss}});
        }

        {
            my::TextureResource res;
            res.layout.type = my::TextureLayout::Type2D;
            res.layout.format = my::TextureFormat::RG8;
            res.layout.width = _height_lut_width;
            res.layout.height = _height_lut_height;
            res.layout.depth = 1;
            res.layout.levels = 1;
            res.data = {};
            res.generate_mipmaps = false;

            _normal_lut = rc->alloc(
                &res, hrz::monitoring::systems::PlanetGeometry, {{"contents"_ss, "normal LUT"_ss}});
        }

        {
            my::FramebufferAttachment attachments[] = {
                {my::Attachment::Color0, _height_lut},
                {my::Attachment::Color1, _normal_lut},
            };

            my::FramebufferResource res;
            res.attachments = attachments;

            _height_lut_fbo = rc->alloc(&res, hrz::monitoring::systems::PlanetGeometry);
        }

        // Fullscreen quad
        {
            lm::vec2 vertices[] = {
                {-1, -1},
                {3, -1},
                {-1, 3},
            };

            my::BufferResource buf_res(my::BufferResource::Vertex);
            buf_res.size = sizeof(vertices);
            buf_res.data = vertices;
            buf_res.usage = my::UsageHint::Static;

            _quad_vb = rc->alloc(
                &buf_res, hrz::monitoring::systems::PlanetGeometry,
                {{"contents"_ss, "full-screen quad vertices"_ss}});

            my::VertexInputStream streams[] = {
                {0, _quad_vb, my::VertexFormat::Float32_2, 0, 0, my::VertexRate::PerVertex}
            };

            my::VertexInputResource vi_res;
            vi_res.attribs = streams;

            _quad_vi = rc->alloc(&vi_res, hrz::monitoring::systems::PlanetGeometry);
        }

        _height_lut_shader = rc->retrieve_shader(hrz_shaders::PlanetPrecomputeHeightLut_name);
    }

    void destroy(my::ResourceContext* ctx)
    {
        ctx->dealloc(_height_lut_fbo);
        ctx->dealloc(_height_lut);
        ctx->dealloc(_quad_vi);
        ctx->dealloc(_quad_vb);
    }

    void draw(
        hrz::Render* render,
        const my::TextureBinding& atlas,
        const my::TextureBinding& indirection,
        const my::TextureBinding& tessellation,
        const my::TextureBinding& patches,
        const my::UboBinding& planet_params,
        std::span<const PatchTree::PatchBin> bins,
        my::ResourceHandle precompute_bin_ubo)
    {
        HRZ_SCOPED_SAMPLE("height precomputation draw");

        uint64_t query_id = 0;
        auto& instance_info = render->my->get_info();
        if (hrz::render::profiling::is_enabled() && instance_info.has_disjoint_time_query)
        {
            query_id = hrz::render::profiling::acquire_time_query_id();
            render->my->begin_time_query(query_id, "height precomputation");
        }

        render->my->set_framebuffer(
            _height_lut_fbo,
            my::ViewportState{{0, 0, _height_lut_width, 0}, {0, 0, _height_lut_width, 0}});

        my::TextureBinding texture_bindings[] = {
            {Sampler::Atlas, atlas.texture, atlas.sampler},
            {Sampler::Indirection, indirection.texture, indirection.sampler},
            {Sampler::Tessellation, tessellation.texture, tessellation.sampler},
        };

        uint32_t first_patch = 0;
        for (const auto& bin : bins)
        {
            render->my->set_viewport(
                my::ViewportState{
                    {0, first_patch, _height_lut_width, bin.patch_count},
                    {0, first_patch, _height_lut_width, bin.patch_count}
                });

            my::UboBinding ubo_bindings[] = {
                {UboPlanetParams, planet_params.buffer, planet_params.offset, planet_params.size},
                {UboPrecomputeBinData, precompute_bin_ubo, (uint32_t)bin.precompute_data_offset,
                 sizeof(PatchTree::PrecomputeBinData)},
            };

            auto batch_info =
                my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3).instanced(1, first_patch);

            render->my->draw(
                batch_info, _height_lut_shader, _quad_vi, ubo_bindings, texture_bindings);

            first_patch += bin.patch_count;
        }

        if (hrz::render::profiling::is_enabled() && instance_info.has_disjoint_time_query)
        {
            render->my->end_time_query(query_id);
            hrz::render::profiling::register_frame_time_query(query_id);
        }
    }
};

} // namespace

namespace hrz
{
namespace planet
{

struct PlanetRenderable : public my::Renderer::Renderable
{
    double _terrain_res = 1.0;
    PatchTree _tree;
    uint32_t _last_update_frame = ~0U;
    std::vector<std::pair<uint16_t, uint16_t>> _subdivision;
    std::array<PatchGeometryInfo, 8> _patch_geometry_info{};

    hrz_proto::SceneViewIndex _scene_view{};

    bool _transparent_terrain = false;
    bool _cast_shadows = true;

    my::ResourceHandle _nearest_sampler;
    my::ResourceHandle _indices_buffer;
    my::ResourceHandle _vertex_input;
    my::ResourceHandle _visual_shader;
    my::ResourceHandle _visual_transparent_shader;
    my::ResourceHandle _picking_shader;
    my::ResourceHandle _feedback_shader;
    my::ResourceHandle _depth_shader;
    my::ResourceHandle _selection_shader;
    planet::GeometryResources _external_resources;

    HeightPrecomputation _height_precomputation;

    explicit PlanetRenderable(Render* render) : my::Renderer::Renderable()
    {
        _nearest_sampler = create_nearest_sampler(render);
        _indices_buffer = create_tessellation(render, &_subdivision, &_patch_geometry_info);
        load_shaders(render->rc);

        {
            my::VertexInputResource res;
            res.attribs = {};
            res.indices = _indices_buffer;
            _vertex_input = render->rc->alloc(&res, monitoring::systems::PlanetGeometry);
        }

        _tree.init(render);

        _height_precomputation.initialize(render->rc);
    }

    static void collect_visual_shaders(hrz::GpuResourceContext* rc)
    {
        static const my::IndexName ubos[] = {
            {UboFrame, "Frame"},
            {hrz::vector_flat_overlay::UboVectorOverlayCameras, "OverlayCamerasUniform"},
            {UboPlanetParams, "PlanetParams"},
            {UboRenderBinData, "BinData"},
        };

        hrz::StaticVector<
            my::IndexName,
            HRZ_S_MAX_OVERLAY_CASCADES + MAX_IMAGERY_GROUP_COUNT * 2 + HRZ_S_MAX_SUN_CASCADES
                + HRZ_S_VIEWSHED_CNT + 4
        >
            samplers;

        samplers.push_back({SamplerHeightLut, "hrz_height_lut"});
        samplers.push_back({SamplerNormalLut, "hrz_normal_lut"});
        samplers.push_back({SamplerTessellation, "hrz_planet_tessellation"});

        if (get_flag(Flag::EnableAtmosphere))
        {
            samplers.push_back({hrz::SamplerSunColor, hrz::sky::SUN_COLOR_SAMPLER_NAME});
        }

        for (int i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
        {
            samplers.push_back(
                {hrz::vector_flat_overlay::SamplerOverlayStart + i,
                 hrz::vector_flat_overlay::sampler_names[i]});
        }

        for (int i = 0; i < MAX_IMAGERY_GROUP_COUNT; ++i)
        {
            samplers.push_back({sampler_imagery_atlas(i), sampler_imagery_atlas_name[i]});

            samplers.push_back(
                {sampler_imagery_indirection(i), sampler_imagery_indirection_name[i]});
        }

        if (get_flag(Flag::EnableShadows))
        {
            for (int i = 0; i < HRZ_S_MAX_SUN_CASCADES; ++i)
            {
                samplers.push_back(
                    {hrz::SamplerSunShadow0 + i, hrz::shadows::SUN_SHADOW_MAP_SAMPLER_NAMES[i]});
            }
        }

        for (int i = 0; i < HRZ_S_VIEWSHED_CNT; ++i)
        {
            samplers.push_back(
                {hrz::SamplerViewshedShadow0 + i,
                 hrz::viewsheds::VIEWSHED_SHADOW_MAP_SAMPLER_NAMES[i]});
        }

        static const char* outputs[] = {"o_color"};

        my::ShaderResource res{};
        res.name = hrz_shaders::Planet_name;
        res.link_hint = my::ShaderLinkHint::Initial;
        res.vertex_source_len = hrz_shaders::Planet_vert_len;
        res.vertex_source = hrz_shaders::Planet_vert;
        res.fragment_source_len = hrz_shaders::Planet_frag_len;
        res.fragment_source = hrz_shaders::Planet_frag;
        res.uniform_blocks = ubos;
        res.outputs = outputs;
        res.attribs = {};
        res.samplers = samplers;

        auto visual_shader = rc->alloc(&res, monitoring::systems::PlanetGeometry);

        my::ShaderDerivativeResource res_d(visual_shader, res, "Planet_transparent");
        res_d.initial_state.color_blend.enable = true;
        res_d.initial_state.color_blend.color.src = my::ColorBlendState::Factor::One;
        res_d.initial_state.color_blend.color.dst = my::ColorBlendState::Factor::OneMinusSrcAlpha;
        res_d.initial_state.color_blend.color.op = my::ColorBlendState::Op::Add;
        res_d.initial_state.color_blend.alpha.src = my::ColorBlendState::Factor::One;
        res_d.initial_state.color_blend.alpha.dst = my::ColorBlendState::Factor::OneMinusSrcAlpha;
        res_d.initial_state.color_blend.alpha.op = my::ColorBlendState::Op::Add;

        rc->alloc(&res_d, monitoring::systems::PlanetGeometry);
    }

    void load_visual_shader(const my::ResourceContext* rc)
    {
        _visual_shader = rc->retrieve_shader(hrz_shaders::Planet_name);
        _visual_transparent_shader = rc->retrieve_shader("Planet_transparent");
    }

    static void collect_feedback_shaders(hrz::GpuResourceContext* rc)
    {
        static const my::IndexName ubos[] = {
            {UboFrame, "Frame"},
            {UboPlanetParams, "PlanetParams"},
            {UboRenderBinData, "BinData"},
        };

        static const my::IndexName samplers[] = {
            {SamplerHeightLut, "hrz_height_lut"},
            {SamplerTessellation, "hrz_planet_tessellation"}
        };

        static const char* outputs[] = {"o_feedback"};

        my::ShaderResource res{};
        res.name = hrz_shaders::Planet_feedback_name;
        res.link_hint = my::ShaderLinkHint::Initial;
        res.vertex_source_len = hrz_shaders::Planet_feedback_vert_len;
        res.vertex_source = hrz_shaders::Planet_feedback_vert;
        res.fragment_source_len = hrz_shaders::Planet_feedback_frag_len;
        res.fragment_source = hrz_shaders::Planet_feedback_frag;
        res.uniform_blocks = ubos;
        res.outputs = outputs;
        res.attribs = {};
        res.samplers = samplers;

        rc->alloc(&res, monitoring::systems::PlanetGeometry);
    }

    void load_feedback_shader(const my::ResourceContext* rc)
    {
        _feedback_shader = rc->retrieve_shader(hrz_shaders::Planet_feedback_name);
    }

    static void collect_picking_shaders(hrz::GpuResourceContext* rc)
    {
        static const my::IndexName ubos[] = {
            {UboFrame, "Frame"},
            {hrz::vector_flat_overlay::UboVectorOverlayCameras, "OverlayCamerasUniform"},
            {UboPlanetParams, "PlanetParams"},
            {UboRenderBinData, "BinData"}
        };

        hrz::StaticVector<my::IndexName, HRZ_S_MAX_OVERLAY_CASCADES + 2> samplers;

        samplers.push_back({SamplerHeightLut, "hrz_height_lut"});
        samplers.push_back({SamplerTessellation, "hrz_planet_tessellation"});

        for (int i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
        {
            samplers.push_back(
                {hrz::vector_flat_overlay::SamplerOverlayStart + i,
                 hrz::vector_flat_overlay::picking_sampler_names[i]});
        }

        static const char* outputs[] = {"o_object_reference", "o_depth"};

        my::ShaderResource res{};
        res.name = hrz_shaders::Planet_picking_name;
        res.link_hint = my::ShaderLinkHint::Initial;
        res.vertex_source_len = hrz_shaders::Planet_picking_vert_len;
        res.vertex_source = hrz_shaders::Planet_picking_vert;
        res.fragment_source_len = hrz_shaders::Planet_picking_frag_len;
        res.fragment_source = hrz_shaders::Planet_picking_frag;
        res.uniform_blocks = ubos;
        res.outputs = outputs;
        res.attribs = {};
        res.samplers = samplers;

        rc->alloc(&res, monitoring::systems::PlanetGeometry);
    }

    void load_picking_shader(const my::ResourceContext* rc)
    {
        _picking_shader = rc->retrieve_shader(hrz_shaders::Planet_picking_name);
    }

    static void collect_depth_shaders(hrz::GpuResourceContext* rc)
    {
        static const my::IndexName ubos[] = {
            {UboFrame, "Frame"},
            {UboView, "View"},
            {UboPlanetParams, "PlanetParams"},
            {UboRenderBinData, "BinData"}
        };

        static const my::IndexName samplers[] = {
            {SamplerHeightLut, "hrz_height_lut"},
            {SamplerTessellation, "hrz_planet_tessellation"}
        };

        my::ShaderResource res{};
        res.name = hrz_shaders::Planet_depth_name;
        res.link_hint = my::ShaderLinkHint::Initial;
        res.vertex_source_len = hrz_shaders::Planet_depth_vert_len;
        res.vertex_source = hrz_shaders::Planet_depth_vert;
        res.fragment_source_len = hrz_shaders::Planet_depth_frag_len;
        res.fragment_source = hrz_shaders::Planet_depth_frag;
        res.uniform_blocks = ubos;
        res.outputs = {};
        res.attribs = {};
        res.samplers = samplers;
        res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
        res.initial_state.rasterization.depth_bias_factor = 1.0F;
        res.initial_state.rasterization.depth_bias_units = 1.0F;

        rc->alloc(&res, monitoring::systems::PlanetGeometry);
    }

    void load_depth_shader(const my::ResourceContext* rc)
    {
        _depth_shader = rc->retrieve_shader(hrz_shaders::Planet_depth_name);
    }

    static void collect_selection_shaders(hrz::GpuResourceContext* rc)
    {
        static const my::IndexName ubos[] = {
            {UboFrame, "Frame"},
            {hrz::vector_flat_overlay::UboVectorOverlayCameras, "OverlayCamerasUniform"},
            {UboPlanetParams, "PlanetParams"},
            {UboRenderBinData, "BinData"}
        };

        hrz::StaticVector<my::IndexName, HRZ_S_MAX_OVERLAY_CASCADES + 2> samplers;

        samplers.push_back({SamplerHeightLut, "hrz_height_lut"});
        samplers.push_back({SamplerTessellation, "hrz_planet_tessellation"});

        for (int i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
        {
            samplers.push_back(
                {hrz::vector_flat_overlay::SamplerOverlayStart + i,
                 hrz::vector_flat_overlay::selection_sampler_names[i]});
        }

        static const char* outputs[] = {"o_highlight"};

        my::ShaderResource res{};
        res.name = hrz_shaders::Planet_selection_name;
        res.link_hint = my::ShaderLinkHint::Initial;
        res.vertex_source_len = hrz_shaders::Planet_selection_vert_len;
        res.vertex_source = hrz_shaders::Planet_selection_vert;
        res.fragment_source_len = hrz_shaders::Planet_selection_frag_len;
        res.fragment_source = hrz_shaders::Planet_selection_frag;
        res.uniform_blocks = ubos;
        res.outputs = outputs;
        res.attribs = {};
        res.samplers = samplers;

        rc->alloc(&res, monitoring::systems::PlanetGeometry);
    }

    void load_selection_shader(const my::ResourceContext* rc)
    {
        _selection_shader = rc->retrieve_shader(hrz_shaders::Planet_selection_name);
    }

    void load_shaders(my::ResourceContext* rc)
    {
        load_visual_shader(rc);
        load_feedback_shader(rc);
        load_depth_shader(rc);
        load_picking_shader(rc);
        load_selection_shader(rc);
    }

    ~PlanetRenderable() override = default;

    void destroy(Render* render)
    {
        _tree.destroy(render);
        render->rc->dealloc(_nearest_sampler);
        render->rc->dealloc(_vertex_input);
        render->rc->dealloc(_indices_buffer);
        _height_precomputation.destroy(render->my);
    }

    struct RenderData
    {
        struct Bin
        {
            PatchGeometryInfo geometry_info;
            uint32_t instance_count;
            uint32_t render_bin_ubo_offset;
        };

        my::ResourceHandle vertex_input;
        my::ResourceHandle visual_shader;
        my::ResourceHandle feedback_shader;
        my::ResourceHandle picking_shader;
        my::ResourceHandle depth_shader;
        my::ResourceHandle selection_shader;
        uint32_t bin_count{};
        const Bin* bins{};
        my::ResourceHandle tessellation_texture;
        my::ResourceHandle render_bins_ubo;
        my::ResourceHandle nearest_sampler;
        my::ResourceHandle height_lut_texture;
        my::ResourceHandle normal_lut_texture;
        GeometryResources external_resources;
        hrz_proto::SceneViewIndex scene_view{};
        bool cast_shadows{};
    };

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* user_data_raw,
        const void* raw)
    {
        HRZ_SCOPED_SAMPLE("planet renderable render callback");

        const RenderData* data = (const RenderData*)raw;
        bool bind_imagery_rasters = false;

        const auto* user_data = (const hrz::SceneViewRenderGraphUserData*)user_data_raw;

        if (user_data->scene_view != data->scene_view) return;

        my::ResourceHandle shader;
        switch (render_type)
        {
            case hrz::RenderVisual:
                shader = data->visual_shader;
                bind_imagery_rasters = true;
                break;
            case hrz::RenderPlanetFeedback: shader = data->feedback_shader; break;
            case hrz::RenderPicking: shader = data->picking_shader; break;
            case hrz::RenderShadows:
            {
                if (!data->cast_shadows) return;
                shader = data->depth_shader;
                break;
            }
            case hrz::RenderViewshed: shader = data->depth_shader; break;
            case hrz::RenderSelection: shader = data->selection_shader; break;
            default: return;
        }

        rb->push_state();

        my::TextureBinding texture_bindings[] = {
            {SamplerTessellation, data->tessellation_texture, data->nearest_sampler},
            {SamplerHeightLut, data->height_lut_texture, data->nearest_sampler},
            {SamplerNormalLut, data->normal_lut_texture, data->nearest_sampler},
        };
        rb->bind(texture_bindings);
        rb->bind({&data->external_resources.planet_params, 1});

        if (bind_imagery_rasters)
        {
            rb->bind(data->external_resources.imagery_indirection);
            rb->bind(data->external_resources.imagery_atlas);
        }

        uint32_t first_instance = 0;
        for (uint32_t i = 0; i < data->bin_count; ++i)
        {
            const auto& bin = data->bins[i];

            my::UboBinding ubo_binding{
                UboRenderBinData, data->render_bins_ubo, bin.render_bin_ubo_offset,
                sizeof(PatchTree::RenderBinData)
            };
            rb->bind({&ubo_binding, 1});

            auto state = rb->get_current_state();

            auto info =
                my::DrawBatchInfo(my::PrimitiveType::TriangleList, bin.geometry_info.index_count)
                    .indexed(my::IndexType::UShort, bin.geometry_info.first_index)
                    .instanced(bin.instance_count, first_instance);

            r->draw(info, shader, data->vertex_input, state.ubos, state.textures);

            first_instance += bin.instance_count;
        }

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler&) const override
    {
        assert(!_tree.patch_bins.empty());
        my::Renderer::BinMask bin_mask = hrz::RenderPlanetBin
            | (_transparent_terrain ? hrz::RenderWorldTransparentBin : hrz::RenderWorldOpaqueBin);

        RenderData data;
        data.visual_shader = _transparent_terrain ? _visual_transparent_shader : _visual_shader;
        data.feedback_shader = _feedback_shader;
        data.depth_shader = _depth_shader;
        data.picking_shader = _picking_shader;
        data.selection_shader = _selection_shader;

        data.bin_count = (uint32_t)_tree.patch_bins.size();
        auto bins = queue.alloc_n_uninit<RenderData::Bin>(_tree.patch_bins.size());
        for (size_t i = 0; i < _tree.patch_bins.size(); ++i)
        {
            auto& dst = bins[i];
            const auto& src = _tree.patch_bins[i];

            dst.geometry_info = _patch_geometry_info[src.patch_type];
            dst.instance_count = src.patch_count;
            dst.render_bin_ubo_offset = src.render_data_offset;
        }
        data.bins = bins.data();

        data.vertex_input = _vertex_input;
        data.tessellation_texture = _tree.geometry_texture.get_for_gpu();
        data.render_bins_ubo = _tree.render_bin_data.get_for_gpu();
        data.nearest_sampler = _nearest_sampler;
        data.external_resources = _external_resources;
        data.height_lut_texture = _height_precomputation._height_lut;
        data.normal_lut_texture = _height_precomputation._normal_lut;
        data.external_resources.planet_params.index = UboPlanetParams;
        data.scene_view = _scene_view;
        data.cast_shadows = _cast_shadows;

        for (int i = 0; i < HRZ_S_MAX_IMAGERY_GROUP_COUNT; ++i)
        {
            data.external_resources.imagery_indirection[i].index = sampler_imagery_indirection(i);
            data.external_resources.imagery_atlas[i].index = sampler_imagery_atlas(i);
        }

        queue.enqueue(bin_mask, render_callback, data, lm::dvec3(0, 0, 0), hrz::EARTH_RADIUS * 2);
    }

    void update(
        const my::Renderer::Culler& culler,
        my::Renderer::ViewId main_view_id,
        my::Renderer::ViewMask main_views,
        bool use_adaptive_resolution,
        double height_above_terrain,
        const std::pair<double, double>& dtm_min_max,
        bool dtm_min_max_has_changed)
    {
        HRZ_SCOPED_SAMPLE("planet collect render info");

        lm::dmat4 view_matrix = culler.get_view(main_view_id).view;
        lm::dvec3 eye_pos = culler.get_eye_point(main_view_id);

        if (use_adaptive_resolution)
        {
            // We simulate the camera being above the ellipsoid at the same altitude as the camera
            // is above the terrain right now. So if the camera is at 2010m but the terrain below is
            // at 2000m, we simulate the camera being 10m + a little offset above the terrain. That
            // way we keep a high resolution when the camera is close to the terrain even at high
            // altitudes. The offset makes sure we don't refine too much. All of this is very much
            // experimental.
            auto eye_pos_geo = hrz::ecef_to_geo3(eye_pos);
            eye_pos_geo.alt = height_above_terrain + 50;
            eye_pos = hrz::geo_to_ecef(eye_pos_geo);
        }
        else
        {
            auto eye_pos_geo = hrz::ecef_to_geo3(eye_pos);
            eye_pos_geo.alt = std::max(eye_pos_geo.alt, 50.0);
            eye_pos = hrz::geo_to_ecef(eye_pos_geo);
        }

        lm::dvec3 eye_pos_sphere = eye_pos;
        eye_pos_sphere.z /= hrz::WGS84_AXES_LENGTH_RATIO;

        double dist_to_center_sphere =
            std::max(lm::length(eye_pos_sphere), EARTH_RADIUS + dtm_min_max.first);
        lm::dvec3 horizon_plane_normal = lm::normalize(eye_pos_sphere);

        TreeTraverseCtx cull_ctx;
        cull_ctx.eye_pos = eye_pos;
        cull_ctx.dtm_min_max = dtm_min_max;
        cull_ctx.dtm_min_max_has_changed = dtm_min_max_has_changed;
        cull_ctx.culler = &culler;

        double cos = (EARTH_RADIUS + dtm_min_max.first) / dist_to_center_sphere;
        cull_ctx.horizon_cone_half_angle_cos_sin = lm::dvec2(cos, std::sqrt(1.0 - cos * cos));
        cull_ctx.horizon_cone_direction = horizon_plane_normal;

        cull_ctx.reserved_instance_count = RootPatchCount;
        cull_ctx.view_matrix = view_matrix;
        cull_ctx.terrain_res = _terrain_res;
        cull_ctx.subdivision = _subdivision;
        cull_ctx.main_views = main_views;

        _tree.traverse_culling(cull_ctx);
        _tree.traverse_render(cull_ctx);
    }

    RenderRequest work_gpu(Render* render)
    {
        RenderRequest render_request;

        if (_tree.to_render.size() > 0)
        {
            render_request |= _tree.do_data_uploads(render);
        }

        return render_request;
    }

    void draw(
        Render* render,
        const GeometryResources& resources,
        bool transparent,
        hrz_proto::SceneViewIndex scene_view_index,
        bool cast_shadows)
    {
        _transparent_terrain = transparent;
        _scene_view = scene_view_index;
        _cast_shadows = cast_shadows;

        if (_tree.to_render.size() > 0)
        {
            _external_resources = resources;
            render->rd->collect_renderable(*this);

            my::TextureBinding tessellation_binding = {
                0, _tree.geometry_texture.get_for_gpu(), _nearest_sampler
            };

            _height_precomputation.draw(
                render, resources.dtm_atlas, resources.dtm_indirection, tessellation_binding, {},
                resources.planet_params, _tree.patch_bins, _tree.precompute_bin_data.get_for_gpu());
        }
    }

    void set_terrain_resolution(double res) { _terrain_res = res; }
};

void collect_shaders(hrz::GpuResourceContext* rc)
{
    PlanetRenderable::collect_visual_shaders(rc);
    PlanetRenderable::collect_feedback_shaders(rc);
    PlanetRenderable::collect_depth_shaders(rc);
    PlanetRenderable::collect_picking_shaders(rc);
    PlanetRenderable::collect_selection_shaders(rc);
    HeightPrecomputation::collect_shaders(rc);
}

} // namespace planet

struct PlanetGeometry
{
    enum class FeedbackStatus
    {
        Idle,
        FeedbackScheduled,
        FeedbackDownloaded,
        SavingToBlob,
        SavedToBlob,
        JobScheduled
    };

    bool disabled = !get_flag(Flag::EnableTerrain);

    std::unique_ptr<planet::PlanetRenderable> renderable;

    std::unique_ptr<FeedbackPass> feedback_pass;
    FeedbackStatus feedback_status = FeedbackStatus::Idle;
    hrz_jobs::ProcessFeedbackTextureTicket feedback_job_ticket;
    bool feedback_requested = false;
    double last_feedback_start_ms = hrz::clock::CurrentFrameRealTime.ms;
    uint64_t feedback_texture_download_id = 0;
    std::unique_ptr<char[]> last_feedback_data;
    lm::uvec2 last_feedback_size;
    blobs::AllocationTicket feedback_image_allocation_ticket;
    BlobImage last_feedback_image;

    bool requested_tiles_updated = false;
    std::vector<planet::RequestedTileCoords> requested_tiles;
    size_t requested_tiles_hash{};

    bool model_updated = false;
    hrz_proto::SceneViewIndex model_scene_view_index{};

    bool is_opaque = true;
    bool use_adaptive_resolution = false;

    static constexpr double DTM_STEP = 500.0; // meters
    std::pair<double, double> discrete_dtm_min_max{0.0, DTM_STEP};
    bool dtm_min_max_updated = true;

    my::RenderPassId initialize_rendering(RenderView* render)
    {
        if (!disabled)
        {
            renderable.reset(new planet::PlanetRenderable(render));
        }

        feedback_pass.reset(new FeedbackPass());
        return render->rg->add_pass("planet feedback", feedback_pass.get());
    }

    void destroy(JobScheduler* js, Render* render)
    {
        if (feedback_status == FeedbackStatus::FeedbackScheduled)
        {
            render->my->cancel_texture_download(feedback_texture_download_id);
            render::release_texture_download_id(feedback_texture_download_id);
        }
        else if (feedback_status == FeedbackStatus::SavingToBlob)
        {
            feedback_image_allocation_ticket.cancel();
        }
        else if (feedback_status == FeedbackStatus::JobScheduled)
        {
            hrz_jobs::cancel_job(js, feedback_job_ticket);
        }

        if (renderable)
        {
            renderable->destroy(render);
        }

        feedback_pass->destroy(render->my);
    }

    RenderRequest work_gpu(
        Render* render,
        my::Renderer::ViewId main_view_id,
        double height_above_terrain)
    {
        if (disabled) return {};

        RenderRequest render_request;

        double now = hrz::clock::CurrentFrameRealTime.ms;
        if (feedback_status == FeedbackStatus::Idle && feedback_requested
            && now - last_feedback_start_ms >= FeedbackDelayMs)
        {
            assert(feedback_job_ticket.ticket == 0);
            feedback_texture_download_id = render::acquire_texture_download_id();
            feedback_pass->schedule(feedback_texture_download_id);
            feedback_status = FeedbackStatus::FeedbackScheduled;
            feedback_requested = false;
            last_feedback_start_ms = now;

            render_request.request_planet_feedback_render();
        }
        else if (
            feedback_status == FeedbackStatus::FeedbackScheduled
            && render->my->is_texture_download_ready(feedback_texture_download_id))
        {
            // @Todo Download directly to blob.
            auto data = render->my->retrieve_texture_download(feedback_texture_download_id);
            assert(data.format == my::TextureFormat::RGBA32UI);

            if (data.data != nullptr)
            {
                last_feedback_data.swap(data.data);
                render::release_texture_download_id(feedback_texture_download_id);
                last_feedback_size = feedback_pass->last_feedback_size();
                feedback_status = FeedbackStatus::FeedbackDownloaded;
            }
            else
            {
                HRZ_LOG_ERROR("Could not retrieve feedback data");
                feedback_status = FeedbackStatus::Idle;
            }
        }

        renderable->update(
            render->rd->as_culler(), main_view_id, render->main_views, use_adaptive_resolution,
            height_above_terrain, discrete_dtm_min_max, std::exchange(dtm_min_max_updated, false));
        render_request |= renderable->work_gpu(render);

        return render_request;
    }

    void draw(Render* render, const planet::GeometryResources& resources, bool cast_shadows)
    {
        HRZ_SCOPED_SAMPLE("planet geometry draw");

        if (!disabled)
            renderable->draw(render, resources, !is_opaque, model_scene_view_index, cast_shadows);
    }

    void work(
        BlobAllocator* ba,
        JobScheduler* js,
        SceneModel* model,
        const vtex::ClipmapParams* clipmap_params,
        const std::pair<double, double>& dtm_min_max,
        const RenderViewInfo& camera_info,
        double mipmap_bias)
    {
        HRZ_SCOPED_SAMPLE("planet geometry work");

        if (disabled) return;

        if (model_updated)
        {
            auto settings = hrz_proto::SceneViewSettingsPathBuilder<SceneModelAccessor>(
                                model, model_scene_view_index)
                                .terrain()
                                .get();

            is_opaque = settings.terrain_opacity() >= 1.0F;
            use_adaptive_resolution = settings.experimental_adaptive_resolution();
            model_updated = false;
        }

        if (feedback_status == FeedbackStatus::FeedbackDownloaded)
        {
            size_t data_size = last_feedback_size.x * last_feedback_size.y * sizeof(float) * 4;
            feedback_image_allocation_ticket = blobs::allocate_blob(ba, data_size);
            blobs::register_owner(
                ba, feedback_image_allocation_ticket, {monitoring::systems::PlanetGeometry});
            feedback_status = FeedbackStatus::SavingToBlob;
        }

        if (feedback_status == FeedbackStatus::SavingToBlob)
        {
            auto state = blobs::get_state(ba, feedback_image_allocation_ticket);
            if (state == blobs::BlobState::Allocated)
            {
                auto blob = blobs::to_blob(ba, feedback_image_allocation_ticket);
                auto blob_data = blob.get_mutable_data();
                std::memcpy(blob_data.data(), last_feedback_data.get(), blob_data.size());
                blob_data.release();
                last_feedback_image = BlobImage::make(
                    my::TextureFormat::RGBA32UI, last_feedback_size.x, last_feedback_size.y, blob,
                    ba);
                last_feedback_image.register_blob_metadata(
                    ba, "image type"_ss, "planet feedback data"_ss);
                last_feedback_image.register_blob_owner(ba, {monitoring::systems::PlanetGeometry});

                last_feedback_data.reset();

                feedback_status = FeedbackStatus::SavedToBlob;
            }
            else if (state == blobs::BlobState::Error)
            {
                HRZ_LOG_ERROR("Could not allocate blob for feedback data");
                blobs::cancel(ba, feedback_image_allocation_ticket);
                last_feedback_data.reset();
                feedback_status = FeedbackStatus::Idle;
            }
        }

        if (feedback_status == FeedbackStatus::SavedToBlob)
        {
            HRZ_SCOPED_SAMPLE("start feedback job");

            assert(feedback_job_ticket.ticket == 0);

            hrz_jobs::FeedbackData feedback_data;
            feedback_data.image = std::move(last_feedback_image);

            for (lm::uvec2 offset : clipmap_params->get_offsets())
            {
                feedback_data.clipmap_offsets.push_back(offset);
            }

            feedback_job_ticket = hrz_jobs::add_job_process_feedback_texture(
                js, std::move(feedback_data), {monitoring::systems::PlanetGeometry});
            feedback_status = FeedbackStatus::JobScheduled;
        }

        requested_tiles_updated = false;

        if (feedback_status == FeedbackStatus::JobScheduled
            && hrz_jobs::is_job_finished(js, feedback_job_ticket))
        {
            assert(feedback_status == FeedbackStatus::JobScheduled);
            HRZ_SCOPED_SAMPLE("receive feedback job result");

            auto response = hrz_jobs::get_job_response(js, feedback_job_ticket);
            feedback_job_ticket.ticket = 0;

            feedback_status = FeedbackStatus::Idle;

            requested_tiles.clear();
            requested_tiles_hash = 0;

            // Add the tile directly below (or above) the camera.
            // This allows refining the DTM at the camera's location, which in turn enables
            // moving the camera above ground if needed.
            {
                auto camera_geo = hrz::ecef_to_geo2(camera_info.cam_view_info.cam.pos);

                // metres per pixel
                double resolution = (std::abs(camera_info.height_above_terrain)
                                     * std::tan(camera_info.cam_view_info.cam.fovy / 2.0))
                    / (camera_info.cam_view_info.viewport.size.y / 2.0);
                double level0_resolution =
                    (hrz::EARTH_CIRCUMFERENCE / hrz::MERCATOR_TILE_SIZE) * std::cos(camera_geo.lat);

                double level0_lod = std::log2(level0_resolution);
                double camera_lod = -std::log2(resolution) + level0_lod - mipmap_bias;

                const auto camera_tile_coords =
                    hrz::geo_to_mercator_tile(camera_geo, (uint8_t)std::round(camera_lod));

                if (camera_tile_coords.has_value())
                {
                    requested_tiles.push_back(
                        planet::RequestedTileCoords{
                            camera_tile_coords.value(), std::numeric_limits<uint32_t>::max(),
                            planet::TileRequestOrigin::CameraVerticalProjectionOrigin
                        });
                    requested_tiles_hash = hrz::hash_mix(
                        requested_tiles_hash, hrz::hash_value(camera_tile_coords.value()));
                }
            }

            for (const auto& tile : response.tile_usage)
            {
                TileCoords coords = tile.coords;
                coords = clipmap_params->clipmap_to_source_tile(coords);

                if (coords.lod == hrz::vtex::ClipmapParams::NoLod) continue;

                requested_tiles.push_back(
                    planet::RequestedTileCoords{coords, tile.uses, tile.origin});
                requested_tiles_hash = hrz::hash_mix(requested_tiles_hash, hrz::hash_value(coords));
            }

            requested_tiles_updated = true;
        }

        auto discretize_to_step = [](double value, double step)
        {
            double q = value / step;
            if (value < 0.0)
            {
                q = std::floor(q);
            }
            else
            {
                q = std::ceil(q);
            }
            return q * step;
        };

        // Because we recompute the patches' bounding boxes when the DTM min/max changes,
        // we discretise it to reduce the number of recomputations.
        std::pair<double, double> new_discrete_dtm_min_max = {
            discretize_to_step(dtm_min_max.first, DTM_STEP),
            discretize_to_step(dtm_min_max.second, DTM_STEP)
        };
        new_discrete_dtm_min_max.first = std::min(new_discrete_dtm_min_max.first, 0.0);
        new_discrete_dtm_min_max.second = std::max(new_discrete_dtm_min_max.second, DTM_STEP);
        if (new_discrete_dtm_min_max != discrete_dtm_min_max)
        {
            discrete_dtm_min_max = new_discrete_dtm_min_max;
            dtm_min_max_updated = true;
        }
    }

    bool is_working() const
    {
        return !disabled && (feedback_requested || feedback_status != FeedbackStatus::Idle);
    }

    void request_feedback_render() { feedback_requested = true; }
}; // namespace hrz

namespace planet
{

PlanetGeometry* create_geometry()
{
    return new PlanetGeometry();
}

void destroy(PlanetGeometry* planet, JobScheduler* js, Render* render)
{
    assert(planet && js && render);
    planet->destroy(js, render);
    delete planet;
}

my::RenderPassId initialize_rendering(PlanetGeometry* planet, RenderView* render)
{
    assert(planet);
    return planet->initialize_rendering(render);
}

void work(
    PlanetGeometry* planet,
    BlobAllocator* ba,
    JobScheduler* js,
    SceneModel* model,
    const vtex::ClipmapParams* clipmap_params,
    const std::pair<double, double>& dtm_min_max,
    const RenderViewInfo& camera_info,
    double mipmap_bias)
{
    assert(planet && ba && js && model && clipmap_params);
    planet->work(ba, js, model, clipmap_params, dtm_min_max, camera_info, mipmap_bias);
}

RenderRequest work_gpu(
    PlanetGeometry* planet,
    Render* render,
    my::Renderer::ViewId main_view_id,
    double height_above_terrain)
{
    assert(planet);
    return planet->work_gpu(render, main_view_id, height_above_terrain);
}

bool is_working(const PlanetGeometry* planet)
{
    assert(planet);
    return planet->is_working();
}

void draw(
    PlanetGeometry* planet,
    Render* render,
    const GeometryResources& resources,
    bool cast_shadows)
{
    assert(planet);
    planet->draw(render, resources, cast_shadows);
}

void set_resolution(PlanetGeometry* planet, double resolution)
{
    assert(planet);
    planet->renderable->set_terrain_resolution(resolution);
}

RequestedTiles get_updated_requested_tiles(const PlanetGeometry* planet)
{
    assert(planet);
    return {planet->requested_tiles, planet->requested_tiles_hash, planet->requested_tiles_updated};
}

void notify_model_update(
    PlanetGeometry* planet,
    scene_model::UpdateType update_type,
    const scene_model::SceneViewSettingsPath& path)
{
    assert(planet);
    assert(path.valid());

    if (path.leaf() || path.is_terrain())
    {
        planet->model_updated = true;
        planet->model_scene_view_index = path.get_root();
    }
}

void request_feedback_render(PlanetGeometry* planet)
{
    assert(planet);
    planet->request_feedback_render();
}

} // namespace planet
} // namespace hrz
