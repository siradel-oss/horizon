// SPDX-FileCopyrightText: Copyright 2018 Siradel
// SPDX-License-Identifier: MIT

import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import Long from "long";

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

export namespace HrzApi {

    export const VERSION: string = "{{ version }}";

    //////// Async backend and API

    export interface AsyncBackend {
        rpc(service: number, method: number, input: Uint8Array): Promise<Uint8Array>;
    }

    export class AsyncApi {
        backend : AsyncBackend;

        {% for s in protocol.services %}
        public {{ s.full_name|last(".") }} : Async{{ s.full_name|last(".") }};
        {% endfor %}

        public constructor(backend: AsyncBackend) {
            this.backend = backend;

            {% for s in protocol.services %}
            this.{{ s.full_name|last(".") }} = new Async{{ s.full_name|last(".") }}(this.backend);
            {% endfor %}
        }
    }

    {% for s in protocol.services %}
    {{ s.documentation|to_documentation_block(8) }}
    export class Async{{ s.full_name|last(".") }} {
        backend : AsyncBackend;

        public constructor(b: AsyncBackend) {
            this.backend = b;
        }

        {% for m in s.methods %}
        {{ m.documentation|to_documentation_block(8) }}
        {% if m.input != "HrzProtocol.Void" %}
        public {{ m.name|camel_case }}(req: {{ m.input|to_ts_interface(enum_names) }}) : Promise<{{ m.output }} & {{ m.output }}.$Shape> {
            return this.backend.rpc(
                {{ s.id }},
                {{ m.id }},
                {{ m.input }}.encode(req).finish())
                    .then((data: Uint8Array) => {
                        return Promise.resolve({{ m.output }}.decode(data));
                    });
        }
        {% else %}
        public {{ m.name|camel_case }}() : Promise<{{ m.output }} & {{ m.output }}.$Shape> {
            return this.backend.rpc(
                {{ s.id }},
                {{ m.id }},
                {{ m.input }}.encode({}).finish())
                    .then((data: Uint8Array) => {
                        return Promise.resolve({{ m.output }}.decode(data));
                    });
        }
        {% endif %}
        {% endfor %}
    }
    {% endfor %}

    namespace AsyncSceneModelAccessor {
        export function get(api: AsyncApi, path: HrzProtocol.Path): Promise<Uint8Array> {
            return api.SceneModelService.get(path)
                .then((response) => { return Promise.resolve(response.value); });
        }

        export function set(api: AsyncApi, path: HrzProtocol.Path, payload: Uint8Array): Promise<HrzProtocol.Void> {
            return api.SceneModelService.set({path: path, payload: payload});
        }

        export function add(api: AsyncApi, path: HrzProtocol.Path, payload: Uint8Array): Promise<number> {
            return api.SceneModelService.add({path: path, payload: payload})
                .then((response) => { return Promise.resolve(response.value); });
        }

        export function remove(api: AsyncApi, path: HrzProtocol.Path): Promise<number> {
            return api.SceneModelService.remove(path)
                .then((response) => { return Promise.resolve(response.value); });
        }

        export function count(api: AsyncApi, path: HrzProtocol.Path): Promise<number> {
            return api.SceneModelService.count(path)
                .then((response) => { return Promise.resolve(response.value); });
        }

        export function getOneofCase(api: AsyncApi, path: HrzProtocol.Path): Promise<number> {
            return api.SceneModelService.getOneofCase(path)
                .then((response) => { return Promise.resolve(response.value); });
        }
    }

    //////// Sync backend and API

    export interface SyncBackend {
        rpcSync(service: number, method: number, input: Uint8Array): Uint8Array;
    }

    export class SyncApi {
        backend : SyncBackend;

        {% for s in protocol.services %}
        public {{ s.full_name|last(".") }} : Sync{{ s.full_name|last(".") }};
        {% endfor %}

        public constructor(backend: SyncBackend) {
            this.backend = backend;

            {% for s in protocol.services %}
            this.{{ s.full_name|last(".") }} = new Sync{{ s.full_name|last(".") }}(this.backend);
            {% endfor %}
        }
    }

    {% for s in protocol.services %}
    {{ s.documentation|to_documentation_block(8) }}
    export class Sync{{ s.full_name|last(".") }} {
        backend : SyncBackend;

