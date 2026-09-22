<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import { computed, ref } from "vue";
import { LoaderCircle } from "@lucide/vue";
import { Button } from "@/components/ui/button";
import { Progress } from "@/components/ui/progress";
import { useRun } from "@/composables/useRun";
import { useTests } from "@/composables/useTests";
import { useReport } from "@/composables/useReport";
import { useImages } from "@/composables/useImages";
import { callRpc } from "@/lib/rpc";

const { filteredTests, selection, refresh: refreshTests } = useTests();
const { refresh: refreshReport } = useReport();
const { bumpReferenceVersion } = useImages();
const { status, isRunning, start, cancel } = useRun();

const selectedNames = computed(() =>
    filteredTests.value.filter((t) => selection.has(t.name)).map((t) => t.name)
);

const progressPercent = computed(() => {
    if (!status.value || status.value.total === 0) return 0;
    return (status.value.completed / status.value.total) * 100;
});

async function onProgress() {
    await Promise.all([refreshTests(), refreshReport()]);
}

async function run(showViewerWindow: boolean) {
    if (selectedNames.value.length === 0) return;
    await start(selectedNames.value, showViewerWindow, onProgress);
}

const regenerating = ref(false);
const regenerateError = ref<string | null>(null);

async function regenerateReferences() {
    const names = selectedNames.value;
    if (names.length === 0) return;

    regenerating.value = true;
    regenerateError.value = null;
    try {
        await callRpc("BulkRegenerateReferenceImages", { names });
    } catch (e) {
        regenerateError.value = e instanceof Error ? e.message : String(e);
    } finally {
        bumpReferenceVersion(...names);
        await Promise.all([refreshTests(), refreshReport()]);
        regenerating.value = false;
    }
}
</script>

<template>
    <div class="space-y-2">
        <div v-if="!isRunning" class="flex gap-2">
            <Button
                class="flex-1"
                :disabled="selectedNames.length === 0 || regenerating"
                @click="run(false)"
            >
                Run in background
            </Button>
            <Button
                class="flex-1"
                variant="outline"
                :disabled="selectedNames.length === 0 || regenerating"
                @click="run(true)"
            >
                Run in foreground
            </Button>
        </div>
        <div v-else class="space-y-1">
            <Button class="w-full" variant="destructive" @click="cancel(onProgress)">
                Cancel run ({{ status?.completed }}/{{ status?.total }})
            </Button>
            <Progress :model-value="progressPercent" />
            <p v-if="status?.currentTest" class="text-xs text-muted-foreground">
                Running {{ status.currentTest }}…
            </p>
        </div>
        <Button
            variant="outline"
            class="w-full"
            :disabled="selectedNames.length === 0 || regenerating || isRunning"
            title="Promotes the last run's capture to be the reference. Tests without a capture are re-rendered, which takes longer."
            @click="regenerateReferences"
        >
            <LoaderCircle v-if="regenerating" class="animate-spin" />
            {{
                regenerating
                    ? `Updating ${selectedNames.length} reference image(s)…`
                    : "Update reference images"
            }}
        </Button>

        <p v-if="regenerateError" class="text-sm text-destructive">{{ regenerateError }}</p>
    </div>
</template>
