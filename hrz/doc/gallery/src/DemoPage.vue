<script setup lang="ts">
import TopBar from "@/layout/TopBar.vue";
import * as Demos from "@/demos";

const props = defineProps<{
    demo: Demos.Demo;
}>();
</script>
<template>
    <div class="w-full h-full p-6 flex flex-col items-stretch">
        <TopBar class="flex-none z-10" :has-back-link="true" />
        <component :is="props.demo.component" class="flex-1 mt-6" />
    </div>
</template>
