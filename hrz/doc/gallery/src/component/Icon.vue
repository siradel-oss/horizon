<script setup lang="ts">
const props = defineProps<{
    icon: string;
}>();
</script>

<template>
    <span class="material-symbols-outlined-inherit">{{ props.icon }}</span>
</template>
