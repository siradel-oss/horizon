// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

import { computed, ref, readonly } from "vue";
import { callRpc } from "@/lib/rpc";
import type { BadgeVariants } from "@/components/ui/badge";
import { ErrorType, type Report, type Result } from "@/proto/schema";
import { DeepReadonly } from "vue";

const report = ref<Report | null>(null);
const hasReport = ref(false);

export function useReport() {
    const resultsByName = computed(() => {
        const map = new Map<string, Result>();
        for (const r of report.value?.results ?? []) map.set(r.name, r);
        return map;
    });

    async function refresh() {
        const res = await callRpc("GetReport", {});
        hasReport.value = res.hasReport;
        report.value = res.report ?? null;
    }

    function resultFor(name: string): Result | undefined {
        return resultsByName.value.get(name);
    }

    return {
        report: readonly(report),
        hasReport: readonly(hasReport),
        resultsByName,
        resultFor,
        refresh,
    };
}

export interface ReportSummary {
    total: number;
    passed: number;
    failed: number;
    aborted: number;
}

export function summarizeReport(report: DeepReadonly<Report> | null): ReportSummary {
    if (!report) return { total: 0, passed: 0, failed: 0, aborted: 0 };
    let passed = 0;
    let failed = 0;
    let aborted = 0;
    for (const r of report.results) {
        if (r.errorType === ErrorType.Aborted) {
            aborted++;
        } else if (r.success) {
            passed++;
        } else {
            failed++;
        }
    }
    return { total: report.results.length, passed, failed, aborted };
}

export type TestStatus = "skipped" | "failed" | "passed" | "passed-with-diff" | "aborted";

export function testStatus(result: Result | undefined): TestStatus {
    if (result === undefined) return "skipped";
    if (result.errorType === ErrorType.Aborted) return "aborted";
    if (!result.success) return "failed";
    if (result.errorRatio != null && result.errorRatio > 0) return "passed-with-diff";
    return "passed";
}

// shadcn's Badge has no success/warning variant, so those two tint the theme tokens the way
// upstream's `destructive` variant does. cn() runs tailwind-merge, which drops the variant's own
// bg/text classes.
export const STATUS_BADGE: Record<
    TestStatus,
    { label: string; variant: NonNullable<BadgeVariants["variant"]>; class?: string }
> = {
    skipped: { label: "Not run", variant: "secondary" },
    failed: { label: "Failed", variant: "destructive" },
    passed: {
        label: "Passed",
        variant: "default",
        class: "bg-success/10 text-success dark:bg-success/20",
    },
    "passed-with-diff": {
        label: "Passed",
        variant: "default",
        class: "bg-warning/10 text-warning dark:bg-warning/20",
    },
    aborted: { label: "Aborted", variant: "secondary" },
};
