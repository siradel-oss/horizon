export type Theme = "light" | "dark";

const LOCAL_STORAGE_THEME_KEY = "hrz-dark-mode";

let lastTheme: Theme = "light";
let darkModeChangedCallbacks: ((theme: Theme) => void)[] = [];

export function onDarkModeChange(callback: (theme: Theme) => void) {
    callback(lastTheme);
    darkModeChangedCallbacks.push(callback);
}

function getSystemTheme(): Theme {
    return window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
}

function setTheme(theme: Theme) {
    document.documentElement.classList.remove("light", "dark");
    document.documentElement.classList.add(theme);

    if (theme === getSystemTheme()) {
        localStorage.removeItem(LOCAL_STORAGE_THEME_KEY);
    } else {
        localStorage.setItem(LOCAL_STORAGE_THEME_KEY, theme);
    }

    lastTheme = theme;
    darkModeChangedCallbacks.forEach((callback) => callback(theme));
}

function getPreferredTheme(): Theme {
    return (localStorage.getItem(LOCAL_STORAGE_THEME_KEY) as Theme) || getSystemTheme();
}

window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", () => {
    if (localStorage.getItem(LOCAL_STORAGE_THEME_KEY) == null) {
        setTheme(getSystemTheme());
    }
});

export function toggleDarkMode() {
    if (getPreferredTheme() === "light") {
        setTheme("dark");
    } else {
        setTheme("light");
    }
}

export function initDarkMode() {
    setTheme(getPreferredTheme());
}
