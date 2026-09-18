// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

import { computed, ref, readonly } from "vue";
import { callRpc } from "@/lib/rpc";
import type { GetRunStatusResponse } from "@/proto/schema";

const runId = ref<string | null>(null);
const status = ref<GetRunStatusResponse | null>(null);
let pollHandle: ReturnType<typeof setInterval> | null = null;

export function useRun() {
    const isRunning = computed(() => status.value?.state === "running");

    function stopPolling() {
        if (pollHandle !== null) {
            clearInterval(pollHandle);
            pollHandle = null;
        }
    }

    async function poll(onProgress?: () => void) {
        if (runId.value === null) return;
        status.value = await callRpc("GetRunStatus", { runId: runId.value });
        onProgress?.();
        if (status.value.state !== "running") {
            stopPolling();
        }
    }

    async function start(testNames: string[], showViewerWindow: boolean, onProgress?: () => void) {
        const res = await callRpc("StartRun", { testNames, showViewerWindow });
        runId.value = res.runId;
        status.value = {
            state: "running",
            currentTest: null,
            completed: 0,
            total: testNames.length,
        };
        stopPolling();
        pollHandle = setInterval(() => poll(onProgress), 1000);
        await poll(onProgress);
    }

    async function cancel(onProgress?: () => void) {
        if (runId.value === null) return;
        await callRpc("CancelRun", { runId: runId.value });
        await poll(onProgress);
    }

    return { status: readonly(status), isRunning: readonly(isRunning), start, cancel };
}
