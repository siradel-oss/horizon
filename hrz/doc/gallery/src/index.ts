import { createApp } from "vue";
import App from "./App.vue";

const app = createApp(App);

app.config.globalProperties.$filters = {
    formatNumber: function (value: number, decimals: number = 0): string {
        return value.toFixed(decimals);
    },
};

app.mount("#app");
