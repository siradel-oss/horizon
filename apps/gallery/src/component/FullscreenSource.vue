<!--
    SPDX-FileCopyrightText: Copyright 2024 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import TextButton from "./TextButton.vue";
import ScrimDialog from "./ScrimDialog.vue";
import { onMounted, ref } from "vue";

interface Props {
    file: string;
    text?: string;
}

const props = withDefaults(defineProps<Props>(), {
    text: "View source",
});

const dialog = ref<InstanceType<typeof ScrimDialog>>();
const source = ref("Loading...");

onMounted(async function () {
    let data = fetch(props.file).then((response) => response.text());
    if (source.value) {
        source.value = await data;
    }
});
</script>
<template>
    <TextButton color="onPrimaryContainer" @click="dialog?.open()"
        ><span class="underline underline-offset-2">{{ props.text }}</span></TextButton
    >
    <ScrimDialog ref="dialog">
        <div class="p-4 whitespace-pre font-mono" v-text="source"></div>
    </ScrimDialog>
</template>
