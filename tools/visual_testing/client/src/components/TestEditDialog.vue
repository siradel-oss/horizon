<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import { computed, reactive, ref, watch } from "vue";
import {
    Dialog,
    DialogContent,
    DialogFooter,
    DialogHeader,
    DialogTitle,
} from "@/components/ui/dialog";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import {
    Select,
    SelectContent,
    SelectItem,
    SelectTrigger,
    SelectValue,
} from "@/components/ui/select";
import { Checkbox } from "@/components/ui/checkbox";
import { callRpc, fileToBase64 } from "@/lib/rpc";
import { ErrorStrategy, TestType } from "@/proto/schema";
import { useTests } from "@/composables/useTests";
import { useImages } from "@/composables/useImages";
import { useEditDialog } from "@/composables/useEditDialog";
import { useViewedTest } from "@/composables/useViewedTest";

const { refresh: refreshTests } = useTests();
const { bumpReferenceVersion } = useImages();
const { open, editingTestName, close } = useEditDialog();
const { viewedTestName } = useViewedTest();

const isEditing = computed(() => !!editingTestName.value);

const form = reactive({
    name: "",
    type: TestType.HrzScene,
    errorThresholdPercent: 1.5,
    timeout: 300,
    errorStrategy: ErrorStrategy.Message,
    errorMessage: "",
    generateReference: false,
});

const file = ref<File | null>(null);
const saving = ref(false);
const error = ref<string | null>(null);

function resetForm() {
    form.name = "";
    form.type = TestType.HrzScene;
    form.errorThresholdPercent = 1.5;
    form.timeout = 300;
    form.errorStrategy = ErrorStrategy.Message;
    form.errorMessage = "";
    form.generateReference = false;
    file.value = null;
    error.value = null;
}

watch(
    () => [open.value, editingTestName.value] as const,
    async ([open, testName]) => {
        if (!open) return;
        resetForm();
        if (testName) {
            const res = await callRpc("GetTest", { name: testName });
            form.name = res.test.name;
            form.type = res.test.type;
            form.errorThresholdPercent = res.test.errorThreshold * 100;
            form.timeout = res.test.timeout;
            form.errorStrategy = res.test.errorStrategy;
            form.errorMessage = res.test.errorMessage ?? "";
        }
    },
    { immediate: true }
);

function onFileChange(event: Event) {
    const input = event.target as HTMLInputElement;
    file.value = input.files?.[0] ?? null;
    if (file.value && !form.name) {
        form.name = file.value.name.replace(/\.(hrz_scene\.pbf|json)$/, "");
    }
}

async function save() {
    error.value = null;

    if (!isEditing.value && !file.value) {
        error.value = "Choose an input file.";
        return;
    }

    saving.value = true;
    try {
        const errorThreshold = Math.max(0, Math.min(1, form.errorThresholdPercent / 100));

        if (isEditing.value && editingTestName.value) {
            await callRpc("UpdateTest", {
                currentName: editingTestName.value,
                newDefinition: {
                    name: form.name,
                    type: form.type,
                    errorThreshold,
                    timeout: form.timeout,
                    errorStrategy: form.errorStrategy,
                    errorMessage: form.errorMessage,
                },
                inputFileContentBase64: file.value ? await fileToBase64(file.value) : null,
                generateReference: form.generateReference,
            });
        } else if (file.value) {
            await callRpc("CreateTest", {
                definition: {
                    name: form.name,
                    type: form.type,
                    errorThreshold,
                    timeout: form.timeout,
                    errorStrategy: form.errorStrategy,
                    errorMessage: form.errorMessage,
                },
                inputFileContentBase64: await fileToBase64(file.value),
                generateReference: form.generateReference,
            });
        }

        if (form.generateReference || file.value) {
            bumpReferenceVersion(form.name);
        }

        await refreshTests();
        viewedTestName.value = form.name;
        close();
    } catch (e) {
        error.value = e instanceof Error ? e.message : String(e);
    } finally {
        saving.value = false;
    }
}
</script>

<template>
    <Dialog :open="open" @update:open="(value) => !value && close()">
        <DialogContent>
            <DialogHeader>
                <DialogTitle>{{ isEditing ? "Edit test" : "Create new test" }}</DialogTitle>
            </DialogHeader>

            <div class="space-y-3">
                <div class="space-y-1">
                    <label class="text-sm font-medium">Test name</label>
                    <Input v-model="form.name" />
                </div>

                <div class="space-y-1">
                    <label class="text-sm font-medium">Test type</label>
                    <Select
                        :model-value="form.type"
                        @update:model-value="(v) => (form.type = v as TestType)"
                    >
                        <SelectTrigger class="w-full">
                            <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                            <SelectItem :value="TestType.HrzScene">Horizon scene</SelectItem>
                            <SelectItem :value="TestType.MapboxStyle">Mapbox style</SelectItem>
                        </SelectContent>
                    </Select>
                </div>

                <div class="space-y-1">
                    <label class="text-sm font-medium">
                        {{
                            form.type === TestType.HrzScene
                                ? "Scene dump file (.hrz_scene.pbf)"
                                : "Mapbox style file (.json)"
                        }}
                    </label>
                    <input
                        type="file"
                        :accept="form.type === TestType.HrzScene ? '.hrz_scene.pbf' : '.json'"
                        class="block w-full text-sm"
                        @change="onFileChange"
                    />
                    <p v-if="isEditing" class="text-xs text-muted-foreground">
                        Leave empty to keep the current input file.
                    </p>
                </div>

                <div class="grid grid-cols-2 gap-3">
                    <div class="space-y-1">
                        <label class="text-sm font-medium">Error threshold (%)</label>
                        <Input
                            :model-value="form.errorThresholdPercent"
                            type="number"
                            step="0.001"
                            @update:model-value="(v) => (form.errorThresholdPercent = Number(v))"
                        />
                    </div>
                    <div class="space-y-1">
                        <label class="text-sm font-medium">Timeout (sec)</label>
                        <Input
                            :model-value="form.timeout"
                            type="number"
                            @update:model-value="(v) => (form.timeout = Number(v))"
                        />
                    </div>
                </div>

                <div class="space-y-1">
                    <label class="text-sm font-medium">Error strategy</label>
                    <Select
                        :model-value="form.errorStrategy"
                        @update:model-value="(v) => (form.errorStrategy = v as ErrorStrategy)"
                    >
                        <SelectTrigger class="w-full">
                            <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                            <SelectItem :value="ErrorStrategy.Abort">Abort</SelectItem>
                            <SelectItem :value="ErrorStrategy.Message">Message</SelectItem>
                        </SelectContent>
                    </Select>
                </div>

                <div v-if="form.errorStrategy === ErrorStrategy.Message" class="space-y-1">
                    <label class="text-sm font-medium">Error message</label>
                    <Input v-model="form.errorMessage" />
                </div>

                <label class="flex items-center gap-2 text-sm">
                    <Checkbox v-model="form.generateReference" />
                    {{ isEditing ? "Regenerate" : "Generate" }} reference image
                </label>

                <p v-if="error" class="text-sm text-destructive">{{ error }}</p>
            </div>

            <DialogFooter>
                <Button variant="outline" @click="close">Cancel</Button>
                <Button :disabled="saving" @click="save">
                    {{ isEditing ? "Update" : "Create" }}
                </Button>
            </DialogFooter>
        </DialogContent>
    </Dialog>
</template>
