#include "hrz/common/proto_settings.h"

#include "hrz/protocol/camera/settings.pb.h"
#include "hrz/protocol/scene/settings.pb.h"
#include "hrz/protocol/scene/view_settings.pb.h"

namespace hrz
{
void default_scene_view_settings(hrz_proto::SceneViewSettings* settings)
{
    settings->CopyFrom(hrz_proto::SceneViewSettings::default_instance());

    settings->set_camera(hrz_proto::CAMERA_0);

    settings->mutable_terrain()->set_terrain_opacity(1.0);
    settings->mutable_terrain()->mutable_terrain_color()->set_r(0.0);
    settings->mutable_terrain()->mutable_terrain_color()->set_g(0.0);
    settings->mutable_terrain()->mutable_terrain_color()->set_b(1.0);
    settings->mutable_terrain()->mutable_terrain_color()->set_a(1.0);
    settings->mutable_terrain()->set_clip_id(-1);
    settings->mutable_terrain()->mutable_lighting()->set_enable_lighting(true);
    settings->mutable_terrain()->mutable_lighting()->set_cast_shadows(true);
    settings->mutable_terrain()->mutable_lighting()->set_receive_shadows(true);

    auto ambient = settings->mutable_ambient();
    ambient->set_sun_ambient_balance(0.5);
    ambient->set_lighting_strength(1.0);
    ambient->set_wrap_lighting(0.0);
    ambient->mutable_sun()->set_mode(hrz_proto::SUN_LIGHTING_SIMULATED);
    ambient->mutable_ambient_lighting()->set_mode(hrz_proto::AMBIENT_LIGHTING_SIMULATED);
    ambient->mutable_sky()->set_mode(hrz_proto::SKY_SIMULATED);
    ambient->mutable_sky()->set_attenuation(0.5F);
    ambient->mutable_lighting()->set_enable_lighting(true);
    ambient->mutable_lighting()->set_cast_shadows(true);
    ambient->mutable_lighting()->set_receive_shadows(true);
    ambient->mutable_sun()->mutable_direction()->set_local_solar_time(15.0F);
    ambient->mutable_sun()->mutable_direction()->set_day_of_year(171.0F);
    ambient->mutable_sun()->mutable_static_color()->set_r(1.0F);
    ambient->mutable_sun()->mutable_static_color()->set_g(1.0F);
    ambient->mutable_sun()->mutable_static_color()->set_b(1.0F);
    ambient->mutable_sun()->mutable_static_color()->set_a(1.0F);
    ambient->mutable_ambient_lighting()->mutable_static_color()->set_r(0.42F);
    ambient->mutable_ambient_lighting()->mutable_static_color()->set_g(0.42F);
    ambient->mutable_ambient_lighting()->mutable_static_color()->set_b(0.42F);
    ambient->mutable_ambient_lighting()->mutable_static_color()->set_a(1.0F);
    ambient->mutable_sky()->mutable_static_atmosphere_color()->set_r(0.80F);
    ambient->mutable_sky()->mutable_static_atmosphere_color()->set_g(0.89F);
    ambient->mutable_sky()->mutable_static_atmosphere_color()->set_b(0.92F);
    ambient->mutable_sky()->mutable_static_atmosphere_color()->set_a(1.0F);
    ambient->mutable_sky()->mutable_static_space_color()->set_r(0.0F);
    ambient->mutable_sky()->mutable_static_space_color()->set_g(0.0F);
    ambient->mutable_sky()->mutable_static_space_color()->set_b(0.0F);
    ambient->mutable_sky()->mutable_static_space_color()->set_a(1.0F);
    ambient->mutable_sky()->set_static_color_transition_start_distance(15000);
    ambient->mutable_sky()->set_static_color_transition_end_distance(60000);
    ambient->mutable_sky()->set_static_color_transition_distance_unit(
        hrz_proto::StaticSkyColorTransitionUnit::STATIC_SKY_COLOR_TRANSITION_UNIT_METERS);
    ambient->mutable_underground_color()->set_r(0.45F);
    ambient->mutable_underground_color()->set_g(0.44F);
    ambient->mutable_underground_color()->set_b(0.27F);
    ambient->mutable_underground_color()->set_a(1.0F);

    ambient->mutable_primary_fog()->set_density(1.0);
    ambient->mutable_primary_fog()->set_start_distance(1000.0);
    ambient->mutable_primary_fog()->set_falloff_start(0.0);
    ambient->mutable_primary_fog()->set_falloff_end(300.0);
    ambient->mutable_primary_fog()->set_apply_to_sky(true);
    ambient->mutable_primary_fog()->mutable_color()->set_r(1.0);
    ambient->mutable_primary_fog()->mutable_color()->set_g(1.0);
    ambient->mutable_primary_fog()->mutable_color()->set_b(1.0);
    ambient->mutable_primary_fog()->mutable_color()->set_a(0.0);
    ambient->mutable_secondary_fog()->set_density(2.0);
    ambient->mutable_secondary_fog()->set_start_distance(300.0);
    ambient->mutable_secondary_fog()->set_falloff_start(0.0);
    ambient->mutable_secondary_fog()->set_falloff_end(300.0);
    ambient->mutable_secondary_fog()->set_apply_to_sky(true);
    ambient->mutable_secondary_fog()->mutable_color()->set_r(1.0);
    ambient->mutable_secondary_fog()->mutable_color()->set_g(1.0);
    ambient->mutable_secondary_fog()->mutable_color()->set_b(1.0);
    ambient->mutable_secondary_fog()->mutable_color()->set_a(0.0);

    auto highlight = settings->mutable_highlight();
    highlight->mutable_selection_color()->set_r(1.0F);
    highlight->mutable_selection_color()->set_g(0.5F);
    highlight->mutable_selection_color()->set_b(0.0F);
    highlight->mutable_selection_color()->set_a(0.2F);
    highlight->set_selection_outline_alpha(1.0);
    highlight->set_selection_outline_size(2.5);
    highlight->set_selection_occlusion_alpha(0.25);
    highlight->mutable_mouse_hover_highlight_color()->set_r(0.2F);
    highlight->mutable_mouse_hover_highlight_color()->set_g(0.9F);
    highlight->mutable_mouse_hover_highlight_color()->set_b(0.9F);
    highlight->mutable_mouse_hover_highlight_color()->set_a(0.5F);

    auto viewshed = settings->mutable_viewshed();
    viewshed->mutable_position()->set_altitude(0.0);
    viewshed->mutable_position()->set_latitude(0.0);
    viewshed->mutable_position()->set_longitude(0.0);
    viewshed->set_enable_viewshed(false);
    viewshed->set_enable_wireframe(false);
    viewshed->set_hfov(70);
    viewshed->set_aspect_ratio(2.0);
    viewshed->set_max_distance(100);
    viewshed->set_start(0.01F);
    viewshed->set_bearing(0);
    viewshed->set_tilt(0);

    viewshed->mutable_visible_color()->set_r(0.5);
    viewshed->mutable_visible_color()->set_g(1.0);
    viewshed->mutable_visible_color()->set_b(0.5);
    viewshed->mutable_visible_color()->set_a(0.5);

    viewshed->mutable_hidden_color()->set_r(1.0);
    viewshed->mutable_hidden_color()->set_g(0.5);
    viewshed->mutable_hidden_color()->set_b(0.5);
    viewshed->mutable_hidden_color()->set_a(0.5);

    auto viewport = settings->mutable_viewport();
    viewport->mutable_viewport()->set_x_min(0.0F);
    viewport->mutable_viewport()->set_y_min(0.0F);
    viewport->mutable_viewport()->set_x_max(1.0F);
    viewport->mutable_viewport()->set_y_max(1.0F);
    viewport->mutable_scissor()->set_x_min(0.0F);
    viewport->mutable_scissor()->set_y_min(0.0F);
    viewport->mutable_scissor()->set_x_max(1.0F);
    viewport->mutable_scissor()->set_y_max(1.0F);
}

void default_scene_settings(hrz_proto::SceneSettings* settings)
{
    settings->CopyFrom(hrz_proto::SceneSettings::default_instance());

    settings->set_main_view(hrz_proto::SCENE_VIEW_0);
    settings->mutable_active_views()->set_bits(1 << hrz_proto::SCENE_VIEW_0);
    settings->mutable_raster()->set_compensate_inclination(true);
    settings->mutable_raster()->set_mix_lods(true);
    settings->mutable_raster()->set_max_screen_space_error(2.0F);
}

void default_camera_settings(hrz_proto::CameraSettings* settings)
{
    settings->CopyFrom(hrz_proto::CameraSettings::default_instance());

    settings->set_fovy(70.0);
    settings->set_user_controls_inertia(0.06F);
    settings->set_movements_inertia(0.06F);
    settings->set_min_height_above_terrain(2.0);
    settings->set_terrain_collision_inertia(0.06F);
}
} // namespace hrz
