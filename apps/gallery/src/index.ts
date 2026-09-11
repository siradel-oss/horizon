// SPDX-FileCopyrightText: Copyright 2018 Siradel
// SPDX-License-Identifier: MIT

import { createApp } from "vue";
import App from "./App.vue";
import * as Demos from "./demos";
import "./style.css";

export function init(target: string | HTMLElement, demoId: string) {
    const el =
        typeof target === "string" ? (document.querySelector(target) as HTMLElement) : target;
    el.classList.add("hrz-gallery");

    const demo = Demos.byId[demoId] ?? null;
    const app = createApp(App, { demo });
    app.config.globalProperties.$filters = {
        formatNumber: function (value: number, decimals: number = 0): string {
            return value.toFixed(decimals);
        },
    };
    app.mount(el);
}