        public constructor(b: SyncBackend) {
            this.backend = b;
        }

        {% for m in s.methods %}
        {{ m.documentation|to_documentation_block(8) }}
        {% if m.input != "HrzProtocol.Void" %}
        public {{ m.name|camel_case }}(req: {{ m.input|to_ts_interface(enum_names) }}) : {{ m.output }} & {{ m.output }}.$Shape {
            return {{ m.output }}.decode(this.backend.rpcSync(
                {{ s.id }},
                {{ m.id }},
                {{ m.input }}.encode(req).finish()));
        }
        {% else %}
        public {{ m.name|camel_case }}() : {{ m.output }} & {{ m.output }}.$Shape {
            return {{ m.output }}.decode(this.backend.rpcSync(
                {{ s.id }},
                {{ m.id }},
                {{ m.input }}.encode({}).finish()));
        }
        {% endif %}
        {% endfor %}
    }
    {% endfor %}

    namespace SyncSceneModelAccessor {
        export function get(api: SyncApi, path: HrzProtocol.Path): Uint8Array {
            return api.SceneModelService.get(path).value;
        }

        export function set(api: SyncApi, path: HrzProtocol.Path, payload: Uint8Array): void {
            api.SceneModelService.set({path: path, payload: payload});
        }

        export function add(api: SyncApi, path: HrzProtocol.Path, payload: Uint8Array): number {
            return api.SceneModelService.add({path: path, payload: payload}).value;
        }

        export function remove(api: SyncApi, path: HrzProtocol.Path): number {
            return api.SceneModelService.remove(path).value;
        }

        export function count(api: SyncApi, path: HrzProtocol.Path): number {
            return api.SceneModelService.count(path).value;
        }

        export function getOneofCase(api: SyncApi, path: HrzProtocol.Path): number {
            return api.SceneModelService.getOneofCase(path).value;
        }
    }

    //////// Backend converter

    /**
     * Convert a synchronous backend to an asynchronous backend.
     */
    export class SyncBackendWrapper implements AsyncBackend {
        private backend: SyncBackend;

        constructor(backend: SyncBackend) {
            this.backend = backend;
        }

        rpc(service: number, method: number, input: Uint8Array): Promise<Uint8Array> {
            return Promise.resolve<Uint8Array>(this.backend.rpcSync(service, method, input));
        }
    }

    //////// Path builders

    export interface ISceneModelPathBuilder<T, P> {
        clone(): P;
        set(api: AsyncApi, value: T): Promise<void>;
        setSync(api: SyncApi, value: T): void;
    }

    export abstract class SceneModelPathBuilder<T, P> implements ISceneModelPathBuilder<T, P> {
        protected _path: HrzProtocol.Path;

        constructor() {
            this._path = HrzProtocol.Path.create();
        }

        abstract clone(): P;
        abstract set(api: AsyncApi, value: T): Promise<void>;
        abstract setSync(api: SyncApi, value: T): void;
    }

    {% for type in path_types %}
    export class {{ type.full_name|to_short_type_name }}PathBuilder extends SceneModelPathBuilder<{{ type.full_name|to_ts_interface(enum_names) }}, {{ type.full_name|to_short_type_name }}PathBuilder> {
        constructor() {
            super();
        }

        public static createWithPath(path: HrzProtocol.Path): {{ type.full_name|to_short_type_name }}PathBuilder {
            var p = new {{ type.full_name|to_short_type_name }}PathBuilder();
            p._path = path;
            return p;
        }

        {% if type.is_path_root %}
        {% if type.path_root_type == "HrzProtocol.Void" %}

        public static create(): {{ type.full_name|to_short_type_name }}PathBuilder {
            var p = new {{ type.full_name|to_short_type_name }}PathBuilder();
            p._path.root = HrzProtocol.PathRoot.create({ {{ type.path_root|snake_to_camel }}: {} });
            return p;
        }

        {% else %}

        public static create({{type.path_root|camel_case}}: {{ type.path_root_type|to_ts_interface(enum_names) }}): {{ type.full_name|to_short_type_name }}PathBuilder {
            var p = new {{ type.full_name|to_short_type_name }}PathBuilder();
            p._path.root = HrzProtocol.PathRoot.create({ {{ type.path_root|snake_to_camel }}: {{ type.path_root|camel_case }} });
            return p;
        }

