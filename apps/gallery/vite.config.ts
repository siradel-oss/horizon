// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

import {
    defineConfig,
    type ConfigEnv,
    type Plugin,
    type UserConfig,
    createLogger,
    ViteDevServer,
} from "vite";
import vue from "@vitejs/plugin-vue";
import * as path from "path";
import * as fs from "fs";
import { nodeResolve } from "@rollup/plugin-node-resolve";
import tailwindcss from "@tailwindcss/vite";
import postcss from "postcss";

const logger = createLogger();
const GALLERY_PREFIX = ".hrz-gallery";

function prefixSelector(selector: string, prefix: string): string {
    const s = selector.trim();

    // Selector IS the container itself (boundary reset rules) — keep as-is
    if (s === prefix) return s;

    // :root → .hrz-gallery
    if (s === ":root") return prefix;
    // :root[...] or :root.class etc → .hrz-gallery[...] / .hrz-gallery.class
    if (s.startsWith(":root")) return prefix + s.slice(":root".length);

    // html / body (bare or with modifiers, but not with a descendant combinator)
    // e.g. "html" → ".hrz-gallery", "html[lang]" → ".hrz-gallery[lang]"
    const rootElementMatch = s.match(/^(html|body)([\[.:#].*)?$/);
    if (rootElementMatch) return prefix + (rootElementMatch[2] ?? "");

    // Default: scope as descendant of the gallery container
    return `${prefix} ${s}`;
}

function scopeCss(css: string, prefix: string): string {
    const root = postcss.parse(css);

    root.walkRules((rule) => {
        // Skip rules inside @keyframes
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        for (let p: any = rule.parent; p; p = p.parent) {
            if (p.type === "atrule" && p.name === "keyframes") return;
        }
        rule.selectors = rule.selectors.map((sel) => prefixSelector(sel, prefix));
    });

    return root.toResult().css;
}

// The purpose of this plugin is to prefix all CSS selectors in the gallery build with a
// specific class (GALLERY_PREFIX) to avoid style conflicts when the gallery is embedded
// in other pages.
function prefixGalleryCss(): Plugin {
    return {
        name: "prefix-gallery-css",
        apply: "build",
        generateBundle(_, bundle) {
            for (const chunk of Object.values(bundle)) {
                if (
                    chunk.type === "asset" &&
                    typeof chunk.source === "string" &&
                    chunk.fileName.endsWith(".css")
                ) {
                    chunk.source = scopeCss(chunk.source, GALLERY_PREFIX);
                }
            }
        },
    };
}

// The manifest option kept crashing vite for some reason, so here we are
function customManifestPlugin(): Plugin {
    return {
        name: "custom-manifest-generator",
        enforce: "post",
        generateBundle(_options, bundle) {
            const manifest: Record<string, { js: string[]; css: string[] }> = {};
            for (const item of Object.values(bundle)) {
                if (item.type === "chunk" && item.isEntry) {
                    const entryName = item.name;
                    if (!manifest[entryName]) {
                        manifest[entryName] = { js: [], css: [] };
                    }
                    manifest[entryName].js.push(item.fileName);
                    if (item.viteMetadata?.importedCss) {
                        manifest[entryName].css.push(...item.viteMetadata.importedCss);
                    }
                }
            }
            this.emitFile({
                type: "asset",
                fileName: "manifest.json",
                source: JSON.stringify(manifest, null, 2),
            });
        },
    };
}

type RequestMatcher = (url: string) => string | null;

interface ServeFilesPluginOptions {
    matchers: Array<RequestMatcher>;
    additionalHeaders?: Record<string, string>;
}

function matchExact(url: string, localPath: string): RequestMatcher {
    localPath = path.resolve(process.cwd(), localPath);
    return (requestUrl: string) => (requestUrl === url ? localPath : null);
}

function matchBasename(basename: string, localPath: string): RequestMatcher {
    localPath = path.resolve(process.cwd(), localPath);
    return (requestUrl: string) => (path.basename(requestUrl) === basename ? localPath : null);
}

function matchDir(urlPrefix: string, localDirPath: string): RequestMatcher {
    localDirPath = path.resolve(process.cwd(), localDirPath);
    return (requestUrl: string) => {
        if (requestUrl.startsWith(urlPrefix)) {
            return path.join(localDirPath, requestUrl.slice(urlPrefix.length));
        } else {
            return null;
        }
    };
}

const MIME_TYPES_BY_EXT: Record<string, string> = {
    js: "text/javascript",
    wasm: "application/wasm",
    json: "application/json",
};

function contentTypeFor(filePath: string): string {
    const ext = path.extname(filePath).slice(1);
    return MIME_TYPES_BY_EXT[ext] ?? "application/octet-stream";
}

function serveFilesPlugin(options: ServeFilesPluginOptions): Plugin {
    return {
        name: "serve-files",
        apply: "serve",
        configureServer(server: ViteDevServer) {
            server.middlewares.use((req, res, next) => {
                function tryServeFile(filePath: string) {
                    fs.readFile(filePath, (err, data) => {
                        if (err) {
                            res.statusCode = 404;
                            res.end("Not Found");
                        } else {
                            res.setHeader("Content-Type", contentTypeFor(filePath));
                            for (const [header, value] of Object.entries(
                                options.additionalHeaders ?? {}
                            )) {
                                res.setHeader(header, value);
                            }
                            res.end(data);
                        }
                    });
                }

                const urlPath = req.url?.split("?")[0] || "";
                for (const matcher of options.matchers) {
                    const filePath = matcher(urlPath);
                    if (filePath) {
                        tryServeFile(filePath);
                        return;
                    }
                }

                next();
            });
        },
    };
}

const ADDITIONAL_HEADERS: Record<string, string> = {
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Embedder-Policy": "require-corp",
};

const serveFilesPluginConfig = serveFilesPlugin({
    additionalHeaders: ADDITIONAL_HEADERS,
    matchers: [
        matchExact("/galleryDemos.json", "src/galleryDemos.json"),
        matchDir("/assets/thumbnails", "thumbnails"),
        matchDir("/assets/demo", "demo_assets"),
        matchDir("/source", "src/demo"),
        matchDir("/scene", "dist_scenes/scene"),
        matchBasename("hrz_core.js", "node_modules/@siradel-oss/horizon-core/dist/hrz_core.js"),
        matchBasename("hrz_core", "node_modules/@siradel-oss/horizon-core/dist/hrz_core.js"),
        matchBasename("hrz_core.wasm", "node_modules/@siradel-oss/horizon-core/dist/hrz_core.wasm"),
    ],
});

// `--mode` is passed by Bazel.
export default defineConfig(({ mode }: ConfigEnv): UserConfig => {
    const isDevelopment = mode === "development";

    return {
        root: fs.realpathSync.native(path.resolve("./")),
        base: "",
        plugins: [
            nodeResolve(),
            tailwindcss(),
            vue(),
            prefixGalleryCss(),
            customManifestPlugin(),
            serveFilesPluginConfig,
        ],
        build: {
            target: "ES2021",
            sourcemap: isDevelopment,
            minify: !isDevelopment,
            rollupOptions: {
                input: {
                    app: "index.html",
                    gallery: "src/index.ts",
                },
                preserveEntrySignatures: "exports-only",
            },
        },
        resolve: {
            alias: {
                "@": path.resolve(process.cwd(), "./src"),
            },
        },
        server: {
            headers: ADDITIONAL_HEADERS,
        },
    };
});
