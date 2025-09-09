<script setup lang="ts">
import IconButton from "./IconButton.vue";
import { ref } from "vue";

const shown = ref(false);
defineExpose({ open: () => (shown.value = true) });
</script>
<template>
    <div class="fixed inset-0 bg-scrim/[75%] p-6 z-[9999]" v-if="shown" @click.self="shown = false">
        <div class="relative max-w-[960px] mx-auto h-full bg-surface text-onSurface rounded-xl">
            <div class="absolute top-2 right-2">
                <IconButton
                    icon="close"
                    color="onSurface"
                    class="text-[24px]"
                    @click="shown = false"
                />
            </div>
            <div class="overflow-auto h-full">
                <slot></slot>
            </div>
        </div>
    </div>
</template>
