import Long from "long";

export function deepAssign(from: any, to: any) {
    for (const key of Object.getOwnPropertyNames(from)) {
        if (typeof from[key] === "object") {
            if (!Object.hasOwnProperty.call(to, key)) {
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

export function eqLong(
    a: number | Long | null | undefined,
    b: number | Long | null | undefined
): boolean {
    if (a === null || a === undefined || b === null || b === undefined) {
        return true;
    }

    if (Long.isLong(a)) {
        return a.eq(b);
    }
    if (Long.isLong(b)) {
        return b.eq(a);
    }
    return a === b;
}
