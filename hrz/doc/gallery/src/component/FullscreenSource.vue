<script setup lang="ts">
import TextButton from "./TextButton.vue";
import IconButton from "./IconButton.vue";

import { onMounted, ref } from "vue";

interface Props {
    file: string;
    text: string;
}

const props = withDefaults(defineProps<Props>(), {
    text: "View source",
});

const shown = ref(false);
const source = ref<HTMLElement | null>(null);

onMounted(async function () {
    let data = fetch(props.file).then((response) => response.text());
    if (source.value) {
        source.value.innerText = await data;
    }
});
</script>
<template>
    <p>
        <TextButton color="onSurfaceVariant" @click="shown = true">{{ props.text }}</TextButton>
    </p>
    <div class="fixed inset-0 bg-scrim/[75%] p-6 z-[9999]" :class="{ hidden: !shown }">
        <div
            class="relative max-w-[960px] mx-auto h-full bg-surface text-onSurface rounded-xl font-mono"
        >
            <div class="absolute top-2 right-2">
                <IconButton
                    icon="close"
                    color="onSurface"
                    class="text-[24px]"
                    @click="shown = false"
                />
            </div>
            <div class="overflow-auto h-full">
                <div class="p-4 whitespace-pre" ref="source">Loading...</div>
            </div>
        </div>
    </div>
</template>
