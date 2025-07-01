<script setup lang="ts">
import Icon from "@/component/Icon.vue";

const props = defineProps<{
    icon?: string;
    disabled?: boolean;
}>();
</script>

<template>
    <button
        :disabled="props.disabled"
        class="h-10 rounded-full select-none flex items-center flex-row gap-2 px-4 text-mLabelLarge bg-primaryContainer text-onPrimaryContainer disabled:bg-secondaryContainer disabled:text-onSecondaryContainer disabled:opacity-50 transition-shadow enabled:hover:shadow-md"
    >
        <Icon v-if="props.icon" :icon="props.icon" class="text-[18px]" />
        <div><slot></slot></div>
    </button>
</template>
