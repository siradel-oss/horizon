<script setup lang="ts">
import { onMounted, ref } from "vue";
import { HrzCoreBackend } from "@siradel/horizon-core";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { MessageHandler } from "@/utils/messages";

const canvas = ref<HTMLCanvasElement | null>(null);
const attributions = ref<string>("");

const options: HrzProtocol.IViewerOptions = {
    showLoadingScreen: true,
    keyBindings: {
        bindings: [
            {
                action: HrzProtocol.KeyAction.TOGGLE_DEV_UI,
                key: HrzProtocol.Key.K_P,
            },
        ],
    },
    graphicsSettingsOverrides: {
        // We force the simulated atmosphere to be enabled
        // because some demos rely on it.
        atmosphereEnabled: true,
        shadowsEnabled: true,
    },
};

const emits = defineEmits<{
    ready: [api: HrzApi.AsyncApi, msgHandler: MessageHandler];
    clickAt: [x: number, y: number];
}>();

onMounted(() => {
    if (canvas.value) {
        HrzCoreBackend.init(
            canvas.value as HTMLCanvasElement,
            "assets/",
            options,
            async (backend, status) => {
                if (!backend) {
                    console.error("Failed to initialize Horizon Core Backend: " + status);
                    return;
                }

                let api = new HrzApi.AsyncApi(backend);
                let messageHandler = new MessageHandler(api);

                api.ViewerService.setAttributionEnabled({ value: true });
                messageHandler.watchAttributions(handleAttributions);

                emits("ready", api, messageHandler);
            }
        );
    }
});

function handleAttributions(msg: HrzProtocol.IAttributionsMessage) {
    attributions.value = (msg.attributions || [])
        .map((a) => a.text?.trim())
        .filter((a) => (a?.length || 0) > 0)
        .join(", ");
}

function handleClick(e: MouseEvent) {
    if (!canvas.value) return;
    const rect = canvas.value.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    emits("clickAt", x, y);
}

let mouseDownCoords = { x: 0, y: 0 };

function mouseDown(e: MouseEvent) {
    mouseDownCoords = { x: e.clientX, y: e.clientY };
}

function mouseUp(e: MouseEvent) {
    if (
        Math.abs(e.clientX - mouseDownCoords.x) < 5 &&
        Math.abs(e.clientY - mouseDownCoords.y) < 5
    ) {
        handleClick(e);
    }
}
</script>
<template>
    <canvas
        id="hrz-canvas"
        ref="canvas"
        class="touch-none focus:outline-none w-full h-full"
        @mousedown="mouseDown"
        @mouseup="mouseUp"
    />
    <div
        v-show="attributions.length > 0"
        v-html="attributions"
        class="attributions absolute right-0 bottom-0 p-2 text-mBodySmall bg-scrim/[25%] text-white backdrop-blur rounded-tl-lg"
    ></div>
</template>
