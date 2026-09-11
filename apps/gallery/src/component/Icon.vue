<!--
    SPDX-FileCopyrightText: Copyright 2024 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
const props = defineProps<{
    icon: string;
}>();
</script>

<template>
    <span class="material-symbols-outlined-inherit">{{ props.icon }}</span>
</template>