        {% endif %}
        {% endif %}

        public clone(): {{ type.full_name|to_short_type_name }}PathBuilder {
            // ...
            var path = HrzProtocol.Path.decode(HrzProtocol.Path.encode(this._path).finish());
            return {{ type.full_name|to_short_type_name }}PathBuilder.createWithPath(path);
        }

        public get(api: AsyncApi): Promise<{{ type.full_name|to_ts_type(enum_names) }}> {
            let prom = AsyncSceneModelAccessor.get(api, this._path)
                .then((payload) => {
                    let decodedValue =
                        {% if type.is_primitive %}
                        HrzProtocol.{{ type.full_name|to_wrapper }}.decode(payload).value;
                        {% elif type.is_enum %}
                        HrzProtocol.UInt32Value.decode(payload).value as {{ type.full_name }};
                        {% else %}
                        {{ type.full_name }}.decode(payload);
                        {% endif %}
                    return Promise.resolve(decodedValue);
                });
            this._path = HrzProtocol.Path.create();
            return prom;
        }

        public getSync(api: SyncApi): {{ type.full_name|to_ts_type(enum_names) }} {
            let payload = SyncSceneModelAccessor.get(api, this._path);
            let decodedValue =
                {% if type.is_primitive %}
                HrzProtocol.{{ type.full_name|to_wrapper }}.decode(payload).value;
                {% elif type.is_enum %}
                HrzProtocol.UInt32Value.decode(payload).value as {{ type.full_name }};
                {% else %}
                {{ type.full_name }}.decode(payload);
                {% endif %}
            this._path = HrzProtocol.Path.create();
            return decodedValue;
        }

        public set(api: AsyncApi, value: {{ type.full_name|to_ts_interface(enum_names) }}): Promise<void> {
            let encodedValue =
                {% if type.is_primitive %}
                HrzProtocol.{{ type.full_name|to_wrapper }}.encode({value: value});
                {% elif type.is_enum %}
                HrzProtocol.UInt32Value.encode({value: value as number});
                {% else %}
                {{ type.full_name }}.encode(value);
                {% endif %}
            let prom = AsyncSceneModelAccessor.set(api, this._path, encodedValue.finish())
                .then((_) => { return Promise.resolve(); });
            this._path = HrzProtocol.Path.create();
            return prom;
        }

        public setSync(api: SyncApi, value: {{ type.full_name|to_ts_interface(enum_names) }}): void {
            let encodedValue =
                {% if type.is_primitive %}
                HrzProtocol.{{ type.full_name|to_wrapper }}.encode({value: value});
                {% elif type.is_enum %}
                HrzProtocol.UInt32Value.encode({value: value as number});
                {% else %}
                {{ type.full_name }}.encode(value);
                {% endif %}
            SyncSceneModelAccessor.set(api, this._path, encodedValue.finish());
            this._path = HrzProtocol.Path.create();
        }

        {% for u in type.unions %}
        public get{{ u.name|snake_to_pascal }}Case(api: AsyncApi): Promise<{{ type.full_name|to_ts_interface(enum_names) }}["{{ u.name|snake_to_camel }}"]> {
            const ID_TO_CASE: { [key: number]: {{ type.full_name|to_ts_interface(enum_names) }}["{{ u.name|snake_to_camel }}"] } = {
                {% for f in u.fields %}
                {{ f.id }}: "{{ f.name|snake_to_camel }}",
                {% endfor %}
            };
            this._path.parts.push({{ u.fields[0].id }});
            let prom = AsyncSceneModelAccessor.getOneofCase(api, this._path).then((caseValue) => {
                return Promise.resolve(ID_TO_CASE[caseValue as keyof typeof ID_TO_CASE]);
            });
            this._path = HrzProtocol.Path.create();
            return prom;
        }

        public get{{ u.name|snake_to_pascal }}CaseSync(api: SyncApi): {{ type.full_name|to_ts_interface(enum_names) }}["{{ u.name|snake_to_camel }}"] {
            const ID_TO_CASE: { [key: number]: {{ type.full_name|to_ts_interface(enum_names) }}["{{ u.name|snake_to_camel }}"] } = {
                {% for f in u.fields %}
                {{ f.id }}: "{{ f.name|snake_to_camel }}",
                {% endfor %}
            };
            this._path.parts.push({{ u.fields[0].id }});
            let caseValue = SyncSceneModelAccessor.getOneofCase(api, this._path);
            this._path = HrzProtocol.Path.create();
            return ID_TO_CASE[caseValue as keyof typeof ID_TO_CASE];
        }
        {% endfor %}

