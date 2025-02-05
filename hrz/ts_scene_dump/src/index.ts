import { HrzProtocol } from "@siradel/horizon-protocol";
import { HrzApi } from "@siradel/horizon-api";

function base64Encode(buffer: Uint8Array): string {
    const binString = buffer.reduce(
        (data: string, byte: number) => data + String.fromCharCode(byte),
        ""
    );
    return window.btoa(binString);
}

function base64Decode(data: string): Uint8Array {
    var binString = window.atob(data);
    var len = binString.length;
    var bytes = new Uint8Array(len);
    for (var i = 0; i < len; i++) {
        bytes[i] = binString.charCodeAt(i);
    }
    return bytes;
}

export interface LoadedScene {
    name: string;
    layers: HrzProtocol.ILayer[];
    viewpoints: HrzProtocol.IViewpointDump[];
}

export async function loadSceneObjAsync(
    api: HrzApi.AsyncApi,
    dump: HrzProtocol.ISceneDump,
    cameraAnimationOptions: HrzProtocol.ICameraAnimationOptions,
    clearScene: boolean = true
): Promise<LoadedScene> {
    const layers = await api.SceneDumpService.loadDump({
        cameraAnimation: cameraAnimationOptions,
        clearLayers: clearScene,
        dump: dump,
    });

    let loadedLayers: LoadedScene = {
        name: dump.name ?? "",
        layers: layers.layers,
        viewpoints: dump.viewpoints ?? [],
    };

    return loadedLayers;
}

export function loadSceneObjSync(
    api: HrzApi.SyncApi,
    dump: HrzProtocol.ISceneDump,
    cameraAnimationOptions: HrzProtocol.ICameraAnimationOptions,
    clearScene: boolean = true
): LoadedScene {
    const layers = api.SceneDumpService.loadDump({
        cameraAnimation: cameraAnimationOptions,
        clearLayers: clearScene,
        dump: dump,
    });

    let loadedLayers: LoadedScene = {
        name: dump.name ?? "",
        layers: layers.layers,
        viewpoints: dump.viewpoints ?? [],
    };

    return loadedLayers;
}

export async function loadSceneJsonAsync(
    api: HrzApi.AsyncApi,
    dump: string,
    cameraAnimationOptions: HrzProtocol.ICameraAnimationOptions,
    clearScene: boolean = true
): Promise<LoadedScene> {
    const obj = HrzProtocol.SceneDump.fromObject(JSON.parse(dump));
    return loadSceneObjAsync(api, obj, cameraAnimationOptions, clearScene);
}

export function loadSceneJsonSync(
    api: HrzApi.SyncApi,
    dump: string,
    cameraAnimationOptions: HrzProtocol.ICameraAnimationOptions,
    clearScene: boolean = true
): LoadedScene {
    const obj = HrzProtocol.SceneDump.fromObject(JSON.parse(dump));
    return loadSceneObjSync(api, obj, cameraAnimationOptions, clearScene);
}

export async function loadSceneBinAsync(
    api: HrzApi.AsyncApi,
    dump: Uint8Array,
    cameraAnimationOptions: HrzProtocol.ICameraAnimationOptions,
    clearScene: boolean = true
): Promise<LoadedScene> {
    const obj = HrzProtocol.SceneDump.decode(new Uint8Array(dump));
    return loadSceneObjAsync(api, obj, cameraAnimationOptions, clearScene);
}

export function loadSceneBinSync(
    api: HrzApi.SyncApi,
    dump: Uint8Array,
    cameraAnimationOptions: HrzProtocol.ICameraAnimationOptions,
    clearScene: boolean = true
): LoadedScene {
    const obj = HrzProtocol.SceneDump.decodeDelimited(new Uint8Array(dump));
    return loadSceneObjSync(api, obj, cameraAnimationOptions, clearScene);
}

export function loadSceneBase64Async(
    api: HrzApi.AsyncApi,
    dump: string,
    cameraAnimationOptions: HrzProtocol.ICameraAnimationOptions,
    clearScene: boolean = true
): Promise<LoadedScene> {
    return loadSceneBinAsync(api, base64Decode(dump), cameraAnimationOptions, clearScene);
}

export function loadSceneBase64Sync(
    api: HrzApi.SyncApi,
    dump: string,
    cameraAnimationOptions: HrzProtocol.ICameraAnimationOptions,
    clearScene: boolean = true
): LoadedScene {
    return loadSceneBinSync(api, base64Decode(dump), cameraAnimationOptions, clearScene);
}

export async function dumpSceneObjAsync(
    api: HrzApi.AsyncApi,
    sceneName: string = "",
    viewpoints: HrzProtocol.IViewpointDump[] = []
): Promise<HrzProtocol.SceneDump> {
    let dump = await api.SceneDumpService.dumpScene({
        sceneName: sceneName,
    });

    dump.viewpoints = viewpoints;
    return dump;
}

export function dumpSceneObjSync(
    api: HrzApi.SyncApi,
    sceneName: string = "",
    viewpoints: HrzProtocol.IViewpointDump[] = []
): HrzProtocol.SceneDump {
    let dump = api.SceneDumpService.dumpScene({
        sceneName: sceneName,
    });

    dump.viewpoints = viewpoints;
    return dump;
}

export async function dumpSceneJsonAsync(
    api: HrzApi.AsyncApi,
    sceneName: string = "",
    viewpoints: HrzProtocol.IViewpointDump[] = []
): Promise<string> {
    const dump = await dumpSceneObjAsync(api, sceneName, viewpoints);
    return JSON.stringify(dump.toJSON());
}

export function dumpSceneJsonSync(
    api: HrzApi.SyncApi,
    sceneName: string = "",
    viewpoints: HrzProtocol.IViewpointDump[] = []
): string {
    const dump = dumpSceneObjSync(api, sceneName, viewpoints);
    return JSON.stringify(dump.toJSON());
}

export async function dumpSceneBinAsync(
    api: HrzApi.AsyncApi,
    sceneName: string = "",
    viewpoints: HrzProtocol.IViewpointDump[] = []
): Promise<Uint8Array> {
    const dump = await dumpSceneObjAsync(api, sceneName, viewpoints);
    return await HrzProtocol.SceneDump.encode(dump).finish();
}

export function dumpSceneBinSync(
    api: HrzApi.SyncApi,
    sceneName: string = "",
    viewpoints: HrzProtocol.IViewpointDump[] = []
): Uint8Array {
    const dump = dumpSceneObjSync(api, sceneName, viewpoints);
    return HrzProtocol.SceneDump.encode(dump).finish();
}

export async function dumpSceneBase64Async(
    api: HrzApi.AsyncApi,
    sceneName: string = "",
    viewpoints: HrzProtocol.IViewpointDump[] = []
): Promise<string> {
    return base64Encode(await dumpSceneBinAsync(api, sceneName, viewpoints));
}

export function dumpSceneBase64Sync(
    api: HrzApi.SyncApi,
    sceneName: string = "",
    viewpoints: HrzProtocol.IViewpointDump[] = []
): string {
    return base64Encode(dumpSceneBinSync(api, sceneName, viewpoints));
}
