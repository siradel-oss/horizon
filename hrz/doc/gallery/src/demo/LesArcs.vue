<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { applySceneTemplate, applyScene, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import { ref } from "vue";

const api = ref<HrzApi.AsyncApi | null>(null);
const layerPolygons = ref<HrzProtocol.ILayerHandle | undefined>(undefined);
const layerPolylines = ref<HrzProtocol.ILayerHandle | undefined>(undefined);
const layerLifts = ref<HrzProtocol.ILayerHandle | undefined>(undefined);

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api.value = api_;

    await applyScene(api.value, "les_arcs").then(async function () {
        layerPolygons.value = await getLayerByName(api.value!, "Les Arcs polygons");
        layerPolylines.value = await getLayerByName(api.value!, "Les Arcs polylines");
        layerLifts.value = await getLayerByName(api.value!, "Les Arcs lifts");
    });

    await applySceneTemplate(api.value, "aws_terrain_tiles", false);
}

async function retrieveLayerModel(layer: HrzProtocol.ILayerHandle | undefined): Promise<any> {
    if (!layer || !api.value) {
        return {};
    }
    return (await HrzApi.VectorTilesLayerPathBuilder.create(layer).get(api.value)).toJSON();
}

async function retrievePolygonsLayerModel(): Promise<any> {
    return retrieveLayerModel(layerPolygons.value);
}

async function retrievePolylinesLayerModel(): Promise<any> {
    return retrieveLayerModel(layerPolylines.value);
}

async function retrieveLiftsLayerModel(): Promise<any> {
    return retrieveLayerModel(layerLifts.value);
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Les Arcs vector map</h1>
                <p>
                    This scene is an example of how a small area can be represented as a vector map,
                    in this case the ski resort of Les Arcs. The map is composed of three layers.
                </p>
                <ul>
                    <li>
                        <strong>Polygons layer</strong>: Contains the polygons of the ski slopes,
                        buildings, grass areas, forests, etc. They are simply displayed as filled
                        polygons using a palette, using three different flat overlay representations
                        for handling z-ordering correctly. Additionally, some polygons use patterns
                        to represent trees, grass, etc.
                    </li>
                    <li>
                        <strong>Polylines layer</strong>: Contains the polylines of ski slopes,
                        roads, streets, paths, hiking and MTB tracks, etc. They are displayed using
                        two polylines every time, one for the outline, which is just a polyline with
                        a larger width and a darker color, and one for the inner line, which is a
                        polyline with a smaller width. Additionally some of them use dashes.
                    </li>
                    <li>
                        <strong>Lifts layer</strong>: Contains the lifts of the ski resort, such as
                        ski lifts, gondolas, etc. They are displayed as 3D cylinders where the
                        elevation of each point has been baked in the Z component of the vector
                        data. In this case these cylinders are not clamped dynamically to the
                        terrain.
                    </li>
                </ul>
                <hr />
                <p>
                    <FullscreenSceneModel
                        text="View polygons layer definition"
                        :retrieveData="retrievePolygonsLayerModel"
                    />
                    <FullscreenSceneModel
                        text="View polylines layer definition"
                        :retrieveData="retrievePolylinesLayerModel"
                    />
                    <FullscreenSceneModel
                        text="View lifts layer definition"
                        :retrieveData="retrieveLiftsLayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
