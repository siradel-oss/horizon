import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";

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
): Promise<HrzProtocol.ILayerHandle | undefined> {
    let layers = await api.LayerService.getAllLayers();
    return layers.layers.find((l) => l.name === name)?.handle || undefined;
}

export async function findVectorRepresentation(
    api: HrzApi.AsyncApi,
    layer: HrzProtocol.ILayerHandle,
    type: HrzProtocol.VectorReprType
): Promise<number | undefined> {
    let model = await HrzApi.VectorTilesLayerPathBuilder.create(layer).style().get(api);
    let index = model.representations.findIndex((r) => r.type === type);
    return index !== -1 ? index : undefined;
}

// @Todo Maybe this could be in the protocol utility library?
export function visitSymbolElements(
    element: HrzProtocol.ISymbolElement | null | undefined,
    callback: (element: HrzProtocol.ISymbolElement) => void
) {
    if (!element) return;

    callback(element);

    switch (element.type) {
        case HrzProtocol.SymbolElementType.ANCHOR_SYMBOL_ELEMENT:
            visitSymbolElements(element.anchor?.child, callback);
            break;
        case HrzProtocol.SymbolElementType.STACK_SYMBOL_ELEMENT:
            element.stack?.children?.forEach((e) => visitSymbolElements(e, callback));
            break;
        case HrzProtocol.SymbolElementType.STACK_EXPAND_SYMBOL_ELEMENT:
            visitSymbolElements(element.stackExpand?.child, callback);
            break;
        case HrzProtocol.SymbolElementType.PADDING_SYMBOL_ELEMENT:
            visitSymbolElements(element.padding?.child, callback);
            break;
        case HrzProtocol.SymbolElementType.FLEX_SYMBOL_ELEMENT:
            element.flex?.children?.forEach((e) => visitSymbolElements(e, callback));
            break;
        case HrzProtocol.SymbolElementType.FLEXIBLE_SYMBOL_ELEMENT:
            visitSymbolElements(element.flexible?.child, callback);
            break;
        case HrzProtocol.SymbolElementType.SIZED_BOX_SYMBOL_ELEMENT:
            visitSymbolElements(element.sizedBox?.child, callback);
            break;
        case HrzProtocol.SymbolElementType.CONSTRAINED_BOX_SYMBOL_ELEMENT:
            visitSymbolElements(element.constrainedBox?.child, callback);
            break;
        case HrzProtocol.SymbolElementType.ALIGN_SYMBOL_ELEMENT:
            visitSymbolElements(element.align?.child, callback);
            break;
        case HrzProtocol.SymbolElementType.ROTATED_BOX_SYMBOL_ELEMENT:
            visitSymbolElements(element.rotatedBox?.child, callback);
            break;
        case HrzProtocol.SymbolElementType.ASPECT_RATIO_SYMBOL_ELEMENT:
            visitSymbolElements(element.aspectRatio?.child, callback);
            break;
        case HrzProtocol.SymbolElementType.FITTED_BOX_SYMBOL_ELEMENT:
            visitSymbolElements(element.fittedBox?.child, callback);
            break;
        case HrzProtocol.SymbolElementType.TRANSFORM_SYMBOL_ELEMENT:
            visitSymbolElements(element.transform?.child, callback);
            break;
        case HrzProtocol.SymbolElementType.OPTIONAL_SYMBOL_ELEMENT:
            visitSymbolElements(element.optional?.child, callback);
            break;
        case HrzProtocol.SymbolElementType.VARIANT_SYMBOL_ELEMENT:
            element.variant?.children?.forEach((e) => visitSymbolElements(e, callback));
            break;
    }
}

export async function transformSymbolElements(
    api: HrzApi.AsyncApi,
    layer: HrzProtocol.ILayerHandle,
    reprIndex: number,
    callback: (element: HrzProtocol.ISymbolElement) => void
): Promise<void> {
    let model = await HrzApi.VectorTilesLayerPathBuilder.create(layer)
        .style()
        .representations(reprIndex)
        .get(api);
    if (model.type != HrzProtocol.VectorReprType.SYMBOL_VECTOR_REPR || !model.symbol) {
        throw Error("Vector representation is not symbol");
    }
    visitSymbolElements(model.symbol.rootElement, callback);
    return HrzApi.VectorTilesLayerPathBuilder.create(layer)
        .style()
        .representations(reprIndex)
        .set(api, model);
}
