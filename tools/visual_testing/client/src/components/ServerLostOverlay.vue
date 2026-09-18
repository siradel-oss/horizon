<!--
    SPDX-FileCopyrightText: Copyright 2026 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import { Button } from "@/components/ui/button";
import { useServerHealth } from "@/composables/useServerHealth";

const { serverLost } = useServerHealth();

function reload() {
    window.location.reload();
}
</script>

<template>
    <div
        v-if="serverLost"
        class="fixed inset-0 z-100 flex items-center justify-center bg-black/80 p-6"
    >
        <div class="max-w-md space-y-3 rounded-lg bg-background p-6 text-center shadow-2xl">
            <h2 class="text-lg font-semibold">Server disconnected</h2>
            <p class="text-sm text-muted-foreground">
                The visual testing server stopped responding, so this session can't continue. Close
                this tab, or relaunch the server and reload this page.
            </p>
            <Button @click="reload">Reload</Button>
        </div>
    </div>
</template>
