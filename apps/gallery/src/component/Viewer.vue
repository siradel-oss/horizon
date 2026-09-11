<!--
    SPDX-FileCopyrightText: Copyright 2024 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import { onMounted, ref, computed } from "vue";
import { HrzCoreBackend, HrzCoreRuntimeFile } from "@siradel-oss/horizon-core";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { MessageHandler } from "@/utils/messages";
import ScrimDialog from "./ScrimDialog.vue";
import NavigationControls from "./NavigationControls.vue";

interface Props {
    viewerOptions?: Partial<HrzProtocol.ViewerOptions.$Shape>;
    messageHandlerIntervalMs?: number;
    withCameraControls?: boolean;
}

const props = withDefaults(defineProps<Props>(), {
    viewerOptions: () => ({}),
    messageHandlerIntervalMs: 200,
    withCameraControls: true,
});

enum AdditionalAttributionsLink {
    HIDDEN,
    SHOW_MORE,
    SHOW_ATTRIBUTIONS,
}

const navigation = ref<InstanceType<typeof NavigationControls> | null>();
const canvas = ref<HTMLCanvasElement | null>(null);
const shortAttributions = ref<string>("");
const fullAttributions = ref<string[]>([]);
const additionalAttributionsLink = ref<AdditionalAttributionsLink>(
    AdditionalAttributionsLink.HIDDEN
);
const attributionsDialog = ref<InstanceType<typeof ScrimDialog>>();

const defaultOptions = HrzProtocol.ViewerOptions.create({
    showLoadingScreen: true,
    keyBindings: {
        bindings: [
            {
                action: HrzProtocol.KeyAction.TOGGLE_DEV_UI,
                key: HrzProtocol.Key.K_P,
            },
            {
                action: HrzProtocol.KeyAction.MOD_KEY,
                key: HrzProtocol.Key.K_CTRL,
            },
            {
                action: HrzProtocol.KeyAction.RESET_NORTH,
                key: HrzProtocol.Key.K_R,
            },
        ],
    },
    graphicsSettingsOverrides: {
        // We force the simulated atmosphere to be enabled
        // because some demos rely on it.
        atmosphereEnabled: true,
        shadowsEnabled: true,
    },
});

const options = computed(() => {
    return HrzProtocol.ViewerOptions.create({
        ...defaultOptions,
        ...props.viewerOptions,
        keyBindings: {
            ...defaultOptions.keyBindings,
            ...props.viewerOptions?.keyBindings,
            bindings: [
                ...(defaultOptions.keyBindings?.bindings || []),
                ...(props.viewerOptions?.keyBindings?.bindings || []),
            ],
        },
        graphicsSettingsOverrides: {
            ...defaultOptions.graphicsSettingsOverrides,
            ...props.viewerOptions?.graphicsSettingsOverrides,
        },
    });
});

const emits = defineEmits<{
    ready: [api: HrzApi.AsyncApi, msgHandler: MessageHandler];
    clickAt: [x: number, y: number];
}>();

onMounted(() => {
    if (canvas.value) {
        HrzCoreBackend.init(
            canvas.value as HTMLCanvasElement,
            (file: HrzCoreRuntimeFile) => {
                switch (file) {
                    case "hrz_core.js":
                        return new URL(
                            "/node_modules/@siradel-oss/horizon-core/dist/hrz_core.js",
                            import.meta.url
                        ).href;
                    case "hrz_core.wasm":
                        return new URL(
                            "/node_modules/@siradel-oss/horizon-core/dist/hrz_core.wasm",
                            import.meta.url
                        ).href;
                }
            },
            options.value,
            async (backend, status) => {
                if (!backend) {
                    console.error("Failed to initialize Horizon Core Backend: " + status);
                    return;
                }

                let api = new HrzApi.AsyncApi(backend);
                let messageHandler = new MessageHandler(api, props.messageHandlerIntervalMs);

                api.ViewerService.setAttributionEnabled({ value: true });
                messageHandler.watchAttributions(handleAttributions);

                emits("ready", api, messageHandler);

                navigation.value?.init(api);

                messageHandler.watch((msg) => {
                    if (msg.payload != "viewerReady") {
                        return false;
                    }
                    navigation.value?.startPolling();
                    return true;
                });
            }
        );
    }
});

function handleAttributions(msg: HrzProtocol.AttributionsMessage.$Shape) {
    let individualAttributions = (msg.attributions || [])
        .map((a) => a.text?.trim() || "")
        .filter((a) => (a?.length || 0) > 0);

    const MAX_TEXT_LENGTH = 200;
    let textLength = 0;
    let hasNonDisplayedAttributions = false;

    shortAttributions.value = "";

    individualAttributions.forEach((a) => {
        let text = document.createElement("div");
        text.innerHTML = a;
        const thisTextLength = text.innerText.length;

        if (textLength + thisTextLength > MAX_TEXT_LENGTH) {
            hasNonDisplayedAttributions = true;
        } else {
            textLength += thisTextLength;
            if (shortAttributions.value.length > 0) {
                shortAttributions.value += ", ";
            }
            shortAttributions.value += a;
        }
    });

    fullAttributions.value = individualAttributions;

    if (hasNonDisplayedAttributions) {
        if (shortAttributions.value.length > 0) {
            additionalAttributionsLink.value = AdditionalAttributionsLink.SHOW_MORE;
        } else {
            additionalAttributionsLink.value = AdditionalAttributionsLink.SHOW_ATTRIBUTIONS;
        }
    } else {
        additionalAttributionsLink.value = AdditionalAttributionsLink.HIDDEN;
    }
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
        class="touch-none focus:outline-hidden w-full h-full"
        @mousedown="mouseDown"
        @mouseup="mouseUp"
    ></canvas>
    <div class="absolute right-2 bottom-10 pointer-events-none" v-if="withCameraControls">
        <NavigationControls ref="navigation" />
    </div>
    <div
        v-show="fullAttributions.length > 0"
        class="attributions select-none absolute right-0 bottom-0 p-2 text-mBodySmall bg-scrim/25 text-white backdrop-blur-sm rounded-tl-lg"
    >
        <span v-html="shortAttributions"></span>
        <span v-show="additionalAttributionsLink === AdditionalAttributionsLink.SHOW_MORE"
            >,
            <a href="#" @click.prevent="attributionsDialog?.open()">and more&hellip;</a>
        </span>
        <span v-show="additionalAttributionsLink === AdditionalAttributionsLink.SHOW_ATTRIBUTIONS">
            <a href="#" @click.prevent="attributionsDialog?.open()">Show attributions&hellip;</a>
        </span>
    </div>
    <ScrimDialog ref="attributionsDialog">
        <div class="p-4 typography-normal">
            <h1>Attributions</h1>
            <p v-for="a in fullAttributions" v-html="a"></p>
        </div>
    </ScrimDialog>
</template>

<style scoped>
@reference "../style.css";
.attributions :deep(a) {
    @apply text-white underline;
}
</style>
