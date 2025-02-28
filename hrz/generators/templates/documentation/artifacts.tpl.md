---
Title: Artifacts
Category: General
---

## Horizon

Those are the core components of Horizon and are all required to run the engine. See the [introduction](index.html) for more details on the architecture.

### Protocol

* [npm (TypeScript): `@siradel/horizon-protocol`]({{ npm_nexus_url }}/@siradel/horizon-protocol/-/horizon-protocol-{{ version + npm_qualifier_string }}.tgz)
* [Linux native package (C++)]({{ raw_nexus_url }}/cpp-protocol-linux-{{ version }}.tar.gz)
* [Windows native package (C++)]({{ raw_nexus_url }}/cpp-protocol-windows-{{ version }}.tar.gz)

### API

* [npm (TypeScript): `@siradel/horizon-api`]({{ npm_nexus_url }}/@siradel/horizon-api/-/horizon-api-{{ version + npm_qualifier_string }}.tgz)
* [Linux native package (C++)]({{ raw_nexus_url }}/cpp-api-linux-{{ version }}.tar.gz)
* [Windows native package (C++)]({{ raw_nexus_url }}/cpp-api-windows-{{ version }}.tar.gz)

### Core

* [npm (TypeScript): `@siradel/horizon-core`]({{ npm_nexus_url }}/@siradel/horizon-core/-/horizon-core-{{ version + npm_qualifier_string }}.tgz)
* [Linux native package (C++)]({{ raw_nexus_url }}/core-linux-{{ version }}.tar.gz)
* [Windows native package (C++)]({{ raw_nexus_url }}/core-windows-{{ version }}.tar.gz)

## Other

### Scene dump

This is a TypeScript library to save and load Horizon scene dumps to and from JSON, base64 or binary data.

* [npm (TypeScript): `@siradel/horizon-scene-dump`]({{ npm_nexus_url }}/@siradel/horizon-scene-dump/-/horizon-scene-dump-{{ version + npm_qualifier_string }}.tgz)

### Monitoring protocol

Use this to deserialize monitoring messages. See [Monitoring the engine](monitoring.html).

* [npm (TypeScript): `@siradel/horizon-monitoring-protocol`]({{ npm_nexus_url }}/@siradel/horizon-protocol/-/horizon-monitoring-protocol-{{ version + npm_qualifier_string }}.tgz)

### Monitoring client

This application connects to and monitors a Horizon client using WebSocket.

* [Linux executable]({{ raw_nexus_url }}/monitoring-client-linux-{{ version }})
* [Windows executable]({{ raw_nexus_url }}/monitoring-client-windows-{{ version }}.exe)

### Testing kit

This archive contains tools to run visual tests inside of Horizon. It contains a viewer that opens a scene dump and takes a screenshot of the loaded scene, an image comparator, and a scene dump migration tool. See the documentation in the archive.

* [Linux (X11)]({{ raw_nexus_url }}/testing-kit-linux-x11-{{ version }}.tar.gz)
* [Linux (headless)]({{ raw_nexus_url }}/testing-kit-linux-headless-{{ version }}.tar.gz)
* [Windows]({{ raw_nexus_url }}/testing-kit-windows-{{ version }}.tar.gz)

### Protocol Buffers definitions

The Horizon protocol & monitoring protocol `.proto` files are available directly in the source tree. These can be useful for creating tooling, or new integrations with languages other than the ones we ship libraries for already.

* Horizon protocol: `//hrz/proto`.
* Horizon monitoring protocol: `//hrz/monitoring/proto`.

### Documentation

Your are reading it right now, but you may want a local copy.

* [Documentation package]({{ raw_nexus_url }}/api-doc-{{ version }}.tar.gz)
