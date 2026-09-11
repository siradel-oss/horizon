// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

import { onMounted, onBeforeUnmount } from "vue";

export function useWindowEvent(event: string, handler: EventListenerOrEventListenerObject) {
    onMounted(() => {
        window.addEventListener(event, handler);
    });

    onBeforeUnmount(() => {
        window.removeEventListener(event, handler);
    });
}
