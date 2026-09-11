// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";

async function applySceneDump(
    api: HrzApi.AsyncApi,
    url: string,
    clearLayers: boolean,
    includeCamera: boolean
) {
    let resp = await fetch(url);
    if (resp.ok) {
        let dump = HrzProtocol.SceneDump.decode(new Uint8Array(await resp.arrayBuffer()));
        if (!includeCamera && dump.cameras) {
            dump.cameras = [];
        }
        await api.SceneDumpService.loadDump({
            clearLayers,
            dump: dump,
        });
    }
}

export async function applySceneTemplate(
    api: HrzApi.AsyncApi,
    name: string,
    includeCamera: boolean = true
) {
    let url = `scene/template/${name}.hrz_scene.pbf`;
    await applySceneDump(api, url, false, includeCamera);
}

export async function applyScene(api: HrzApi.AsyncApi, name: string) {
    let url = `scene/${name}.hrz_scene.pbf`;
    await applySceneDump(api, url, true, true);
}

export async function applyDefaultOrthoBaseLayer(
    api: HrzApi.AsyncApi
): Promise<HrzProtocol.LayerHandle> {
    HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
        .terrain()
        .terrainColor()
        .set(api, { r: 0.059, g: 0.227, b: 0.341, a: 1 });

    return applySceneTemplate(api, "ign_bd_ortho").then(() => {
        return getLayerByName(api, "IGN BD ORTHO").then((layer) => {
            return HrzProtocol.LayerHandle.create(layer);
        });
    });
}

export async function applyDefaultSymbolicBaseLayer(
    api: HrzApi.AsyncApi
): Promise<HrzProtocol.LayerHandle> {
    HrzApi.SceneViewSettingsPathBuilder.create(HrzProtocol.SceneViewIndex.SCENE_VIEW_0)
        .terrain()
        .terrainColor()
        .set(api, { r: 0.667, g: 0.835, b: 0.914, a: 1 });

    return applySceneTemplate(api, "plan_ign").then(() => {
        return getLayerByName(api, "Plan IGN").then((layer) => {
            return HrzProtocol.LayerHandle.create(layer);
        });
    });
}

export async function getLayerByName(
    api: HrzApi.AsyncApi,
    name: string
): Promise<HrzProtocol.LayerHandle | undefined> {
    let layers = await api.LayerService.getAllLayers();
    let layer = layers.layers.find((l) => l.name === name);
    return layer && HrzProtocol.LayerHandle.create(layer.handle || {});
}

export async function findVectorRepresentation(
    api: HrzApi.AsyncApi,
    layer: HrzProtocol.LayerHandle.$Properties,
    reprType: string
): Promise<number | undefined> {
    let model = await HrzApi.VectorTilesLayerPathBuilder.create(layer).style().get(api);
    let index = model.representations.findIndex((r) => r.reprType === reprType);
    return index !== -1 ? index : undefined;
}

// @Todo Maybe this could be in the protocol utility library?
export function visitSymbolElements(
    element: HrzProtocol.SymbolElement.$Properties | null | undefined,
    callback: (element: HrzProtocol.SymbolElement.$Properties) => void
) {
    if (!element) return;

    callback(element);

    switch (element.elementType) {
        case "anchor":
            visitSymbolElements(element.anchor?.child, callback);
            break;
        case "stack":
            element.stack?.children?.forEach((e) => visitSymbolElements(e, callback));
            break;
        case "stackExpand":
            visitSymbolElements(element.stackExpand?.child, callback);
            break;
        case "padding":
            visitSymbolElements(element.padding?.child, callback);
            break;
        case "flex":
            element.flex?.children?.forEach((e) => visitSymbolElements(e, callback));
            break;
        case "flexible":
            visitSymbolElements(element.flexible?.child, callback);
            break;
        case "sizedBox":
            visitSymbolElements(element.sizedBox?.child, callback);
            break;
        case "constrainedBox":
            visitSymbolElements(element.constrainedBox?.child, callback);
            break;
        case "align":
            visitSymbolElements(element.align?.child, callback);
            break;
        case "rotatedBox":
            visitSymbolElements(element.rotatedBox?.child, callback);
            break;
        case "aspectRatio":
            visitSymbolElements(element.aspectRatio?.child, callback);
            break;
        case "fittedBox":
            visitSymbolElements(element.fittedBox?.child, callback);
            break;
        case "transform":
            visitSymbolElements(element.transform?.child, callback);
            break;
        case "optional":
            visitSymbolElements(element.optional?.child, callback);
            break;
        case "variant":
            element.variant?.children?.forEach((e) => visitSymbolElements(e, callback));
            break;
    }
}

export async function transformSymbolElements(
    api: HrzApi.AsyncApi,
    layer: HrzProtocol.LayerHandle.$Properties,
    reprIndex: number,
    callback: (element: HrzProtocol.SymbolElement.$Properties) => void
): Promise<void> {
    let model = await HrzApi.VectorTilesLayerPathBuilder.create(layer)
        .style()
        .representations(reprIndex)
        .get(api);
    if (model.reprType != "symbol" || !model.symbol) {
        throw Error("Vector representation is not symbol");
    }
    visitSymbolElements(model.symbol.rootElement, callback);
    return HrzApi.VectorTilesLayerPathBuilder.create(layer)
        .style()
        .representations(reprIndex)
        .set(api, model);
}
