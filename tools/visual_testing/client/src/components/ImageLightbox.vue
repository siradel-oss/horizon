<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import { computed, onMounted, onUnmounted } from "vue";
import { Button } from "@/components/ui/button";
import { useImages } from "@/composables/useImages";
import { Dialog, DialogContent, DialogHeader } from "@/components/ui/dialog";

export type ImageKind = "reference" | "capture" | "diff";

const { imageUrl } = useImages();

const props = defineProps<{
    open: boolean;
    testName: string;
    kind: ImageKind;
    availableKinds: ImageKind[];
}>();
const emit = defineEmits<{
    (e: "update:open", value: boolean): void;
    (e: "update:kind", value: ImageKind): void;
}>();

const KIND_LABELS: Record<ImageKind, string> = {
    reference: "Reference",
    capture: "Capture",
    diff: "Diff",
};

const src = computed(() => imageUrl(props.testName, props.kind));

function cycleKind(direction: 1 | -1) {
    const kinds = props.availableKinds;
    if (kinds.length <= 1) return;
    const index = kinds.indexOf(props.kind);
    const nextIndex = (index + direction + kinds.length) % kinds.length;
    emit("update:kind", kinds[nextIndex]);
}

function onKeydown(event: KeyboardEvent) {
    if (!props.open) return;
    if (event.key === "ArrowLeft") {
        cycleKind(-1);
    } else if (event.key === "ArrowRight") {
        cycleKind(1);
    }
}

onMounted(() => window.addEventListener("keydown", onKeydown));
onUnmounted(() => window.removeEventListener("keydown", onKeydown));
</script>

<template>
    <Dialog :open="props.open" @update:open="(value) => emit('update:open', value)">
        <DialogContent class="max-w-140!">
            <DialogHeader>
                <div class="flex gap-1 justify-center">
                    <Button
                        v-for="k in props.availableKinds"
                        :key="k"
                        size="sm"
                        :variant="k === props.kind ? 'default' : 'ghost'"
                        @click="emit('update:kind', k)"
                    >
                        {{ KIND_LABELS[k] }}
                    </Button>
                </div>
            </DialogHeader>

            <div class="flex justify-center">
                <img :src="src" :alt="KIND_LABELS[props.kind]" />
            </div>

            <p class="text-xs text-center">
                Press <kbd class="rounded bg-white/10 px-1 py-0.5">Esc</kbd> to close
                <template v-if="props.availableKinds.length > 1">
                    ·
                    <kbd class="rounded bg-white/10 px-1 py-0.5">←</kbd>
                    <kbd class="rounded bg-white/10 px-1 py-0.5">→</kbd> to switch images
                </template>
            </p>
        </DialogContent>
    </Dialog>
</template>
