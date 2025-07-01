<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol, HrzProtocolHelper } from "@siradel/horizon-protocol";
import { applyDefaultOrthoBaseLayer } from "@/utils/scenes";
import { MessageHandler } from "@/utils/messages";
import { eqLong } from "@/utils/utils";
import { ref, watch, computed, reactive } from "vue";
import ColorButton from "@/component/ColorButton.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";

const IN_MEMORY_LAYER_ID = 1;
const VECTOR_DATA_LAYER_ID = 1;

const MARKER_COLOR_ATTRIBUTE_ID = 1;
const MARKER_ID_ATTRIBUTE_ID = 2;
const MARKER_NAME_ATTRIBUTE_ID = 3;

const MARKER_REPR_ID = 0;

const MARKER_NAME_PROPERTY = "name";
const MARKER_COLOR_PROPERTY = "color";
const MARKER_TEXT_COLOR_PROPERTY = "textColor";
const MARKER_OUTLINE_COLOR_PROPERTY = "outlineColor";

const MARKER_NAME_ATTRIBUTE_NAME = "name";
const MARKER_COLOR_ATTRIBUTE_NAME = "color";

let api: HrzApi.AsyncApi;
let inMemoryLayer: HrzProtocol.ILayerHandle;
let vectorDataLayer: HrzProtocol.ILayerHandle;
let vectorTilesLayer: HrzProtocol.ILayerHandle;
let msgHandler: MessageHandler;

let nextMarkerId = 0;
function makeMarkerId() {
    return nextMarkerId++;
}

interface Marker {
    id: number;
    name: string;
    color: string;
    latitude: number;
    longitude: number;
}

function markerToInMemoryFeature(marker: Marker): HrzProtocol.IInMemoryVectorFeature {
    return {
        geometry: {
            type: HrzProtocol.VectorGeometryType.POINT_GEOMETRY,
            coords: [marker.longitude, marker.latitude, 0],
        },
        attributeValues: [
            {
                numberValue: marker.id,
            },
            {
                stringValue: marker.name,
            },
            {
                stringValue: marker.color,
            },
        ],
    };
}

let markers = reactive<Marker[]>([
    {
        id: makeMarkerId(),
        name: "Mairie de Rennes",
        color: "#D85249",
        latitude: 48.111346,
        longitude: -1.680093,
    },
]);

let selectedMarkerId = ref<number | null>(null);
let addingMarker = ref(false);

const selectedMarker = computed(() => {
    if (selectedMarkerId.value === null) return null;
    return markers.find((m) => m.id === selectedMarkerId.value) || null;
});

// The usual way to handle this with reactivity would be for the
// markers array to be reactive, and for all the features to be
// update in the layer when this changes.
//
// Here we manually update the features to keep them in sync with the
// model so that we can add/remove/edit features individually, which
// is very slightly more efficient.
//
// Both approaches are fine although updating all features can be expensive
// if you have a lot of them. Though in-memory layers shouldn't be used
// to store massive amounts of data anyway.

function beginAddingMarker() {
    addingMarker.value = true;
    selectedMarkerId.value = null;
}

function addMarker(marker: Marker) {
    markers.push(marker);
    HrzApi.InMemoryVectorSourceLayerPathBuilder.create(inMemoryLayer).addFeatures(
        api,
        markerToInMemoryFeature(marker)
    );
    selectedMarkerId.value = marker.id;
}

async function removeSelectedMarker() {
    if (selectedMarkerId.value === null) return;

    const markerId = selectedMarkerId.value;
    const index = markers.findIndex((marker) => marker.id === markerId);
    if (index === -1) {
        return;
    }

    markers.splice(index, 1);
    await HrzApi.InMemoryVectorSourceLayerPathBuilder.create(inMemoryLayer).removeFeatures(
        api,
        index
    );
    selectedMarkerId.value = null;
}

watch(markers, async () => {
    if (selectedMarkerId.value === null) return;

    let idx = markers.findIndex((m) => m.id === selectedMarkerId.value);
    if (idx === -1) return;

    await HrzApi.InMemoryVectorSourceLayerPathBuilder.create(inMemoryLayer)
        .features(idx)
        .set(api, markerToInMemoryFeature(markers[idx]));
});

