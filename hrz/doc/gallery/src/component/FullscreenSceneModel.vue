<script setup lang="ts">
import TextButton from "./TextButton.vue";
import ScrimDialog from "./ScrimDialog.vue";
import { ref } from "vue";

function simplifyModelInPlace(model: any) {
    for (let key in model) {
        let value = model[key];
        if (typeof value === "object") {
            simplifyModelInPlace(value);
            // If object is empty, remove it
            if (Object.keys(value).length === 0) {
                delete model[key];
            }
        }
    }
}

interface Props {
    text?: string;
    retrieveData: () => Promise<any>;
}

const props = withDefaults(defineProps<Props>(), {
    text: "View scene model",
});

const dialog = ref<InstanceType<typeof ScrimDialog>>();
const formattedData = ref("Loading...");

async function open() {
    let data = await props.retrieveData();
    simplifyModelInPlace(data);
    formattedData.value = JSON.stringify(data, null, 2);
    dialog.value?.open();
}
</script>
<template>
    <TextButton color="onSurfaceVariant" @click="open">{{ props.text }}</TextButton>
    <ScrimDialog ref="dialog">
        <div class="p-4 whitespace-pre font-mono" v-text="formattedData"></div>
    </ScrimDialog>
</template>
