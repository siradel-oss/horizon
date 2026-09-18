// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

import { reactive } from "vue";
import { useReport } from "@/composables/useReport";

export type ImageKind = "reference" | "capture" | "diff";

// Image URLs carry a cache key because recreating an <img> with an unchanged URL does *not*
// reliably issue a request: the browser serves it from its in-memory cache.
//
// The key is per-image so only what actually changed gets re-fetched:
//   - captures/diffs are written by the run that produced a test's result, so the result's own
//     date changes exactly when those files change — no bookkeeping needed.
//   - references are only rewritten by the regenerate actions below, per test, so a per-test
//     counter covers them without asking the server for anything.
const referenceVersions = reactive(new Map<string, number>());

export function useImages() {
    const { resultFor } = useReport();

    function cacheKey(testName: string, kind: ImageKind): string {
        if (kind === "reference") return `r${referenceVersions.get(testName) ?? 0}`;
        return resultFor(testName)?.date ?? "none";
    }

    function imageUrl(testName: string, kind: ImageKind): string {
        const key = encodeURIComponent(cacheKey(testName, kind));
        return `/images/${encodeURIComponent(testName)}/${kind}?v=${key}`;
    }

    // Call after regenerating the reference image(s) of the given tests.
    function bumpReferenceVersion(...testNames: string[]) {
        for (const name of testNames) {
            referenceVersions.set(name, (referenceVersions.get(name) ?? 0) + 1);
        }
    }

    return { imageUrl, bumpReferenceVersion };
}
