<!--
    SPDX-FileCopyrightText: Copyright 2025 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import Icon from "@/component/Icon.vue";

const props = defineProps<{
    icon?: string;
    disabled?: boolean;
}>();

const emit = defineEmits<{
    (e: "click", event: MouseEvent): boolean;
}>();
</script>

<template>
    <button
        :disabled="props.disabled"
        class="h-10 rounded-full select-none flex items-center flex-row gap-2 px-4 text-mLabelLarge bg-primaryContainer text-onPrimaryContainer disabled:bg-secondaryContainer disabled:text-onSecondaryContainer disabled:opacity-50 transition-shadow enabled:hover:shadow-md"
        @click="emit('click', $event)"
    >
        <Icon v-if="props.icon" :icon="props.icon" class="text-[18px]" />
        <div><slot></slot></div>
    </button>
</template>
