// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

type DemoEntry = { title: string; thumbnailFile: string; tags: string[] };

// Dynamic import and fetch (not static imports) keep this module free of shared
// dependencies with ./index.ts, so Rollup emits the gallery entry as a single
// self-contained JS+CSS bundle with no extracted shared chunks.
const demoId = new URLSearchParams(location.search).get("demo");

if (demoId) {
    document.getElementById("gallery")!.style.display = "none";
    const appEl = document.getElementById("app")!;
    appEl.style.display = "block";
    import("./index").then(({ init }) => init(appEl, demoId));
} else {
    fetch("./galleryDemos.json")
        .then((r) => r.json() as Promise<Record<string, DemoEntry>>)
        .then((demos) => {
            const grid = document.getElementById("demo-grid")!;
            for (const [id, demo] of Object.entries(demos)) {
                const a = document.createElement("a");
                a.href = `?demo=${id}`;
                a.className = "demo-card";
                a.innerHTML = `<img src="assets/thumbnails/${demo.thumbnailFile}" alt="${demo.title}"><div class="demo-card-label">${demo.title}</div>`;
                grid.appendChild(a);
            }
        });
}
