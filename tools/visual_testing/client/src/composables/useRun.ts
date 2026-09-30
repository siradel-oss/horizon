// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

import { computed, ref, readonly } from "vue";
import { callRpc } from "@/lib/rpc";
import type { GetRunStatusResponse } from "@/proto/schema";

const runId = ref<string | null>(null);
const status = ref<GetRunStatusResponse | null>(null);
let pollHandle: ReturnType<typeof setInterval> | null = null;

type OnProgress = () => void | Promise<void>;

export function useRun() {
    const isRunning = computed(() => status.value?.state === "running");
    const error = computed(() =>
        status.value?.state === "failed" ? (status.value.error ?? "Unknown error") : null
    );

    function stopPolling() {
        if (pollHandle !== null) {
            clearInterval(pollHandle);
            pollHandle = null;
        }
    }

    async function poll(onProgress?: OnProgress, onComplete?: () => void) {
        if (runId.value === null) return;
        const wasRunning = status.value?.state === "running";
        status.value = await callRpc("GetRunStatus", { runId: runId.value });
        await onProgress?.();
        if (status.value.state !== "running") {
            stopPolling();
            // Only for a run that went through to the end: a cancelled or failed one leaves
            // whatever the user was looking at alone.
            if (wasRunning && status.value.state === "done") {
                onComplete?.();
            }
        }
    }

    async function start(
        testNames: string[],
        showViewerWindow: boolean,
        onProgress?: OnProgress,
        onComplete?: () => void
    ) {
        const res = await callRpc("StartRun", { testNames, showViewerWindow });
        runId.value = res.runId;
        status.value = {
            state: "running",
            currentTest: null,
            completed: 0,
            total: testNames.length,
        };
        stopPolling();
        pollHandle = setInterval(() => poll(onProgress, onComplete), 1000);
        await poll(onProgress, onComplete);
    }

    async function cancel(onProgress?: OnProgress) {
        if (runId.value === null) return;
        await callRpc("CancelRun", { runId: runId.value });
        await poll(onProgress);
    }

    return {
        status: readonly(status),
        isRunning: readonly(isRunning),
        error: readonly(error),
        start,
        cancel,
    };
}
