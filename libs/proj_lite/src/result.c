// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "proj_lite.h"

#include <assert.h>

const char* pl_result_string(pl_Result result)
{
    switch (result)
    {
        case pl_Result_Ok: return "OK";
        case pl_Result_UnknownEllipsoid: return "Unknown ellipsoid";
        case pl_Result_UnknownDatumShiftType: return "Unknown data shift type";
        case pl_Result_UnknownDatum: return "Unknown datum";
        case pl_Result_UnknownUnit: return "Unknown unit";
        case pl_Result_UnknownPrimeMeridian: return "Unknown prime meridian";
        case pl_Result_UnknownProjection: return "Unknown projection";
        case pl_Result_InvalidAxis: return "Invalid axis";
        case pl_Result_UnknownNadgridsValue: return "Unknown NAD grid value";
        case pl_Result_LatitudeOutOfRange: return "Latitude out of range";
        case pl_Result_LatTsLargerThan90: return "Latitude of true scale out of range";
        case pl_Result_ProjectionError: return "Projection error";
        case pl_Result_NonConvInvMeriDist: return "Non-convergent inversion of meridional distance";
        case pl_Result_EllipsoidRequired: return "Ellipsoid required";
        case pl_Result_InvalidUtmZone: return "Invalid UTM zone";
        case pl_Result_ConicLatEqual: return "Equal conic latitudes";
        case pl_Result_Lat1Or2ZeroOr90: return "Conic latitude equal to 0 or 90 deg";
        case pl_Result_InvalidEccentricity: return "Invalid eccentricity";
        default: assert(false && "Unhandled case"); return "<invalid result code>";
    }
}
