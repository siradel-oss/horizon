<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import { computed, onUnmounted, ref } from "vue";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import StatusBadge from "@/components/StatusBadge.vue";
import { Button } from "@/components/ui/button";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import ImageLightbox, { type ImageKind } from "@/components/ImageLightbox.vue";
import { LoaderCircle } from "@lucide/vue";
import { callRpc } from "@/lib/rpc";
import { useImages } from "@/composables/useImages";
import { ErrorType, TestType } from "@/proto/schema";
import { testStatus, useReport } from "@/composables/useReport";
import { useTests } from "@/composables/useTests";
import { useViewedTest } from "@/composables/useViewedTest";
import { useEditDialog } from "@/composables/useEditDialog";

const props = defineProps<{ testName: string }>();

const { refresh: refreshReport, resultFor } = useReport();
const { tests, refresh: refreshTests } = useTests();
const { viewedTestName } = useViewedTest();
const { openEdit } = useEditDialog();
const { imageUrl, bumpReferenceVersion } = useImages();

const testInfo = computed(() => tests.value.find((t) => t.name === props.testName) ?? null);
const result = computed(() => resultFor(props.testName) ?? null);
const status = computed(() => testStatus(result.value ?? undefined));

const hasCaptureAndDiff = computed(
    () =>
        result.value !== null &&
        (result.value.errorType === ErrorType.None ||
            result.value.errorType === ErrorType.Threshold)
);

const errorMessageForType = computed(() => {
    if (!result.value || !testInfo.value) return null;
    switch (result.value.errorType) {
        case ErrorType.MissingInput:
            return "Couldn't load the test's input file.";
        case ErrorType.MissingRef:
            return "Couldn't load the test's reference image.";
        case ErrorType.MigrationError:
            return "Couldn't migrate the scene dump.";
        case ErrorType.Timeout:
            return `The test timed out after ${testInfo.value.timeout} seconds.`;
        case ErrorType.Viewer:
            return "The scene couldn't be captured due to a viewer error.";
        case ErrorType.Aborted:
            return "Test was aborted due to a previous error.";
        case ErrorType.Comparator:
            return "There was an error during capture comparison.";
        default:
            return null;
    }
});

// Hover-to-compare: cycles the large preview between the reference and capture image, mirroring
// the old GUI's tooltip that auto-flipped every 500ms while hovering the thumbnails.
const compareShowingCapture = ref(true);
let compareTimer: ReturnType<typeof setInterval> | null = null;

function startCompare() {
    compareShowingCapture.value = false;
    compareTimer = setInterval(() => {
        compareShowingCapture.value = !compareShowingCapture.value;
    }, 500);
}
function stopCompare() {
    if (compareTimer !== null) {
        clearInterval(compareTimer);
        compareTimer = null;
    }
    compareShowingCapture.value = true;
}
onUnmounted(stopCompare);

const lightboxOpen = ref(false);
const lightboxKind = ref<ImageKind>("reference");

const availableKinds = computed<ImageKind[]>(() =>
    hasCaptureAndDiff.value ? ["reference", "capture", "diff"] : ["reference"]
);

function openLightbox(kind: ImageKind) {
    lightboxKind.value = kind;
    lightboxOpen.value = true;
}

const reviewQueue = computed(() =>
    tests.value
        .map((t) => t.name)
        .filter((name) => {
            const r = resultFor(name);
            return r && !r.success && r.errorType !== ErrorType.Aborted;
        })
);

const regenerating = ref(false);
const regenerateError = ref<string | null>(null);

async function regenerateReference() {
    regenerating.value = true;
    regenerateError.value = null;
    try {
        await callRpc("RegenerateReferenceImage", { name: props.testName });
        bumpReferenceVersion(props.testName);
        await Promise.all([refreshReport(), refreshTests()]);
        goToNextInQueue();
    } catch (e) {
        regenerateError.value = e instanceof Error ? e.message : String(e);
    } finally {
        regenerating.value = false;
    }
}

function goToNextInQueue() {
    const queue = reviewQueue.value.filter((n) => n !== props.testName);
    if (queue.length > 0) {
        viewedTestName.value = queue[0];
    }
}
</script>

