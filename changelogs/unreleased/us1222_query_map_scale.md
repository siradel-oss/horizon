# Added

* Added the `GetSceneViewScaleAndAltitude` method to [[CameraService]] to retrieve information about a scene view.
    * The [returned information](HrzProtocol.ViewScaleAltitude.html) is the scale on the view (in pixels per meter), and the relative and absolute altitude of the camera.
    * This can be used to implement a map scale on the viewer.
