// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

import { ref, readonly } from "vue";
import { callRpc } from "@/lib/rpc";

const path = ref("");
const isTemp = ref(true);
const hasReport = ref(false);

export function useWorkdir() {
    async function refresh() {
        const res = await callRpc("GetWorkdir", {});
        path.value = res.path;
        isTemp.value = res.isTemp;
        hasReport.value = res.hasReport;
    }

    async function setPath(newPath: string) {
        const res = await callRpc("SetWorkdir", { path: newPath });
        path.value = res.path;
        isTemp.value = res.isTemp;
        hasReport.value = res.hasReport;
    }

    return {
        path: readonly(path),
        isTemp: readonly(isTemp),
        hasReport: readonly(hasReport),
        refresh,
        setPath,
    };
}
