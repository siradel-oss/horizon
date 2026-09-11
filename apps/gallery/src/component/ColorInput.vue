<!--
    SPDX-FileCopyrightText: Copyright 2025 Siradel
    SPDX-License-Identifier: MIT
-->

<script lang="ts" setup>
import { debounce } from "@/utils/utils";
import { colorFromHex } from "@/utils/colors";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { computed, ref, watch } from "vue";

const emits = defineEmits(["update:modelValue", "change"]);

interface Props {
    alpha?: boolean;
    modelValue: HrzProtocol.Color.$Shape;
    modelModifiers?: any;
}

const props = withDefaults(defineProps<Props>(), {
    alpha: false,
    modelModifiers: null,
});

const model = ref(props.modelValue);
const isDebounce = !!props.modelModifiers?.debounce;

watch(props.modelValue, (value) => {
    model.value = value;
});

const htmlColor = computed({
    get() {
        const r = Math.round((model.value.r || 0) * 255)
            .toString(16)
            .padStart(2, "0");
        const g = Math.round((model.value?.g || 0) * 255)
            .toString(16)
            .padStart(2, "0");
        const b = Math.round((model.value?.b || 0) * 255)
            .toString(16)
            .padStart(2, "0");
        return `#${r}${g}${b}`;
    },
    set(value: string) {
        model.value = colorFromHex(value);
    },
});

const notifyUpdate = debounce(
    (value: HrzProtocol.Color.$Shape) => {
        emits("update:modelValue", value);
    },
    isDebounce ? 300 : 0
);

function onColorChange(event: Event) {
    const target = event.target as HTMLInputElement;
    htmlColor.value = target.value;
    notifyUpdate(model.value);
}

function onAlphaChange(event: Event) {
    const target = event.target as HTMLInputElement;
    model.value = { ...model.value, a: parseFloat(target.value) };
    notifyUpdate(model.value);
}
</script>
<template>
    <div class="flex flex-row space-x-4">
        <input
            type="color"
            :value="htmlColor"
            @input="onColorChange"
            @change="onColorChange"
            class="flex-1"
        />
        <input
            type="range"
            min="0"
            max="1"
            step="any"
            v-if="model && props.alpha"
            :value="model.a"
            @input="onAlphaChange"
            @change="onAlphaChange"
            class="flex-1"
        />
    </div>
</template>
