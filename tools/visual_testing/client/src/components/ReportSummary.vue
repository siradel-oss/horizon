<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import { computed } from "vue";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { useReport, summarizeReport } from "@/composables/useReport";
import { useWorkdir } from "@/composables/useWorkdir";

const { report, hasReport } = useReport();
const { path: workdirPath } = useWorkdir();
const reportSummary = computed(() => summarizeReport(report.value));

function ratio(count: number, total: number) {
    return total > 0 ? ((count / total) * 100).toFixed(1) : "--";
}
const passedRatio = computed(() =>
    ratio(reportSummary.value.passed ?? 0, reportSummary.value.total ?? 0)
);
const failedRatio = computed(() =>
    ratio(reportSummary.value.failed ?? 0, reportSummary.value.total ?? 0)
);
const abortedRatio = computed(() =>
    ratio(reportSummary.value.aborted ?? 0, reportSummary.value.total ?? 0)
);
</script>

<template>
    <Card v-if="hasReport && report">
        <CardHeader>
            <CardTitle>Report</CardTitle>
        </CardHeader>
        <CardContent class="space-y-4">
            <dl class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1 text-sm">
                <dt class="text-muted-foreground">Working directory</dt>
                <dd class="truncate">{{ workdirPath }}</dd>
                <dt class="text-muted-foreground">Date</dt>
                <dd>{{ new Date(report.date).toLocaleString() }}</dd>
                <dt class="text-muted-foreground">Branch</dt>
                <dd>{{ report.branch }}</dd>
                <dt class="text-muted-foreground">Commit</dt>
                <dd class="truncate font-mono text-xs">{{ report.commit }}</dd>
            </dl>
            <div class="grid grid-cols-4 gap-2 text-center">
                <div>
                    <div class="text-xs text-muted-foreground">Total</div>
                    <div class="text-lg font-semibold">{{ reportSummary.total }}</div>
                </div>
                <div>
                    <div class="text-xs text-success">Passed</div>
                    <div class="text-lg font-semibold text-success">
                        {{ reportSummary.passed }} ({{ passedRatio }}%)
                    </div>
                </div>
                <div>
                    <div class="text-xs text-destructive">Failed</div>
                    <div class="text-lg font-semibold text-destructive">
                        {{ reportSummary.failed }} ({{ failedRatio }}%)
                    </div>
                </div>
                <div>
                    <div class="text-xs text-muted-foreground">Aborted</div>
                    <div class="text-lg font-semibold text-muted-foreground">
                        {{ reportSummary.aborted }} ({{ abortedRatio }}%)
                    </div>
                </div>
            </div>
        </CardContent>
    </Card>
    <Card v-else>
        <CardContent class="p-4 text-sm text-muted-foreground">
            No report yet — run some tests.
        </CardContent>
    </Card>
</template>
