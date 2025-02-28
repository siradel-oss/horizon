---
Title: Monitoring the engine
Category: General
---

## Collecting monitoring information

Horizon is capable of profiling different aspects of its execution.

Profiling features can be accessed using either through the development UI (when it has been enabled in the [viewer options](HrzProtocol.ViewerOptions.html)) under `Monitoring` > `Remote monitoring`, or by using the methods of [the monitoring service](HrzProtocol.MonitoringService.html) from the API.

- Profiling tracks the call stack of the engine at any point of time. It can be enabled or disabled.
- Metrics track the evolution of different values at any point of time. They can be enabled or disabled.
- GPU memory snapshots shows the usage of GPU memory at a set point in time. They need to be queried explicitly.
- Blob allocator snapshots shows the usage of shared memory between the different threads run by the engine. They need to be queried explicitly.

## Visualizing monitoring information

The information related to the features listed above is not kept in the engine's memory for long. As such, it is not possible to visualize it using the development UI. Instead, use one of the following solutions:

### Using the dedicated monitoring application

This method is fast and uses a custom GUI, which offers different visualization tools.

Horizon can connect to its dedicated monitoring application via WebSockets using the development UI (under `Monitoring` > `Remote monitoring` > `Connect`). It allows to easily visualize all of the information collected by the engine. See the [artifacts](artifacts.html) page for a prebuilt version, or compile from source from `//tools/monitoring_client`.

### Using the message queue

This method works through code and can be used to collect the monitoring data without using the GUI, which is useful for automated processes such as performance testing.

Horizon can be asked to send the monitoring information through the message queue with the [`EnableMonitoringMessages` method of `MonitoringService`](HrzProtocol.MonitoringService.html#method-EnableMonitoringMessages). When that option is enabled, messages with a [`MonitoringDataMessage`](HrzProtocol.TypedMessage.html) payload will be regularly sent to the client.

The format is described in [[MonitoringDataMessage]]. The WebSocket interface uses the same format.

The `HrzMonitoringProtocol.MonitoringMessage` type is defined separately from Horizon's main protocol definitions, and its `.proto` file is available in the source tree as `//hrz/monitoring/proto:hrz_monitoring_interface.proto`. TypeScript bindings are available through the `@siradel/horizon-monitoring-protocol` npm package.
