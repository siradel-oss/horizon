import { HrzDarkMode } from "@siradel/horizon-doc-common";

HrzDarkMode.initDarkMode();

document.addEventListener("DOMContentLoaded", () => {
    const darkModeToggle = document.getElementById("dark-light-switch");
    if (darkModeToggle) {
        darkModeToggle.addEventListener("click", () => {
            HrzDarkMode.toggleDarkMode();
        });
    }
});
