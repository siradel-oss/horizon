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

    export interface ISceneModelPathBuilder<T> {
        set(api: AsyncApi, value: T): Promise<void>;
        setSync(api: SyncApi, value: T): void;
    }

    export abstract class SceneModelPathBuilder<T> implements ISceneModelPathBuilder<T> {
        protected _path: HrzProtocol.Path;

        constructor() {
            this._path = HrzProtocol.Path.create();
        }

        protected subPath(...parts: number[]): HrzProtocol.Path {
            let path = HrzProtocol.Path.create();
            path.root = this._path.root;
            path.parts = [...this._path.parts, ...parts];
            return path;
        }

        abstract set(api: AsyncApi, value: T): Promise<void>;
        abstract setSync(api: SyncApi, value: T): void;
    }

    {% for type in path_types %}
    export class {{ type.full_name|to_short_type_name }}PathBuilder extends SceneModelPathBuilder<{{ type.full_name|to_ts_interface(enum_names) }}> {
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

        public get(api: AsyncApi): Promise<{{ type.full_name|to_ts_type(enum_names) }}> {
            return AsyncSceneModelAccessor.get(api, this._path)
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
            return AsyncSceneModelAccessor.set(api, this._path, encodedValue.finish())
                .then((_) => { return Promise.resolve(); });
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
        }

        {% for u in type.unions %}
        public get{{ u.name|snake_to_pascal }}Case(api: AsyncApi): Promise<{{ type.full_name|to_ts_interface(enum_names) }}["{{ u.name|snake_to_camel }}"]> {
            const ID_TO_CASE: { [key: number]: {{ type.full_name|to_ts_interface(enum_names) }}["{{ u.name|snake_to_camel }}"] } = {
                {% for f in u.fields %}
                {{ f.id }}: "{{ f.name|snake_to_camel }}",
                {% endfor %}
            };
            return AsyncSceneModelAccessor.getOneofCase(api, this.subPath({{ u.fields[0].id }})).then((caseValue) => {
                return Promise.resolve(ID_TO_CASE[caseValue as keyof typeof ID_TO_CASE]);
            });
        }

        public get{{ u.name|snake_to_pascal }}CaseSync(api: SyncApi): {{ type.full_name|to_ts_interface(enum_names) }}["{{ u.name|snake_to_camel }}"] {
            const ID_TO_CASE: { [key: number]: {{ type.full_name|to_ts_interface(enum_names) }}["{{ u.name|snake_to_camel }}"] } = {
                {% for f in u.fields %}
                {{ f.id }}: "{{ f.name|snake_to_camel }}",
                {% endfor %}
            };
            let caseValue = SyncSceneModelAccessor.getOneofCase(api, this.subPath({{ u.fields[0].id }}));
            return ID_TO_CASE[caseValue as keyof typeof ID_TO_CASE];
        }
        {% endfor %}

        {% if not type.is_path_leaf %}
        {% for f in type.fields %}
        {% if not f.repeated %}

        {{ f.documentation|to_documentation_block(8) }}
        public {{ f.name|snake_to_camel }}(): {{ f.type|to_short_type_name }}PathBuilder {
            return {{ f.type|to_short_type_name }}PathBuilder.createWithPath(this.subPath({{ f.id }}));
        }

        {% else %}

        {{ f.documentation|to_documentation_block(8) }}
        public {{ f.name|snake_to_camel }}(index: number): {{ f.type|to_short_type_name }}PathBuilder {
            return {{ f.type|to_short_type_name }}PathBuilder.createWithPath(this.subPath({{ f.id }}, index));
        }

        /**
         * Returns the number of {{ f.name }}.
         */
        public {{f.name|snake_to_camel }}Count(api: AsyncApi): Promise<number> {
            return AsyncSceneModelAccessor.count(api, this.subPath({{ f.id }}));
        }

        /**
         * Returns the number of {{ f.name }}.
         */
        public {{f.name|snake_to_camel }}CountSync(api: SyncApi): number {
            return SyncSceneModelAccessor.count(api, this.subPath({{ f.id }}));
        }

        /**
         * Adds an element to {{ f.name }} and returns the new count.
         */
        public add{{f.name|snake_to_pascal }}(api: AsyncApi, val: {{ f.type|to_ts_interface(enum_names) }}): Promise<number> {
            let encodedValue =
                {% if f.is_primitive %}
                HrzProtocol.{{ f.type|to_wrapper }}.encode({value: val});
                {% elif f.is_enum %}
                HrzProtocol.UInt32Value.encode({value: val as number});
                {% else %}
                {{ f.type }}.encode(val);
                {% endif %}
            return AsyncSceneModelAccessor.add(api, this.subPath({{ f.id }}), encodedValue.finish());
        }

        /**
         * Adds an element to {{ f.name }} and returns the new count.
         */
        public add{{f.name|snake_to_pascal }}Sync(api: SyncApi, val: {{ f.type|to_ts_interface(enum_names) }}): number {
            let encodedValue =
                {% if f.is_primitive %}
                HrzProtocol.{{ f.type|to_wrapper }}.encode({value: val});
                {% elif f.is_enum %}
                HrzProtocol.UInt32Value.encode({value: val as number});
                {% else %}
                {{ f.type }}.encode(val);
                {% endif %}
            return SyncSceneModelAccessor.add(api, this.subPath({{ f.id }}), encodedValue.finish());
        }

        /**
         * Removes an element from {{ f.name }} and returns the new count.
         */
        public remove{{f.name|snake_to_pascal }}(api: AsyncApi, index: number): Promise<number> {
            return AsyncSceneModelAccessor.remove(api, this.subPath({{ f.id }}, index));
        }

        /**
         * Removes an element from {{ f.name }} and returns the new count.
         */
        public remove{{f.name|snake_to_pascal }}Sync(api: SyncApi, index: number): number {
            return SyncSceneModelAccessor.remove(api, this.subPath({{ f.id }}, index));
        }

        {% endif %}
        {% endfor %}
        {% endif %}
    }

    {% endfor %}
}
