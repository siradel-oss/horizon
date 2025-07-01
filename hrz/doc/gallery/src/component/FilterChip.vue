<script setup lang="ts">
import Icon from "@/component/Icon.vue";
import { computed } from "vue";

let props = defineProps<{
    selected: boolean;
    disabled: boolean;
}>();

const styleUnselectedContainer = "px-4 border border-outline text-onSurfaceVariant";
const styleUnselectedStateLayer = "bg-onSurfaceVariant";
const styleSelectedContainer = " pl-2 pr-4 bg-secondaryContainer text-onSecondaryContainer";
const styleSelectedStateLayer = "bg-onSecondaryContainer";

const styleEnabledContainer = "cursor-pointer";
const styleEnabledStateLayer =
    "opacity-0 hover:opacity-(--hover-alpha) active:opacity-(--active-alpha)";

const styleDisabledContainer =
    "px-4 border border-onSurface/[12%] bg-onSurface/[12%] text-onSurface/[38%]";
const styleDisabledStateLayer = "opacity-0";

let styleContainer = computed<string>(() => {
    if (props.disabled) {
        return styleDisabledContainer;
    } else {
        if (props.selected) {
            return styleSelectedContainer + " " + styleEnabledContainer;
        } else {
            return styleUnselectedContainer + " " + styleEnabledContainer;
        }
    }
});

let styleStateLayer = computed<string>(() => {
    if (props.disabled) {
        return styleDisabledStateLayer;
    } else {
        if (props.selected) {
            return styleSelectedStateLayer + " " + styleEnabledStateLayer;
        } else {
            return styleUnselectedStateLayer + " " + styleEnabledStateLayer;
        }
    }
});
</script>
<template>
    <div
        class="relative inline-block h-8 rounded-[8px] text-mLabelLarge overflow-clip transition-color select-none"
        :class="styleContainer"
    >
        <div
            class="inline-block absolute inset-0 transition-opacity transition-color"
            :class="styleStateLayer"
        ></div>
        <div class="pointer-events-none h-full flex flex-row gap-2 items-center">
            <Icon icon="check" v-if="props.selected" class="text-[18px]" />
            <span>
                <slot></slot>
            </span>
        </div>
    </div>
</template>
