<script setup lang="ts">
import { Demo } from "@/demos";

const props = defineProps<{
    demo: Demo;
}>();

function go() {
    location.href = `?demo=${props.demo.id}`;
}
</script>
<template>
    <div
        @click="go"
        class="relative w-[280px] bg-surface rounded-xl border border-outlineVariant cursor-pointer overflow-clip hover:shadow-md transition-shadow"
    >
        <div
            class="absolute inset-0 bg-onSurface opacity-0 hover:opacity-(--hover-alpha) active:opacity-(--active-alpha) transition-opacity"
        ></div>
        <div class="relative pointer-events-none" style="background: transparent">
            <img
                :src="`assets/thumbnails/${props.demo.def.thumbnailFile}`"
                class="aspect-video object-cover w-full rounded-b-xl"
            />
            <div class="p-4">
                <h3 class="text-mTitleLarge text-onSurfaceVariant">{{ props.demo.def.title }}</h3>
                <div class="mt-4 flex flex-row flex-wrap gap-2">
                    <span
                        v-for="tag in props.demo.def.tags"
                        class="inline-block text-onSurfaceVariant text-mLabelLarge"
                    >
                        #{{ tag }}
                    </span>
                </div>
            </div>
        </div>
    </div>
</template>
