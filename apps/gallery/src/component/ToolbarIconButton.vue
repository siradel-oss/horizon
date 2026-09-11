<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import Icon from "@/component/Icon.vue";
import { computed } from "vue";

const props = defineProps<{
    icon: string;
    selected?: boolean;
    danger?: boolean;
    title?: string;
}>();

const emit = defineEmits<{
    (e: "click", event: MouseEvent): void;
}>();

const containerClass = computed(() => {
    if (props.selected) {
        return props.danger ? "bg-error text-onError" : "bg-primary text-onPrimary";
    }
    return "text-onSurfaceVariant hover:bg-surfaceContainerHigh hover:text-onSurface";
});
</script>

<template>
    <button
        :title="title"
        @click="emit('click', $event)"
        class="h-9 w-9 flex items-center justify-center rounded transition-colors select-none"
        :class="containerClass"
    >
        <Icon class="text-[22px]" :icon="icon" />
    </button>
</template>
