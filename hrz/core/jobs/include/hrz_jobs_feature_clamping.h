#pragma once

#include <hrz_protocol_all.h>

#include <gsl/gsl-lite.hpp>

namespace hrz
{
class PointClampingGenerator
{
    gsl::span<const float> _clamps;
    float _feature_clamp;
    bool _per_vertex;
    bool _use_z;

public:
    PointClampingGenerator(
        gsl::span<const float> clamps,
        float feature_clamp,
        bool per_vertex,
        bool use_z) :
        _clamps(clamps), _feature_clamp(feature_clamp), _per_vertex(per_vertex), _use_z(use_z)
    {
    }

    inline double get_clamp_for_point(size_t point_index) const
    {
        if (_per_vertex)
        {
            return _clamps[point_index];
        }
        else
        {
            return _feature_clamp;
        }
    }

    inline double clamp_point(size_t point_index, double z) const
    {
        double clamp = get_clamp_for_point(point_index);
        if (!_use_z)
        {
            return clamp;
        }
        else
        {
            return z + clamp;
        }
    }
};

class FeatureClampingGenerator
{
    gsl::span<const float> _clamps;
    hrz_proto::VectorClampMode _mode;
    bool _use_z;

public:
    FeatureClampingGenerator(
        gsl::span<const float> clamps,
        const hrz_proto::VectorClamping& config) :
        _clamps(clamps), _mode(config.method()), _use_z(config.use_z())
    {
    }

    PointClampingGenerator for_feature(size_t feature_index, size_t first_point) const
    {
        switch (_mode)
        {
            case hrz_proto::VectorClampMode::NO_CLAMPING:
                return PointClampingGenerator({}, 0.0f, false, _use_z);
            case hrz_proto::VectorClampMode::ANCHOR:
                return PointClampingGenerator({}, _clamps[feature_index], false, _use_z);
            case hrz_proto::VectorClampMode::PER_VERTEX:
                return PointClampingGenerator(_clamps.subspan(first_point), 0.0f, true, _use_z);
            default: return PointClampingGenerator({}, 0.0f, false, false);
        }
    }
};

} // namespace hrz
