<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import { ref, watch } from "vue";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Checkbox } from "@/components/ui/checkbox";
import {
    Table,
    TableBody,
    TableCell,
    TableHead,
    TableHeader,
    TableRow,
} from "@/components/ui/table";
import StatusBadge from "@/components/StatusBadge.vue";
import RunProgress from "@/components/RunProgress.vue";
import { useTests } from "@/composables/useTests";
import { testStatus, type TestStatus, useReport } from "@/composables/useReport";
import { useWorkdir } from "@/composables/useWorkdir";
import { useViewedTest } from "@/composables/useViewedTest";
import { useEditDialog } from "@/composables/useEditDialog";
import { TestType } from "@/proto/schema";
import { callRpc } from "@/lib/rpc";

const {
    filteredTests,
    selection,
    nameFilter,
    typeFilter,
    isSelected,
    setSelected,
    selectAll,
    deselectAll,
    selectWhere,
    refresh: refreshTests,
} = useTests();
const { resultFor, refresh: refreshReport } = useReport();
const { path: workdirPath, setPath } = useWorkdir();
const { viewedTestName } = useViewedTest();

const workdirInput = ref(workdirPath.value);
watch(workdirPath, (p) => (workdirInput.value = p));

async function applyWorkdir() {
    await setPath(workdirInput.value);
    await Promise.all([refreshTests(), refreshReport()]);
}

const { openCreate: openCreateDialog, openEdit: openEditDialog } = useEditDialog();

async function removeTest(name: string) {
    if (!confirm(`Are you sure you want to remove '${name}'?`)) return;
    await callRpc("DeleteTest", { name });
    await refreshTests();
    if (viewedTestName.value === name) viewedTestName.value = null;
}

function statusFor(name: string): TestStatus {
    return testStatus(resultFor(name));
}
</script>

<template>
    <div class="flex h-full flex-col gap-4">
        <div class="flex gap-2 items-center">
            <label class="text-center text-sm font-semibold">Working directory</label>
            <Input
                class="grow w-auto"
                v-model="workdirInput"
                placeholder="Empty = temporary directory"
                @keyup.enter="applyWorkdir"
            />
            <Button variant="outline" @click="applyWorkdir">Set</Button>
        </div>

        <Button class="w-full" @click="openCreateDialog">Create new test…</Button>

        <div class="space-y-2">
            <div class="flex gap-2">
                <Input v-model="nameFilter" placeholder="Filter (-exclude)" />
                <Button variant="outline" @click="nameFilter = ''">Clear</Button>
            </div>
            <div class="flex flex-row items-center">
                <label class="text-xs font-semibold w-24">Filter by type</label>
                <div class="flex gap-4 text-sm">
                    <label class="flex items-center gap-1">
                        <Checkbox
                            :model-value="typeFilter.has(TestType.HrzScene)"
                            @update:model-value="
                                (v) =>
                                    v
                                        ? typeFilter.add(TestType.HrzScene)
                                        : typeFilter.delete(TestType.HrzScene)
                            "
                        />
                        Horizon scenes
                    </label>
                    <label class="flex items-center gap-1">
                        <Checkbox
                            :model-value="typeFilter.has(TestType.MapboxStyle)"
                            @update:model-value="
                                (v) =>
                                    v
                                        ? typeFilter.add(TestType.MapboxStyle)
                                        : typeFilter.delete(TestType.MapboxStyle)
                            "
                        />
                        Mapbox styles
                    </label>
                </div>
            </div>
            <div class="flex flex-row items-center">
                <label class="text-xs font-semibold w-24">Select</label>
                <div class="grid grid-cols-4 gap-2 grow">
                    <Button size="sm" variant="outline" @click="selectAll">All</Button>
                    <Button size="sm" variant="outline" @click="deselectAll">None</Button>
                    <Button
                        size="sm"
                        variant="outline"
                        @click="selectWhere((t) => statusFor(t.name) === 'skipped')"
                    >
                        Not run
                    </Button>
                    <Button
                        size="sm"
                        variant="outline"
                        @click="selectWhere((t) => statusFor(t.name) === 'failed')"
                    >
                        Failed
                    </Button>
                </div>
            </div>
        </div>

        <div class="min-h-0 flex-1 overflow-y-auto rounded border">
            <Table class="layout-auto w-full">
                <TableHeader>
                    <TableRow>
                        <TableHead class="w-8"></TableHead>
                        <TableHead class="w-full">Name</TableHead>
                        <TableHead>Status</TableHead>
                    </TableRow>
                </TableHeader>
                <TableBody>
                    <TableRow
                        v-for="test in filteredTests"
                        :key="test.name"
                        :class="viewedTestName === test.name ? 'bg-accent' : ''"
                    >
                        <TableCell>
                            <Checkbox
                                :model-value="isSelected(test.name)"
                                @update:model-value="(v) => setSelected(test.name, !!v)"
                            />
                        </TableCell>
                        <TableCell
                            class="max-w-0 cursor-pointer truncate"
                            :title="test.name"
                            @click="viewedTestName = test.name"
                        >
                            {{ test.name }}
                        </TableCell>
                        <TableCell class="whitespace-nowrap text-right">
                            <StatusBadge :status="statusFor(test.name)" />
                            <Button size="sm" variant="ghost" @click="openEditDialog(test.name)">
                                Edit
                            </Button>
                            <Button size="sm" variant="ghost" @click="removeTest(test.name)">
                                Remove
                            </Button>
                        </TableCell>
                    </TableRow>
                </TableBody>
            </Table>
        </div>

        <RunProgress />
    </div>
</template>
