<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { applyDefaultOrthoBaseLayer } from "@/utils/scenes";
import { ref } from "vue";

let spinner = ref<HTMLElement | null>(null);
let successiveIsNotWorking = ref<number>(0);

async function testIsWorking(api: HrzApi.AsyncApi) {
    let isWorking = await api.ViewerService.isWorking();
    if (!isWorking.value) {
        successiveIsNotWorking.value++;
    } else {
        successiveIsNotWorking.value = 0;
    }
}

function onHorizonReady(api: HrzApi.AsyncApi) {
    applyDefaultOrthoBaseLayer(api);

    setInterval(() => {
        testIsWorking(api);
    }, 50);
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Loading indicator</h1>
                <p>
                    This example shows how to display a loading indicator while the Horizon engine
                    is loading data or doing any work. This is achieved by repeatedly calling the
                    <code>IsLoading</code> method from <code>ViewerService</code>. The scene is
                    considered stable once this method returns <code>false</code>.
                </p>
                <p>
                    You will notice that as you move around the planet, a loading indicator appears
                    on the bottom-left on the canvas, indicating when the engine is doing work.
                </p>
                <p>
                    It is recommended to apply a little bit of hysteresis to this value to avoid
                    flickering, as transient states when the engine is not doing any work can happen
                    between loading stages.
                </p>
                <p><FullscreenSource file="source/LoadingIndicator.vue" /></p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
            <div
                ref="spinner"
                class="absolute bottom-4 left-4 w-[50px] aspect-square rounded-full border-8 border-onPrimary border-r-primary animate-spin transition-opacity duration-300 ease-in-out"
                :class="{ 'opacity-0': successiveIsNotWorking > 4 }"
            ></div>
        </template>
    </SplitView>
</template>