async function schedulePick(x: number, y: number) {
    if (!api || !msgHandler) return;

    let request: HrzProtocol.IPickRequest = {
        coords: { x: x, y: y },
        includedRasters: [],
    };

    let requestResult = await api.ViewerService.pickScreen(request);
    if (requestResult.hasATicket && requestResult.ticket) {
        msgHandler.awaitPickResult(requestResult.ticket, (results: HrzProtocol.IPickResults) => {
            if (addingMarker.value) {
                let id = makeMarkerId();
                let marker: Marker = {
                    id: id,
                    name: `New marker ${id}`,
                    color: "#3e5aff",
                    latitude: results.position?.latitude || 0,
                    longitude: results.position?.longitude || 0,
                };
                addMarker(marker);
                addingMarker.value = false;
                return;
            }

            for (const result of results.results || []) {
                if (eqLong(result.layer?.handle?.opaque, vectorTilesLayer.opaque)) {
                    let idAttribute = result.vector?.featureId?.attributes?.at(0)?.value;
                    if (idAttribute !== null && idAttribute !== undefined) {
                        selectedMarkerId.value = HrzProtocolHelper.attributeAsNumber(idAttribute);
                    } else {
                        selectedMarkerId.value = null;
                    }
                    return;
                }
            }
            selectedMarkerId.value = null;
        });
    }
}

