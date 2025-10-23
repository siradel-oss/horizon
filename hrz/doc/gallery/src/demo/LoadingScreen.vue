<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { HrzApi } from "@siradel/horizon-api";
import { MessageHandler } from "@/utils/messages";
import { applyDefaultOrthoBaseLayer } from "@/utils/scenes";
import { ref } from "vue";

let msgHandler: MessageHandler;

const showLoadingScreen = ref(true);
const isLoading = ref(true);
const loadingProgress = ref(0); // 0 to 1
const isWorking = ref<boolean>(true);
const isReady = ref(false);

async function testIsWorking(api: HrzApi.AsyncApi) {
    isWorking.value = (await api.ViewerService.isWorking()).value;

    if (!isLoading.value && !isWorking.value && !isReady.value) {
        isReady.value = true;
    }
}

function onLoadingScreenTransitionEnd() {
    if (isReady.value) {
        showLoadingScreen.value = false;
    }
}

async function onHorizonReady(api: HrzApi.AsyncApi, msgHandler_: MessageHandler) {
    applyDefaultOrthoBaseLayer(api);
    msgHandler = msgHandler_;

    msgHandler.watchForever((msg) => {
        if (
            msg.type == HrzProtocol.MessageType.VIEWER_LOADING_PROGRESS_MESSAGE &&
            msg.viewerLoadingProgress
        ) {
            const { completedStepCount, totalStepCount } = msg.viewerLoadingProgress;
            loadingProgress.value = totalStepCount > 0 ? completedStepCount / totalStepCount : 0;
        } else if (msg.type == HrzProtocol.MessageType.VIEWER_READY_MESSAGE) {
            isLoading.value = false;
        }
    });

    setInterval(() => {
        testIsWorking(api);
    }, 50);
}
</script>

<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Custom loading screen</h1>
                <p>
                    Horizon does not load instantly, neither does the data it displays. A built-in
                    loading screen can be activated via the viewer configuration, but it has a few
                    limitations:
                </p>
                <ul>
                    <li>it is not possible to customise its appearance,</li>
                    <li>it cannot be integrated with the rest of the application UI,</li>
                    <li>
                        it is only displayed when the viewer has been downloaded and instantiated,
                    </li>
                    <li>
                        it fades out when the viewer is ready but before the scene’s data is fully
                        loaded.
                    </li>
                </ul>
                <p>
                    By listening to messages with the <code>MessageQueueService</code>, as well as
                    the <code>isWorking</code> method of the <code>ViewerService</code>, it is
                    possible to create a custom loading screen that overcomes these limitations.
                </p>
                <p>
                    <code>VIEWER_LOADING_PROGRESS_MESSAGE</code>s provide progress information about
                    the initialisation of the viewer. A <code>VIEWER_READY_MESSAGE</code> indicates
                    that the viewer is ready to display the scene. This is the moment the scene’s
                    data starts loading. The <code>isWorking</code> method can be polled to
                    determine when the scene has finished loading all the data for its initial
                    viewpoint.
                </p>
                <p>
                    Some scenes can take a while to load all their data, so depending on the use
                    case, it may be preferable not to wait until the scene is fully loaded before
                    hiding the loading screen.
                </p>
                <p><FullscreenSource file="source/LoadingScreen.vue" /></p>
            </div>
        </template>
        <template #right>
            <div class="relative w-full h-full">
                <div
                    class="w-full h-full transition-opacity duration-300 ease-out"
                    :class="{ 'opacity-100': isReady, 'opacity-0': !isReady }"
                >
                    <Viewer @ready="onHorizonReady" />
                </div>
                <div
                    v-if="showLoadingScreen"
                    class="absolute inset-0 bg-transparent flex items-center justify-center z-[1000] transition-opacity duration-300 ease-out"
                    :class="{ 'opacity-0 pointer-events-none': isReady }"
                    @transitionend="onLoadingScreenTransitionEnd"
                >
                    <div class="relative flex items-center justify-center">
                        <svg width="100" height="100" class="relative z-10">
                            <circle
                                cx="50"
                                cy="50"
                                r="40"
                                fill="none"
                                stroke="var(--theme-color-brand-container)"
                                stroke-opacity="0.3"
                                stroke-width="4"
                            />
                            <circle
                                cx="50"
                                cy="50"
                                r="40"
                                fill="none"
                                stroke="var(--theme-color-brand-container)"
                                stroke-width="4"
                                stroke-linecap="round"
                                :stroke-dasharray="`${loadingProgress * 251.2} 251.2`"
                                :stroke-opacity="loadingProgress > 0 ? 1 : 0"
                                transform="rotate(-90 50 50)"
                                class="transition-[stroke-dasharray] duration-300 ease-out"
                            />
                        </svg>
                        <div
                            class="absolute w-[56px] h-[56px] rounded-full bg-[var(--theme-color-brand-container)] animate-disc-pulse"
                        ></div>
                    </div>
                </div>
            </div>
        </template>
    </SplitView>
</template>

<style scoped>
@keyframes disc-pulse {
    0%,
    100% {
        transform: scale(1);
        opacity: 0.8;
    }
    50% {
        transform: scale(1.2);
        opacity: 0.4;
    }
}

.animate-disc-pulse {
    animation: disc-pulse 2s ease-in-out infinite;
}
</style>
