#pragma once

#include <cstdint>

namespace hrz
{
struct RenderRequest
{
    using TypeBitset = uint8_t;

    enum class Type : TypeBitset
    {
        Visual = 0x01,
        Picking = 0x02,
        PlanetFeedback = 0x04,

        All = Visual | Picking | PlanetFeedback,
    };

    using VisualCauseBitset = uint8_t;

    // Monitoring::draw_frames() must be updated if this enum is modified.
    enum class VisualCause : VisualCauseBitset
    {
        Scene = 0x01,
        Animation = 0x02,
        FlatOverlayRefresh = 0x04,
        SymbolCulling = 0x08,
        VectorTileVisibilitySet = 0x10,
    };

private:
    enum Flags
    {
        SchedulePlanetFeedback = 0x08,
        ScheduleFlatOverlayRender = 0x10,
    };

    TypeBitset _flags{};
    VisualCauseBitset _visual_causes{};

public:
    static RenderRequest all()
    {
        RenderRequest rr;
        rr.request_all();
        return rr;
    }

    static RenderRequest visual(VisualCause cause = VisualCause::Scene)
    {
        RenderRequest rr;
        rr.request_visual_render(cause);
        return rr;
    }

    constexpr TypeBitset get_requested_render_types() const
    {
        return _flags & (TypeBitset)Type::All;
    }

    constexpr VisualCauseBitset get_visual_render_causes() const { return _visual_causes; }

    constexpr bool is_render_requested(Type type) const { return _flags & (TypeBitset)type; }

    constexpr bool is_any_render_requested() const { return get_requested_render_types() != 0; }

    constexpr bool is_visual_render_caused_by(VisualCause cause) const
    {
        return _visual_causes & (VisualCauseBitset)cause;
    }

    constexpr void request_visual_render(VisualCause cause = VisualCause::Scene)
    {
        _flags |= (TypeBitset)Type::Visual;
        _visual_causes |= (VisualCauseBitset)cause;
    }

    constexpr void request_picking_render() { _flags |= (TypeBitset)Type::Picking; }

    constexpr void request_planet_feedback_render() { _flags |= (TypeBitset)Type::PlanetFeedback; }

    constexpr void request_all(VisualCause cause = VisualCause::Scene)
    {
        _flags |= ~_flags;
        _visual_causes |= (VisualCauseBitset)cause;
    }

    constexpr void schedule_planet_feedback() { _flags |= SchedulePlanetFeedback; }

    constexpr void schedule_flat_overlay_render() { _flags |= ScheduleFlatOverlayRender; }

    constexpr bool is_planet_feedback_scheduled() const { return _flags & SchedulePlanetFeedback; }

    constexpr bool is_flat_overlay_render_scheduled() const
    {
        return _flags & ScheduleFlatOverlayRender;
    }

    constexpr void reset()
    {
        _flags = 0;
        _visual_causes = 0;
    }

    constexpr RenderRequest& operator|=(const RenderRequest& request)
    {
        _flags |= request._flags;
        _visual_causes |= request._visual_causes;
        return *this;
    }
};

} // namespace hrz
