<script setup lang="ts">
import TextButton from "./TextButton.vue";
import ScrimDialog from "./ScrimDialog.vue";
import { onMounted, ref } from "vue";

interface Props {
    file: string;
    text?: string;
}

const props = withDefaults(defineProps<Props>(), {
    text: "View source",
});

const dialog = ref();
const source = ref("Loading...");

onMounted(async function () {
    let data = fetch(props.file).then((response) => response.text());
    if (source.value) {
        source.value = await data;
    }
});
</script>
<template>
    <TextButton color="onSurfaceVariant" @click="dialog.open()">{{ props.text }}</TextButton>
    <ScrimDialog ref="dialog">
        <div class="p-4 whitespace-pre" v-text="source"></div>
    </ScrimDialog>
</template>