watch(selectedMarkerId, async (newValue) => {
    await api.ViewerService.deselectAllFeatures();
    if (newValue !== null) {
        await api.ViewerService.selectFeatures({
            features: [
                {
                    layer: vectorTilesLayer,
                    featureId: {
                        attributes: [
                            {
                                id: MARKER_ID_ATTRIBUTE_ID,
                                value: { numberValue: newValue },
                            },
                        ],
                    },
                },
            ],
        });
    }
});

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler_: MessageHandler) {
    api = api_;
    msgHandler = msgHandler_;

    applyDefaultOrthoBaseLayer(api);

    api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        pose: {
            bearing: 0,
            tilt: -Math.PI / 2 + 0.1,
            position: {
                latitude: 48.115452341020415,
                longitude: -1.6405653904991666,
                altitude: 9934.416964659467,
            },
        },
        maxAltitude: 10000000,
        minTilt: 0,
        maxTilt: Math.PI,
    });

    // =====================================================
    // In-memory vector source layer definition
    // =====================================================

    inMemoryLayer = await api.LayerService.createLayer({
        name: "In-memory data layer",
        type: HrzProtocol.LayerType.IN_MEMORY_VECTOR_SOURCE,
    });
    HrzApi.InMemoryVectorSourceLayerPathBuilder.create(inMemoryLayer).set(api, {
        id: IN_MEMORY_LAYER_ID,
        projection: {
            descriptorType: HrzProtocol.SrsDescriptorType.SRID_DESCRIPTOR,
            descriptor: "EPSG:4326", // WGS84 lat-long
        },
        attributes: [
            {
                id: MARKER_ID_ATTRIBUTE_ID,
                isFeatureId: true,
                transform: HrzProtocol.AttributeTransform.ATTRIBUTE_TRANSFORM_TO_INT,
            },
            {
                id: MARKER_NAME_ATTRIBUTE_ID,
                transform: HrzProtocol.AttributeTransform.ATTRIBUTE_TRANSFORM_TO_STRING,
            },
            {
                id: MARKER_COLOR_ATTRIBUTE_ID,
                transform: HrzProtocol.AttributeTransform.ATTRIBUTE_TRANSFORM_TO_COLOR,
            },
        ],
        features: markers.map(markerToInMemoryFeature),
    });

    // =====================================================
    // Vector data layer definition
    // =====================================================

    vectorDataLayer = await api.LayerService.createLayer({
        name: "Data layer",
        type: HrzProtocol.LayerType.VECTOR_DATA,
    });
    HrzApi.VectorDataLayerPathBuilder.create(vectorDataLayer).set(api, {
        id: VECTOR_DATA_LAYER_ID,
        sources: [
            {
                providerType: HrzProtocol.VectorDataProviderType.IN_MEMORY_VECTOR_DATA_PROVIDER,
                hasGeometry: true,
                inMemoryDataProvider: {
                    inMemoryLayerId: IN_MEMORY_LAYER_ID,
                },
                attributes: [
                    {
                        id: MARKER_ID_ATTRIBUTE_ID,
                        isFeatureId: true,
                    },
                    {
                        id: MARKER_NAME_ATTRIBUTE_ID,
                    },
                    {
                        id: MARKER_COLOR_ATTRIBUTE_ID,
                    },
                ],
            },
        ],
    });

    // =====================================================
    // Vector tiles layer definition
    // =====================================================

    vectorTilesLayer = await api.LayerService.createLayer({
        name: "Vector tiles layer",
        type: HrzProtocol.LayerType.VECTOR_TILES,
    });
    HrzApi.VectorTilesLayerPathBuilder.create(vectorTilesLayer).set(api, {
        source: {
            vectorDataLayerId: VECTOR_DATA_LAYER_ID,
        },
        style: {
            attributes: [
                {
                    stylingName: "color",
                    vectorDataAttrId: MARKER_COLOR_ATTRIBUTE_ID,
                },
                {
                    stylingName: "name",
                    vectorDataAttrId: MARKER_NAME_ATTRIBUTE_ID,
                },
            ],
            stylingScript: `
                set "${MARKER_NAME_PROPERTY}" = attr("${MARKER_NAME_ATTRIBUTE_NAME}");
                set "${MARKER_COLOR_PROPERTY}" = attr("${MARKER_COLOR_ATTRIBUTE_NAME}");
                set "${MARKER_TEXT_COLOR_PROPERTY}" = lighten(attr("${MARKER_COLOR_ATTRIBUTE_NAME}"), 0.95);
                set "${MARKER_OUTLINE_COLOR_PROPERTY}" = darken(attr("${MARKER_COLOR_ATTRIBUTE_NAME}"), 0.5);
                emit ${MARKER_REPR_ID};
            `,
            // prettier-ignore
            representations: [
                {
                    type: HrzProtocol.VectorReprType.SYMBOL_VECTOR_REPR,
                    id: MARKER_REPR_ID,
                    sceneViews: { bits: 3 },
                    symbol: {
                        ignoreWorldOcclusions: true,
                        rootElement: {
                            type: HrzProtocol.SymbolElementType.ANCHOR_SYMBOL_ELEMENT,
                            anchor: {
                                xAxisAlignment: HrzProtocol.SymbolAxisAlignment.AXIS_ALIGNMENT_SCREEN,
                                yAxisAlignment: HrzProtocol.SymbolAxisAlignment.AXIS_ALIGNMENT_SCREEN,
                                elementSizeUnit: HrzProtocol.SymbolSizeUnit.SYMBOL_SIZE_IN_PIXELS,
                                elementSizeRelativeScaling: HrzProtocol.SymbolRelativeScaling.SYMBOL_RELATIVE_SCALING_CAMERA_HEIGHT,
                                elementAlignment: { defaultValue: { x: 0, y: 1 } },
                                minRelativeScale: 0,
                                maxRelativeScale: 10,
                                canOverlapOtherSymbols: true,
                                hidesOtherSymbols: false,
                                child: {
                                    type: HrzProtocol.SymbolElementType.FLEX_SYMBOL_ELEMENT,
                                    flex: {
                                        mainAxis: HrzProtocol.FlexAxis.FLEX_AXIS_VERTICAL,
                                        mainAxisAlignment: HrzProtocol.FlexMainAxisAlignment.FLEX_MAIN_AXIS_START,
                                        crossAxisAlignment: HrzProtocol.FlexCrossAxisAlignment.FLEX_CROSS_AXIS_CENTER,
                                        children: [
                                            {
                                                type: HrzProtocol.SymbolElementType.CONSTRAINED_BOX_SYMBOL_ELEMENT,
                                                constrainedBox: {
                                                    maxSize: {
                                                        defaultValue: { x: 150, y: 1000 }
                                                    },
                                                    child: {
                                                        type: HrzProtocol.SymbolElementType.TEXT_SYMBOL_ELEMENT,
                                                        text: {
                                                            text: {
                                                                defaultValue: "<NO MARKER NAME>",
                                                                name: MARKER_NAME_PROPERTY,
                                                            },
                                                            fontSize: { defaultValue: 15 },
                                                            textColor: {
                                                                defaultValue: { r: 1, g: 1, b: 1, a: 1 },
                                                                name: MARKER_TEXT_COLOR_PROPERTY,
                                                            },
                                                            outlineWidth: { defaultValue: 0.1 },
                                                            outlineWidthUnit: HrzProtocol.TextOutlineWidthUnit.OUTLINE_WIDTH_IN_EM,
                                                            outlineColor: {
                                                                defaultValue: { r: 0, g: 0, b: 0, a: 1 },
                                                                name: MARKER_OUTLINE_COLOR_PROPERTY,
                                                            },
                                                            alignment: { defaultValue: HrzProtocol.TextAlignment.CENTERED },
                                                            fontUrl: "assets/demo/SourceSans3-Regular.ttf",
                                                            lineSpacing: { defaultValue: 1.0 },
                                                        }
                                                    },
                                                },
                                            },
                                            {
                                                type: HrzProtocol.SymbolElementType.IMAGE_SYMBOL_ELEMENT,
                                                image: {
                                                    url: "assets/demo/marker.webp",
                                                    color: {
                                                        defaultValue: { r: 1, g: 1, b: 1, a: 1 },
                                                        name: MARKER_COLOR_PROPERTY,
                                                    },
                                                    colorBlendMode: HrzProtocol.BlendMode.BLEND_OVERLAY,
                                                    colorBlendStrength: 1.0,
                                                    scale: { defaultValue: 0.7 },
                                                }
                                            }
                                        ],
                                    },
                                }
                            }
                        }
                    }
                }
            ],
        },
        resolution: { maxScreenSpaceError: 2 },
        visible: true,
        sceneViews: { bits: 3 },
    });
}

