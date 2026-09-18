// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

import { ref } from "vue";

const viewedTestName = ref<string | null>(null);

export function useViewedTest() {
    return { viewedTestName };
}
