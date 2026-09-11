// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "internal/projection.h"

static int fwd(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* x,
    double* y,
    double* z,
    int stride)
{
    (void)opq;
    (void)x;
    (void)y;
    (void)z;
    (void)stride;
    return n;
}

PL_BAKE_FWD_PROJ_FN(latlong)
{
    *fn = fwd;
    return pl_Result_Ok;
}

PL_BAKE_INV_PROJ_FN(latlong)
{
    *fn = fwd;
    return pl_Result_Ok;
}
