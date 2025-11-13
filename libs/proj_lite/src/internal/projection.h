#pragma once

#include "internal/string_span.h"
#include "proj_lite.h"

#include <assert.h>

#define PL_CHECK_PROJ_DATA_OPAQUE_SIZE(STRUCT) \
    assert(sizeof(STRUCT) <= sizeof(pl_ProjectionDataOpaque))

#define PL_REG_PROJ_PARAM(PROJ_ID) pl_reg_##PROJ_ID##_param
#define PL_BAKE_FWD_PROJ(PROJ_ID) pl_bake_##PROJ_ID##_forward
#define PL_BAKE_INV_PROJ(PROJ_ID) pl_bake_##PROJ_ID##_inverse

#define PL_REG_PROJ_PARAM_FN(PROJ_ID) \
    bool pl_reg_##PROJ_ID##_param(pl_ProjectionParams* p, pl_StringSpan param, pl_StringSpan value)
#define PL_BAKE_FWD_PROJ_FN(PROJ_ID)       \
    pl_Result pl_bake_##PROJ_ID##_forward( \
        const pl_Crs* crs, pl_ProjectionDataOpaque* opq, pl_TransformFn* fn)
#define PL_BAKE_INV_PROJ_FN(PROJ_ID)       \
    pl_Result pl_bake_##PROJ_ID##_inverse( \
        const pl_Crs* crs, pl_ProjectionDataOpaque* opq, pl_TransformFn* fn)

typedef pl_Result (
    *pl_BakeProjectionFn)(const pl_Crs* crs, pl_ProjectionDataOpaque* opq, pl_TransformFn* fn);

typedef bool (*pl_RegisterProjectionParamFn)(
    pl_ProjectionParams* p,
    pl_StringSpan param,
    pl_StringSpan value);

typedef struct pl_Projection
{
    const char* name;
    pl_RegisterProjectionParamFn param_fn;
    pl_BakeProjectionFn bake_fwd;
    pl_BakeProjectionFn bake_inv;
} pl_Projection;

const pl_Projection* pl_find_projection(pl_StringSpan ref);
const pl_Projection* pl_get_projection(pl_ProjectionType type);
pl_ProjectionType pl_find_projection_type(pl_StringSpan ref);
