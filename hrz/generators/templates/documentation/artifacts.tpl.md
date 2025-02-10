---
Title: Artifacts
Category: General
---

Everything we make can be found on the [Nexus](http://redacted.localhost/).

## Horizon

Those are the core components of Horizon and are all required to run the engine. See the [introduction](index.html) for more details on the architecture.

### Protocol

* [npm (TypeScript): `@siradel/horizon-protocol` version `{{ version + npm_qualifier_string }}`]({{ npm_nexus_url }}/@siradel/horizon-protocol/-/horizon-protocol-{{ version + npm_qualifier_string }}.tgz)
* [Linux native package (C++)]({{ raw_nexus_url }}/cpp-protocol-linux-{{ version }}.tar.gz)
* [Windows native package (C++)]({{ raw_nexus_url }}/cpp-protocol-windows-{{ version }}.tar.gz)

### API

* [npm (TypeScript): `@siradel/horizon-api` version `{{ version + npm_qualifier_string }}`]({{ npm_nexus_url }}/@siradel/horizon-api/-/horizon-api-{{ version + npm_qualifier_string }}.tgz)
* [Linux native package (C++)]({{ raw_nexus_url }}/cpp-api-linux-{{ version }}.tar.gz)
* [Windows native package (C++)]({{ raw_nexus_url }}/cpp-api-windows-{{ version }}.tar.gz)

### Core

* [npm (TypeScript): `@siradel/horizon-core` version `{{ version + npm_qualifier_string }}`]({{ npm_nexus_url }}/@siradel/horizon-core/-/horizon-core-{{ version + npm_qualifier_string }}.tgz)
* [Linux native package (C++)]({{ raw_nexus_url }}/core-linux-{{ version }}.tar.gz)
* [Windows native package (C++)]({{ raw_nexus_url }}/core-windows-{{ version }}.tar.gz)

## Other

### SceneDump

This is a TypeScript library to save and load Horizon scene dumps to and from JSON, base64 or binary data.

* [npm (TypeScript): `@siradel/horizon-scene-dump` version `{{ version + npm_qualifier_string }}`]({{ npm_nexus_url }}/@siradel/horizon-scene-dump/-/horizon-scene-dump-{{ version + npm_qualifier_string }}.tgz)

### Monitoring application

This program connects and monitors a Horizon client using WebSocket.

* [Linux executable]({{ raw_nexus_url }}/monitoring-client-linux-{{ version }})
* [Windows executable]({{ raw_nexus_url }}/monitoring-client-windows-{{ version }}.exe)

### Testing kit

This archive contains tools to run visual tests inside of Horizon. It contains a viewer that opens a scene dump and takes a screenshot of the loaded scene, an image comparator, and a scene dump migration tool. See the documentation in the archive.

* [Linux (X11)]({{ raw_nexus_url }}/testing-kit-linux-x11-{{ version }}.tar.gz)
* [Linux (headless)]({{ raw_nexus_url }}/testing-kit-linux-headless-{{ version }}.tar.gz)
* [Windows]({{ raw_nexus_url }}/testing-kit-windows-{{ version }}.tar.gz)

### Raw protocol definitions

The Horizon protocol descriptor file can be parsed using the protobuf library to generate code ([explanations here](using_proto.html)).

The Horizon monitoring protocol is necessary to decode monitoring information received from the engine, if you opt out of using the dedicated monitoring application ([explanations here](monitoring.html)).

* [Horizon protocol descriptor]({{ raw_nexus_url }}/hrz-protocol-{{ version }}.pbf)
* [Horizon monitoring protocol definition]({{ raw_nexus_url }}/hrz-monitoring-interface-{{ version }}.proto)

### Documentation

Your are reading it right now, but you may want a local copy.

* [Documentation package]({{ raw_nexus_url }}/api-doc-{{ version }}.tar.gz)
