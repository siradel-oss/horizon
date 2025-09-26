<script lang="ts" setup>
import { debounce } from "@/utils/utils";
import { colorFromHex } from "@/utils/colors";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { computed, ref, watch } from "vue";

const emits = defineEmits(["update:modelValue", "change"]);

interface Props {
    alpha?: boolean;
    modelValue: HrzProtocol.IColor;
    modelModifiers?: any;
}

const props = withDefaults(defineProps<Props>(), {
    alpha: false,
    modelModifiers: null,
});

const model = ref(props.modelValue);
const isLazy = !!props.modelModifiers?.lazy;
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
    (value: HrzProtocol.IColor, isInputEvent: boolean) => {
        if (!isLazy && !isInputEvent) {
            emits("update:modelValue", value);
        }
    },
    isDebounce ? 300 : 0
);

function onColorChange(event: InputEvent, isInputEvent: boolean) {
    const target = event.target as HTMLInputElement;
    htmlColor.value = target.value;
    notifyUpdate(model.value);
}

function onAlphaChange(event: InputEvent, isInputEvent: boolean) {
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
            @input="(event) => onColorChange(event as InputEvent, true)"
            @change="(event) => onColorChange(event as InputEvent, false)"
            class="flex-1"
        />
        <input
            type="range"
            min="0"
            max="1"
            step="any"
            v-if="model && props.alpha"
            :value="model.a"
            @input="(event) => onAlphaChange(event as InputEvent, true)"
            @change="(event) => onAlphaChange(event as InputEvent, false)"
            class="flex-1"
        />
    </div>
</template>
