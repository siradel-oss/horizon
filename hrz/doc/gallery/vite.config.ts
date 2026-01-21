import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";
import * as path from "path";
import { nodeResolve } from "@rollup/plugin-node-resolve";
import tailwindcss from "@tailwindcss/vite";

export default defineConfig({
    mode: "production",
    base: "",
    plugins: [nodeResolve(), tailwindcss(), vue()],
    build: {
        target: "ES2021",
    },
    resolve: {
        preserveSymlinks: true,
        alias: {
            vue: path.resolve(__dirname, "node_modules/vue/dist/vue.esm-browser.prod.js"),
            "@": path.resolve(__dirname, "./src"),
            // Go up hrz/doc/gallery.
            $: path.resolve(__dirname, "../../../"),
        },
    },
});