async function retrieveInMemoryLayerModel(): Promise<any> {
    if (!inMemoryLayer) {
        return {};
    }
    return (
        await HrzApi.InMemoryVectorSourceLayerPathBuilder.create(inMemoryLayer).get(api)
    ).toJSON();
}

async function retrieveVectorDataLayerModel(): Promise<any> {
    if (!vectorDataLayer) {
        return {};
    }
    return (await HrzApi.VectorDataLayerPathBuilder.create(vectorDataLayer).get(api)).toJSON();
}

async function retrieveVectorTilesLayerModel(): Promise<any> {
    if (!vectorTilesLayer) {
        return {};
    }
    return (await HrzApi.VectorTilesLayerPathBuilder.create(vectorTilesLayer).get(api)).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Markers</h1>
                <p>
                    This demo shows an example of using an in-memory vector data source and the
                    picking functionality to implement a simple application to place markers on a
                    map and change their properties.
                </p>
                <hr />
                <p class="flex flex-row flex-wrap gap-4">
                    <ColorButton icon="add" @click="beginAddingMarker">Add marker</ColorButton>
                    <ColorButton
                        icon="delete"
                        @click="removeSelectedMarker"
                        :disabled="selectedMarkerId === null"
                        >Remove marker</ColorButton
                    >
                </p>
                <p class="typography text-mLabelLarge italic" v-show="addingMarker">
                    Click on the map to add a marker
                </p>
                <div v-if="selectedMarker !== null">
                    <p>
                        <label>Marker name</label>
                        <input type="text" v-model.lazy="selectedMarker.name" />
                    </p>
                    <p>
                        <label>Marker color</label>
                        <input type="color" v-model.lazy="selectedMarker.color" />
                    </p>
                </div>
                <hr />
                <p>
                    <FullscreenSource file="source/Markers.vue" />
                    <FullscreenSceneModel
                        text="View in-memory vector data layer model"
                        :retrieveData="retrieveInMemoryLayerModel"
                    />
                    <FullscreenSceneModel
                        text="View vector data layer model"
                        :retrieveData="retrieveVectorDataLayerModel"
                    />
                    <FullscreenSceneModel
                        text="View vector tiles layer model"
                        :retrieveData="retrieveVectorTilesLayerModel"
                    />
                </p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" @clickAt="schedulePick" />
        </template>
    </SplitView>
</template>
