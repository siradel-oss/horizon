// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

import { computed, reactive, ref, readonly } from "vue";
import { callRpc } from "@/lib/rpc";
import { type TestInfo, TestType } from "@/proto/schema";

const tests = ref<TestInfo[]>([]);
const selection = reactive(new Set<string>());
const nameFilter = ref("");
const typeFilter = reactive(new Set<TestType>([TestType.HrzScene, TestType.MapboxStyle]));
const loading = ref(false);

const filters = computed<{ positive: string[]; negative: string[] }>(() => {
    const positive: string[] = [];
    const negative: string[] = [];
    for (const token of nameFilter.value.split(" ")) {
        if (token.startsWith("-")) {
            if (token.length >= 2) negative.push(token.slice(1).toLowerCase());
        } else if (token !== "") {
            positive.push(token.toLowerCase());
        }
    }
    return { positive, negative };
});

function matchesFilter(test: TestInfo): boolean {
    if (!typeFilter.has(test.type)) return false;
    if (nameFilter.value === "") return true;

    const { positive, negative } = filters.value;

    const name = test.name.toLowerCase();
    if (positive.some((p) => !name.includes(p))) return false;
    if (negative.some((n) => name.includes(n))) return false;
    return true;
}

export function useTests() {
    const filteredTests = computed(() => tests.value.filter(matchesFilter));

    async function refresh() {
        loading.value = true;
        try {
            const res = await callRpc("ListTests", {});
            tests.value = res.tests;
            const names = new Set(tests.value.map((t) => t.name));
            for (const name of Array.from(selection)) {
                if (!names.has(name)) selection.delete(name);
            }
        } finally {
            loading.value = false;
        }
    }

    function isSelected(name: string) {
        return selection.has(name);
    }

    function setSelected(name: string, value: boolean) {
        if (value) selection.add(name);
        else selection.delete(name);
    }

    function selectAll() {
        for (const t of filteredTests.value) selection.add(t.name);
    }

    function deselectAll() {
        selection.clear();
    }

    function selectWhere(predicate: (test: TestInfo) => boolean) {
        for (const t of filteredTests.value) {
            if (predicate(t)) selection.add(t.name);
            else selection.delete(t.name);
        }
    }

    return {
        tests: readonly(tests),
        filteredTests: readonly(filteredTests),
        selection: readonly(selection),
        nameFilter: nameFilter,
        typeFilter: typeFilter,
        loading: readonly(loading),
        matchesFilter,
        refresh,
        isSelected,
        setSelected,
        selectAll,
        deselectAll,
        selectWhere,
    };
}
