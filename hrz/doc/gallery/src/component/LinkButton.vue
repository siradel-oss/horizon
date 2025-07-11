<script setup lang="ts">
import { ColorName, COLOR_CLASS } from "@/utils/colors";

interface Props {
    color: ColorName;
    href?: string;
}

const props = withDefaults(defineProps<Props>(), {});
</script>

<template>
    <a
        :href="props.href"
        class="relative h-10 px-3 inline-block text-mLabelLarge select-none"
        :class="COLOR_CLASS[props.color].text"
        style="display: flex; justify-content: center; align-items: center"
    >
        <div
            class="absolute rounded-full w-full h-full inset-0 opacity-0 hover:opacity-(--hover-alpha) active:opacity-(--active-alpha) transition-opacity z-10"
            :class="COLOR_CLASS[props.color].bg"
        ></div>
        <slot></slot>
    </a>
</template>
