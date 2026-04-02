#pragma once

#include "hrz/common/geo.h"
#include "hrz/protocol/geo/coords.pb.h"

namespace hrz
{

inline GeoPosition3 from_proto(const hrz_proto::GeographicPosition& model)
{
    GeoPosition3 p;
    p.lat = lm::radians(model.latitude());
    p.lon = lm::radians(model.longitude());
    p.alt = model.altitude();

    return normalize(p);
}

inline hrz_proto::GeographicPosition to_proto(GeoPosition3 p)
{
    p = normalize(p);

    hrz_proto::GeographicPosition pos;
    pos.set_altitude(p.alt);
    pos.set_longitude(lm::degrees(p.lon));
    pos.set_latitude(lm::degrees(p.lat));

    return pos;
}

inline GeoBounds from_proto(const hrz_proto::GeographicBounds& bounds)
{
    GeoBounds b;
    b.west = lm::radians(bounds.west());
    b.east = lm::radians(bounds.east());
    b.south = lm::radians(bounds.south());
    b.north = lm::radians(bounds.north());

    return normalize(b);
}

inline void to_proto(const GeoVolumeBounds& bounds, hrz_proto::GeographicVolumeBounds* proto)
{
    proto->set_west(lm::degrees(bounds.west));
    proto->set_south(lm::degrees(bounds.south));
    proto->set_east(lm::degrees(bounds.east));
    proto->set_north(lm::degrees(bounds.north));
    proto->set_min_height(bounds.min_height);
    proto->set_max_height(bounds.max_height);
}

inline GeoVolumeBounds from_proto(const hrz_proto::GeographicVolumeBounds& bounds)
{
    GeoVolumeBounds b;
    b.west = lm::radians(bounds.west());
    b.east = lm::radians(bounds.east());
    b.south = lm::radians(bounds.south());
    b.north = lm::radians(bounds.north());
    b.min_height = bounds.min_height();
    b.max_height = bounds.max_height();

    return normalize(b);
}

} // namespace hrz
