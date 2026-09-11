<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
interface Props {
    active?: boolean;
}

withDefaults(defineProps<Props>(), { active: false });

const emit = defineEmits<{
    (e: "click", event: MouseEvent): boolean;
}>();
</script>

<template>
    <button
        class="relative h-10 px-4 rounded-lg text-mLabelLarge select-none transition-colors"
        :class="
            active
                ? 'bg-primaryContainer text-onPrimaryContainer'
                : 'bg-surfaceContainerHigh text-onSurfaceVariant hover:bg-surfaceContainerHighest'
        "
        @click="emit('click', $event)"
    >
        <slot></slot>
    </button>
</template>
