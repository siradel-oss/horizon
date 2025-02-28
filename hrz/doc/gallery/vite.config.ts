import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";
import * as path from "path";
import { nodeResolve } from "@rollup/plugin-node-resolve";

export default defineConfig({
    mode: "production",
    base: "",
    plugins: [nodeResolve(), vue()],
    build: {
        target: "ES2020",
        rollupOptions: {
            input: {
                gallery: path.resolve(__dirname, "src/index.ts"),
            },
            output: {
                format: "es",
                entryFileNames: "[name].bundle.js",
            },
        },
    },
    resolve: {
        preserveSymlinks: true,
        alias: {
            vue: path.resolve(__dirname, "node_modules/vue/dist/vue.esm-browser.prod.js"),
            "@": path.resolve(__dirname, "./src"),
            // Go up bazel-out/<arch>/bin/hrz/doc/gallery.
            $: path.resolve(__dirname, "../../../../../../"),
        },
    },
});
