<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import { computed } from "vue";
import { Badge } from "@/components/ui/badge";
import { STATUS_BADGE, type TestStatus } from "@/composables/useReport";

const props = defineProps<{ status: TestStatus }>();

const badge = computed(() => STATUS_BADGE[props.status]);
</script>

<template>
    <Badge :variant="badge.variant" :class="badge.class">{{ badge.label }}</Badge>
</template>
