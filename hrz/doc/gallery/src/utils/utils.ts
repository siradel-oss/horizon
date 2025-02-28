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
