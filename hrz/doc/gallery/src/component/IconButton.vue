<script setup lang="ts">
import Icon from "@/component/Icon.vue";
import { ColorName, COLOR_CLASS } from "@/utils/colors";

const props = defineProps<{
    icon: string;
    color: ColorName;
}>();
</script>

<template>
    <button class="relative h-10 w-10 inline-block select-none">
        <div
            class="absolute w-full h-full inset-0 z-10 rounded-full opacity-0 hover:opacity-(--hover-alpha) active:opacity-(--active-alpha) transition-opacity"
            :class="COLOR_CLASS[props.color].bg"
        ></div>
        <Icon
            class="absolute top-2 left-2"
            :class="COLOR_CLASS[props.color].text"
            :icon="props.icon"
        />
    </button>
</template>
