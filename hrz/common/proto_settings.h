#pragma once

namespace HrzProtocol
{

class SceneViewSettings;
class SceneSettings;
class CameraSettings;

} // namespace HrzProtocol

namespace hrz
{

void default_scene_view_settings(HrzProtocol::SceneViewSettings*);
void default_scene_settings(HrzProtocol::SceneSettings*);
void default_camera_settings(HrzProtocol::CameraSettings*);

} // namespace hrz
