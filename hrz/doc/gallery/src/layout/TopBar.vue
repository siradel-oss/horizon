<script setup lang="ts">
import { HrzApi } from "@siradel/horizon-api";
import { HrzDarkMode } from "@siradel/horizon-doc-common";
import { onMounted, ref, computed } from "vue";
import IconButton from "@/component/IconButton.vue";
import TextButton from "@/component/TextButton.vue";
import { go } from "@/utils/browser";

const props = defineProps<{
    hasBackLink: boolean;
}>();

const darkMode = ref<boolean>(false);

const icon = computed<string>(() => {
    return darkMode.value ? "dark_mode" : "light_mode";
});

onMounted(() => {
    HrzDarkMode.onDarkModeChange((mode) => (darkMode.value = mode == "dark"));
});
</script>
<template>
    <div class="flex flex-row items-center gap-6 text-onSurfaceVariant">
        <img src="$/hrz/branding/logo.svg" class="h-12" />
        <p class="text-mHeadlineSmall">Horizon gallery</p>
        <p class="text-mTitleSmall">Version {{ HrzApi.VERSION }}</p>
        <div class="grow"></div>
        <TextButton v-if="props.hasBackLink" color="onSurfaceVariant" @click="go('index.html')"
            >Back to list</TextButton
        >
        <TextButton color="onSurfaceVariant" @click="go('../')">Documentation</TextButton>
        <IconButton
            :icon="icon"
            color="onSurfaceVariant"
            @click="HrzDarkMode.toggleDarkMode"
            class="text-[24px]"
        />
    </div>
</template>
