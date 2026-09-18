// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";
import * as path from "path";
import { nodeResolve } from "@rollup/plugin-node-resolve";
import tailwindcss from "@tailwindcss/vite";

const BACKEND_URL = process.env.VISUAL_TESTING_BACKEND ?? "http://127.0.0.1:5000";

export default defineConfig({
    plugins: [nodeResolve(), tailwindcss(), vue()],
    resolve: {
        alias: {
            "@": path.resolve(process.cwd(), "./src"),
        },
    },
    server: {
        proxy: {
            "/rpc": BACKEND_URL,
            "/images": BACKEND_URL,
        },
    },
    build: {
        target: "ES2021",
        sourcemap: false,
        outDir: "dist",
    },
});
