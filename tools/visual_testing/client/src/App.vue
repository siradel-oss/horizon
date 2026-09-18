<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import { Sun, Moon } from "@lucide/vue";
import { onMounted, ref, watchEffect } from "vue";
import TestListPanel from "@/components/TestListPanel.vue";
import ReportSummary from "@/components/ReportSummary.vue";
import TestDetail from "@/components/TestDetail.vue";
import TestEditDialog from "@/components/TestEditDialog.vue";
import ServerLostOverlay from "@/components/ServerLostOverlay.vue";
import { useTests } from "@/composables/useTests";
import { useReport } from "@/composables/useReport";
import { useWorkdir } from "@/composables/useWorkdir";
import { useViewedTest } from "@/composables/useViewedTest";
import { useServerHealth } from "@/composables/useServerHealth";
import { Button } from "@/components/ui/button";

const { tests, refresh: refreshTests, setSelected } = useTests();
const { refresh: refreshReport } = useReport();
const { refresh: refreshWorkdir } = useWorkdir();
const { viewedTestName } = useViewedTest();
const { startMonitoring } = useServerHealth();

const useDarkMode = ref<boolean>(window.matchMedia("(prefers-color-scheme: dark)").matches);
watchEffect(() => {
    if (useDarkMode.value) {
        document.documentElement.classList.add("dark");
    } else {
        document.documentElement.classList.remove("dark");
    }
});

onMounted(async () => {
    startMonitoring();
    await Promise.all([refreshTests(), refreshReport(), refreshWorkdir()]);
    for (const test of tests.value) setSelected(test.name, true);
    if (viewedTestName.value === null && tests.value.length > 0) {
        viewedTestName.value = tests.value[0].name;
    }
});
</script>

<template>
    <Button
        class="rounded-full absolute top-4 right-8 size-10"
        variant="secondary"
        @click="useDarkMode = !useDarkMode"
    >
        <Sun v-if="useDarkMode" class="size-6" />
        <Moon v-else class="size-6" />
    </Button>
    <div class="flex h-screen w-screen overflow-hidden bg-background text-foreground">
        <div class="w-1/3 overflow-y-auto border-r p-4">
            <TestListPanel />
        </div>
        <div class="flex-1 space-y-4 overflow-y-auto p-4">
            <ReportSummary />
            <TestDetail v-if="viewedTestName" :test-name="viewedTestName" />
        </div>

        <TestEditDialog />
        <ServerLostOverlay />
    </div>
</template>
