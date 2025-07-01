<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol, HrzProtocolHelper } from "@siradel/horizon-protocol";
import { applyDefaultSymbolicBaseLayer, applyScene, getLayerByName } from "@/utils/scenes";
import { MessageHandler } from "@/utils/messages";
import FullscreenSource from "@/component/FullscreenSource.vue";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import ColorButton from "@/component/ColorButton.vue";
import { ref, watch } from "vue";

let api: HrzApi.AsyncApi;
let msgHandler: MessageHandler;
let vectorDataLayer: HrzProtocol.ILayerHandle;
let vectorTilesLayer: HrzProtocol.ILayerHandle;
let time = 0;
let autoRefresh = ref(false);
let autoRefreshInterval: number | null = null;

async function respondToVectorDataRequest(msg: HrzProtocol.IVectorDataRequestMessage) {
    let response: HrzProtocol.IVectorDataRequestResponse = {
        ticket: msg.ticket,
        features: [],
    };

    for (const featureId of msg.featureIdSelection?.featureIds || []) {
        const id = HrzProtocolHelper.attributeAsNumber((featureId.attributes || [])[0].value || {});
        const feature: HrzProtocol.IClientFeature = {
            attributeValues: [{ numberValue: Math.sin(time + id / 100) }],
        };
        response.features?.push(feature);
    }

    await api.ClientDataService.provideVectorData(response);
}

function refreshData() {
    time += 0.16;
    api.ClientDataService.invalidateVectorData({
        vectorDataLayerId: 0,
        vectorDataSourceIndex: 1,
        everything: {},
    });
}

watch(autoRefresh, (newValue) => {
    if (newValue) {
        autoRefreshInterval = window.setInterval(refreshData, 2500);
    } else {
        if (autoRefreshInterval) {
            window.clearInterval(autoRefreshInterval);
            autoRefreshInterval = null;
        }
    }
});

async function onHorizonReady(api_: HrzApi.AsyncApi, msgHandler_: MessageHandler) {
    api = api_;
    msgHandler = msgHandler_;

    await applyScene(api, "rennes_iris_client_data").then(async () => {
        vectorDataLayer = (await getLayerByName(api, "IRIS vector data")) || {};
        vectorTilesLayer = (await getLayerByName(api, "IRIS vector tiles")) || {};
    });
    await applyDefaultSymbolicBaseLayer(api);

    await api.CameraService.setOrbit({
        cameraIndex: HrzProtocol.CameraIndex.CAMERA_0,
        pose: {
            bearing: 0,
            tilt: -Math.PI / 2,
            position: {
                latitude: 48.111558,
                longitude: -1.678848,
                altitude: 16000,
            },
        },
        maxAltitude: 10000000,
        minTilt: 0,
        maxTilt: Math.PI,
    });

    msgHandler.watchForever((msg) => {
        if (
            msg.type == HrzProtocol.MessageType.VECTOR_DATA_REQUEST_MESSAGE &&
            msg.vectorDataRequest
        ) {
            if (
                msg.vectorDataRequest.vectorDataLayerId == 0 &&
                msg.vectorDataRequest.vectorDataSourceIndex == 1 &&
                msg.vectorDataRequest.featureIdSelection
            ) {
                respondToVectorDataRequest(msg.vectorDataRequest);
            }
        }
    });
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
                <h1>Live data</h1>
                <p>
                    This demo uses a client vector data provider to change the values of an
                    attribute at regular intervals or when the user clicks on the “Refresh data”
                    button.
                </p>
                <p>
                    The vector data layer has two sources: one loads a PMTiles file containing the
                    geometry and some attributes, and the other is a client data provider that
                    provides the attribute we colorize the areas with.
                </p>
                <p>
                    When the data needs to be refreshed, the vector data for the client provider is
                    invalidated, which triggers a new request that is sent to the client, to which
                    the client responds with the new attribute values.
                </p>
                <p>
                    Note that the data is synthetic and thus does not have a meaningful value. But
                    following the same principle, an external or generated data source could be
                    wired to provide useful data.
                </p>
                <hr />
                <p class="flex flex-row gap-4 flex-wrap">
                    <ColorButton
                        :icon="autoRefresh ? 'pause' : 'play_arrow'"
                        @click="autoRefresh = !autoRefresh"
                        >{{ autoRefresh ? "Stop auto-refresh" : "Start auto-refresh" }}</ColorButton
                    >
                    <ColorButton icon="refresh" @click="refreshData" :disabled="autoRefresh"
                        >Refresh data</ColorButton
                    >
                </p>
                <p class="italic text-mdLabelLarge" v-show="autoRefresh">
                    The data is automatically refreshed every 2.5 seconds.
                </p>
                <hr />
                <p>
                    <FullscreenSource file="source/LiveData.vue" />
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
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
