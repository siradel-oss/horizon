<!--
    SPDX-FileCopyrightText: Copyright 2024 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import * as Demos from "@/demos";
import NotFound from "@/NotFound.vue";
import DemoPage from "@/DemoPage.vue";

const props = defineProps<{
    demo: Demos.Demo | null;
}>();
</script>
<template>
    <NotFound v-if="!props.demo" />
    <DemoPage v-else :demo="props.demo" />
</template>
