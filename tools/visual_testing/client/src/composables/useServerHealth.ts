// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

import { ref, readonly } from "vue";
import { callRpc, RpcError } from "@/lib/rpc";

const PING_INTERVAL_MS = 5000;

const serverLost = ref(false);
let pingHandle: ReturnType<typeof setInterval> | null = null;

export function useServerHealth() {
    function stopMonitoring() {
        if (pingHandle !== null) {
            clearInterval(pingHandle);
            pingHandle = null;
        }
    }

    async function ping() {
        try {
            await callRpc("Ping", {});
        } catch (e) {
            // An RpcError means the server answered, so it is still there.
            if (e instanceof RpcError) return;
            serverLost.value = true;
            stopMonitoring();
        }
    }

    function startMonitoring() {
        stopMonitoring();
        pingHandle = setInterval(ping, PING_INTERVAL_MS);
    }

    return { serverLost: readonly(serverLost), startMonitoring };
}