<template>
    <Card v-if="testInfo">
        <CardHeader>
            <div class="flex flex-row gap-4">
                <CardTitle>
                    {{ testInfo.name }}
                    <p class="text-xs text-muted-foreground">
                        {{ testInfo.type === TestType.HrzScene ? "Horizon scene" : "Mapbox style" }}
                    </p>
                </CardTitle>
                <StatusBadge :status="status" />
            </div>
        </CardHeader>
        <CardContent class="space-y-4">
            <div
                class="grid grid-cols-2 gap-x-4 gap-y-1 text-sm"
                v-if="status != 'passed' && status != 'skipped'"
            >
                <span class="text-muted-foreground">Error ratio</span>
                <span>
                    {{
                        result?.errorRatio != null
                            ? (result.errorRatio * 100).toFixed(5) + "%"
                            : "--"
                    }}
                    (threshold: {{ (testInfo.errorThreshold * 100).toFixed(5) }}%)
                </span>
                <template v-if="result?.errorMessage">
                    <span class="text-muted-foreground">Error message</span>
                    <span>{{ result.errorMessage }}</span>
                </template>
            </div>

            <p v-if="errorMessageForType" class="text-sm text-destructive">
                {{ errorMessageForType }}
            </p>

            <div v-if="hasCaptureAndDiff" class="grid grid-cols-2 gap-4">
                <div
                    class="flex flex-col gap-2 cursor-zoom-in"
                    @mouseenter="startCompare"
                    @mouseleave="stopCompare"
                    @click="openLightbox(compareShowingCapture ? 'capture' : 'reference')"
                >
                    <img
                        :src="
                            imageUrl(testInfo.name, compareShowingCapture ? 'capture' : 'reference')
                        "
                        :alt="compareShowingCapture ? 'Capture' : 'Reference'"
                        class="rounded border object-contain"
                    />
                    <p class="text-center text-xs text-muted-foreground">
                        Hover to compare — {{ compareShowingCapture ? "Capture" : "Reference" }}
                    </p>
                </div>
                <div class="flex flex-col gap-2 cursor-zoom-in" @click="openLightbox('diff')">
                    <img
                        :src="imageUrl(testInfo.name, 'diff')"
                        alt="Diff"
                        class="rounded border object-contain"
                    />
                    <p class="text-center text-xs text-muted-foreground">
                        Diff ({{ ((result?.errorRatio ?? 0) * 100).toFixed(5) }}%)
                    </p>
                </div>
            </div>
            <div v-else class="grid grid-cols-2 gap-4">
                <img
                    :src="imageUrl(testInfo.name, 'reference')"
                    alt="Reference"
                    class="cursor-zoom-in rounded border object-contain"
                    @click="openLightbox('reference')"
                />
            </div>

            <ImageLightbox
                v-model:open="lightboxOpen"
                v-model:kind="lightboxKind"
                :test-name="testInfo.name"
                :available-kinds="availableKinds"
            />

            <Tabs v-if="result?.log" default-value="log">
                <TabsList>
                    <TabsTrigger value="log">Logs</TabsTrigger>
                </TabsList>
                <TabsContent value="log">
                    <pre
                        class="max-h-64 overflow-auto rounded border bg-muted p-2 font-mono text-xs"
                        >{{ result.log }}</pre>
                </TabsContent>
            </Tabs>

            <div class="flex gap-2">
                <Button
                    variant="outline"
                    :disabled="regenerating"
                    title="Promotes the last run's capture to be the reference. Without a capture the scene is re-rendered, which takes longer."
                    @click="regenerateReference"
                >
                    <LoaderCircle v-if="regenerating" class="animate-spin" />
                    {{ regenerating ? "Updating…" : "Update reference image" }}
                </Button>
                <Button variant="outline" @click="openEdit(testInfo.name)">Edit</Button>
                <Button variant="ghost" @click="goToNextInQueue">
                    Review next ({{ reviewQueue.length }})
                </Button>
            </div>

            <p v-if="regenerateError" class="text-sm text-destructive">
                {{ regenerateError }}
            </p>
        </CardContent>
    </Card>
</template>
