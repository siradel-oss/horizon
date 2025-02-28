<script setup lang="ts">
import { HrzDarkMode } from "@siradel/horizon-doc-common";
import { onMounted, ref } from "vue";
import * as Demos from "@/demos";
import DemoList from "@/DemoList.vue";
import NotFound from "@/NotFound.vue";
import DemoPage from "@/DemoPage.vue";

onMounted(() => {
    HrzDarkMode.initDarkMode();
});

let showList = ref<boolean>(true);
let demo = ref<Demos.Demo | null>(null);

let search = new URLSearchParams(location.search);
if (search.get("demo")) {
    showList.value = false;
    demo.value = Demos.byId[search.get("demo")?.toString() || ""];
}
</script>
<template>
    <div class="h-screen w-full bg-surfaceContainer text-onSurface">
        <DemoList v-if="showList" />
        <NotFound v-else-if="!demo" />
        <DemoPage v-else :demo="demo" />
    </div>
</template>