        {% if not type.is_path_leaf %}
        {% for f in type.fields %}
        {% if not f.repeated %}

        {{ f.documentation|to_documentation_block(8) }}
        public {{ f.name|snake_to_camel }}(): {{ f.type|to_short_type_name }}PathBuilder {
            this._path.parts.push({{ f.id }});
            let newBuilder = {{ f.type|to_short_type_name }}PathBuilder.createWithPath(this._path);
            this._path = HrzProtocol.Path.create();
            return newBuilder;
        }

        {% else %}

        {{ f.documentation|to_documentation_block(8) }}
        public {{ f.name|snake_to_camel }}(index: number): {{ f.type|to_short_type_name }}PathBuilder {
            this._path.parts.push({{ f.id }});
            this._path.parts.push(index);
            let newBuilder = {{ f.type|to_short_type_name }}PathBuilder.createWithPath(this._path);
            this._path = HrzProtocol.Path.create();
            return newBuilder;
        }

        /**
         * Returns the number of {{ f.name }}.
         */
        public {{f.name|snake_to_camel }}Count(api: AsyncApi): Promise<number> {
            this._path.parts.push({{ f.id }});
            let count = AsyncSceneModelAccessor.count(api, this._path);
            this._path = HrzProtocol.Path.create();
            return count;
        }

        /**
         * Returns the number of {{ f.name }}.
         */
        public {{f.name|snake_to_camel }}CountSync(api: SyncApi): number {
            this._path.parts.push({{ f.id }});
            let count = SyncSceneModelAccessor.count(api, this._path);
            this._path = HrzProtocol.Path.create();
            return count;
        }

        /**
         * Adds an element to {{ f.name }} and returns the new count.
         */
        public add{{f.name|snake_to_pascal }}(api: AsyncApi, val: {{ f.type|to_ts_interface(enum_names) }}): Promise<number> {
            this._path.parts.push({{ f.id }});
            let encodedValue =
                {% if f.is_primitive %}
                HrzProtocol.{{ f.type|to_wrapper }}.encode({value: val});
                {% elif f.is_enum %}
                HrzProtocol.UInt32Value.encode({value: val as number});
                {% else %}
                {{ f.type }}.encode(val);
                {% endif %}
            let count = AsyncSceneModelAccessor.add(api, this._path, encodedValue.finish());
            this._path = HrzProtocol.Path.create();
            return count;
        }

        /**
         * Adds an element to {{ f.name }} and returns the new count.
         */
        public add{{f.name|snake_to_pascal }}Sync(api: SyncApi, val: {{ f.type|to_ts_interface(enum_names) }}): number {
            this._path.parts.push({{ f.id }});
            let encodedValue =
                {% if f.is_primitive %}
                HrzProtocol.{{ f.type|to_wrapper }}.encode({value: val});
                {% elif f.is_enum %}
                HrzProtocol.UInt32Value.encode({value: val as number});
                {% else %}
                {{ f.type }}.encode(val);
                {% endif %}
            let count = SyncSceneModelAccessor.add(api, this._path, encodedValue.finish());
            this._path = HrzProtocol.Path.create();
            return count;
        }

        /**
         * Removes an element from {{ f.name }} and returns the new count.
         */
        public remove{{f.name|snake_to_pascal }}(api: AsyncApi, index: number): Promise<number> {
            this._path.parts.push({{ f.id }});
            this._path.parts.push(index);
            let count = AsyncSceneModelAccessor.remove(api, this._path);
            this._path = HrzProtocol.Path.create();
            return count;
        }

        /**
         * Removes an element from {{ f.name }} and returns the new count.
         */
        public remove{{f.name|snake_to_pascal }}Sync(api: SyncApi, index: number): number {
            this._path.parts.push({{ f.id }});
            this._path.parts.push(index);
            let count = SyncSceneModelAccessor.remove(api, this._path);
            this._path = HrzProtocol.Path.create();
            return count;
        }

        {% endif %}
        {% endfor %}
        {% endif %}
    }

    {% endfor %}
}
