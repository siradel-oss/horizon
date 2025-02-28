<script setup lang="ts">
import { ColorName, COLOR_CLASS } from "@/utils/colors";

export type Type = "button" | "submit" | "reset";

interface Props {
    color: ColorName;
    type: Type;
}

const props = withDefaults(defineProps<Props>(), {
    type: "button",
});
</script>

<template>
    <button
        class="relative h-10 px-3 inline-block text-mLabelLarge select-none"
        :class="COLOR_CLASS[props.color].text"
        :type="props.type"
    >
        <div
            class="absolute rounded-full w-full h-full inset-0 opacity-0 hover:opacity-hover active:opacity-active transition-opacity z-10"
            :class="COLOR_CLASS[props.color].bg"
        ></div>
        <slot></slot>
    </button>
</template>
