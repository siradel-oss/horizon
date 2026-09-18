// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

import { ref, readonly } from "vue";

const open = ref(false);
const editingTestName = ref<string | null>(null);

export function useEditDialog() {
    function openCreate() {
        editingTestName.value = null;
        open.value = true;
    }
    function openEdit(name: string) {
        editingTestName.value = name;
        open.value = true;
    }
    function close() {
        open.value = false;
    }
    return {
        open: readonly(open),
        editingTestName: readonly(editingTestName),
        openCreate,
        openEdit,
        close,
    };
}
