<!--
    SPDX-FileCopyrightText: Copyright 2024 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import * as Demos from "@/demos";

const props = defineProps<{
    demo: Demos.Demo;
}>();
</script>
<template>
    <component :is="props.demo.component" class="w-full grow absolute inset-0" />
</template>
