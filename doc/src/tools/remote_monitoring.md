# Remote monitoring

## Overview

A dedicated tool can be used for monitoring Horizon integrated in any application. It displays profiling data, the different metrics collected by Horizon, as well as the different GPU resources it uses.

It can be built with the Bazel target `//tools/monitoring_client`.

## Receiving data from Horizon

All data is collected by Horizon, the application itself only receives and displays it. The application must first connect to Horizon: open the developer menu, open the `Remote monitoring` category, then select `Connect...`. If the monitoring application is opened and the default parameters have not been changed, the connection should be done. The `Server` window of the application shows the current state of the WebSocket server used for communication, and whether a version of Horizon has connected or not.

If the connection isn't established, make sure the address of the WebSocket server created by the application is correct in the developer menu. You can also try changing the port used by the monitoring application. To do so, close the current WebSocket server (`File` > `Stop server`) then start a new one (`File` > `Start monitoring server...`). A dialog can be used to change the port.

By default, Horizon collects no data, so nothing will appear on the application, even after the connection has been established. To enable data collection, use the developer menu in Horizon:
- **Profiling**: `Monitoring` > `Monitoring` > `Profiling enabled`
- **Metrics**: `Monitoring` > `Monitoring` > `Metrics enabled`
- **GPU passes** (sent as metrics): `Monitoring` > `GPU performance` > `Enable GPU profiling`
- **GPU resources snapshot**: `Remote monitoring` > `GPU resources snapshot`

## Visualisation tools overview

- **Timeline**: Show profiling and metrics data on the same timeline. Use the upper left `+` button to add a metric to the timeline. You can double click on a sample to focus on it, or right click to open a context menu with more actions.
- **Sample Inspector**: Show all functions called by Horizon and the time spent in each. You can order the tables by clicking on a column header, and see the corresponding function call in the timeline by double clicking on one of the right table entries.
- **GPU Treemap**: Show the resources allocated on the GPU in a 2D map. Like the timeline, you can zoom, scroll, focus, and right click to open a context menu. You can also filter the resources by type, system, or metadata.
- **GPU Comparator**: Compare the resources allocated between two different GPU snapshots. You can also use it to visualize one snapshot as a tree.
- **Frame Graph**: Show the duration of every frame registered by Horizon, optionally filtered by associated render type. You can also select a metric to display its values instead.
- **Metrics Pie**: Compare the values of different metrics during a given frame on a pie chart. The frame can also be selected by double-clicking somewhere on the frame graph.
- **Histogram**: Show the special "histogram" metrics collected by Horizon.

## Data storage

The monitoring application only stores the data associated to a *single* monitoring session. If the connection between the two application ends one way or another, the monitoring session is over, and no data can be appended to the session. The WebSocket server used for communication also shuts down.

At this point, you are free to browse through the received data, and save it to the disk or not. To start monitoring Horizon again, you have to start a new WebSocket server, which requires clearing all of the application's data to start a new monitoring session (the application will warn you if you try to do so).

## User data

User data is automatically saved when closing the application, and automatically loaded at launch. It is stored as a JSON file:
- `C:\Users\[username]\AppData\Roaming\Horizon Monitoring\userdata.json` on Windows
- `/home/[username]/.config/hrz_monitoring/userdata.json` on Linux

### Application layouts

The `View` menu can be used to customize which widgets are shown (they are all shown by default). The different windows of the application can also be moved, docked and resized as wanted (docking icons can get in the way when moving windows - hold `Shift` to hide them).

Specific layouts of the application can be saved and loaded, using the `Layout` menu. Saving a layout consists of saving the position, size, and visibility of the different windows. To save a layout, use `Layout` > `Save preset...`, then enter a unique name for the layout. It will then appear in the `Layout` menu as a new entry: clicking it will rearrange the windows to match the corresponding layout.

### Filter presets

It is possible to filter the GPU resources displayed when viewing the `GPU treemap` in the application. Such filters can be saved and loaded as presets, just like the layout of the application.

