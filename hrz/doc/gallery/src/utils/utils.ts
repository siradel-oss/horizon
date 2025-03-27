export function deepAssign(from: any, to: any) {
    for (const key of Object.getOwnPropertyNames(from)) {
        if (typeof from[key] === "object") {
            if (!Object.hasOwn(to, key)) {
                to[key] = {};
            }
            deepAssign(from[key], to[key]);
        } else {
            to[key] = from[key];
        }
    }
}

export function debounce(callback: (...args: any) => void, waitMs: number): (...args: any) => void {
    if (waitMs > 0) {
        let timeoutId: number | undefined = undefined;
        return (...args: any) => {
            window.clearTimeout(timeoutId);
            timeoutId = window.setTimeout(() => {
                callback(...args);
            }, waitMs);
        };
    } else {
        return callback;
    }
}
