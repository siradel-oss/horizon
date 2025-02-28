<script lang="ts" setup>
import { HrzProtocol } from "@siradel/horizon-protocol";
import { computed } from "vue";

const model = defineModel<HrzProtocol.IColor | null>();

interface Props {
    alpha: boolean;
}

const props = withDefaults(defineProps<Props>(), { alpha: false });

const htmlColor = computed({
    get() {
        const r = Math.round((model.value?.r || 0) * 255)
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
        const r = parseInt(value.slice(1, 3), 16) / 255;
        const g = parseInt(value.slice(3, 5), 16) / 255;
        const b = parseInt(value.slice(5, 7), 16) / 255;
        model.value = { r, g, b, a: model.value?.a || 1 };
    },
});
</script>
<template>
    <div class="flex flex-row space-x-4">
        <input type="color" v-model="htmlColor" class="flex-1" />
        <input
            type="range"
            min="0"
            max="1"
            step="any"
            v-if="model && props.alpha"
            v-model="model.a"
            class="flex-1"
        />
    </div>
</template>
