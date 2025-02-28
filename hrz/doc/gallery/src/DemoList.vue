<script setup lang="ts">
import TopBar from "@/layout/TopBar.vue";
import DemoCard from "@/component/DemoCard.vue";
import FilterChip from "@/component/FilterChip.vue";
import TextButton from "@/component/TextButton.vue";
import * as Demos from "@/demos";
import { onBeforeMount, ref, computed } from "vue";

type Tag = {
    name: string;
    selected: boolean;
};

type TagCount = {
    name: string;
    selected: boolean;
    count: number;
};

let allTags = ref<Tag[]>([]);

onBeforeMount(() => {
    allTags.value = Array.from(new Set(Demos.all.flatMap((d) => d.def.tags)))
        .sort()
        .map((tag) => ({ name: tag, selected: false }));
});

const demos = computed<Demos.Demo[]>(() => {
    const anyTagSelected = allTags.value.some((t) => t.selected);
    if (!anyTagSelected) {
        return Demos.all;
    } else {
        return Demos.all.filter((d) =>
            allTags.value.filter((t) => t.selected).every((t) => d.def.tags.includes(t.name))
        );
    }
});

const tagsWithCount = computed<TagCount[]>(() => {
    return allTags.value.map((t) => {
        const count = demos.value.filter((d) => d.def.tags.includes(t.name)).length;
        return { ...t, count };
    });
});

const hasSelectedTags = computed<boolean>(() => {
    return allTags.value.some((t) => t.selected);
});

function toggleSelected(name: string, count: number) {
    if (count > 0) {
        const tag = allTags.value.find((t) => t.name === name);
        if (tag) {
            tag.selected = !tag.selected;
        }
    }
}

function clearSelectedTags() {
    allTags.value.forEach((t) => (t.selected = false));
}
</script>
<template>
    <div class="w-full lg:max-w-[1280px] mx-auto p-6 flex flex-col items-stretch">
        <TopBar class="flex-none z-10" :has-back-link="false" />
        <div class="mt-6 bg-surface rounded-2xl p-4 w-full flex-1">
            <div class="w-full flex flex-row items-center justify-between gap-8">
                <h1 class="text-onSurfaceVariant text-mHeadlineLarge">Browse demos</h1>
                <TextButton
                    v-if="hasSelectedTags"
                    color="onSurfaceVariant"
                    @click="clearSelectedTags"
                    >Clear filters</TextButton
                >
            </div>
            <div class="py-4 flex flex-row flex-wrap justify-start gap-4">
                <FilterChip
                    v-for="tag in tagsWithCount"
                    :key="tag.name"
                    :selected="tag.selected"
                    :disabled="tag.count == 0"
                    @click="() => toggleSelected(tag.name, tag.count)"
                >
                    {{ tag.name }}<span v-if="!tag.selected"> ({{ tag.count }})</span>
                </FilterChip>
            </div>
            <div class="py-4 flex flex-row flex-wrap justify-start items-start gap-4">
                <DemoCard v-for="d in demos" :demo="d" />
            </div>
        </div>
    </div>
</template>
