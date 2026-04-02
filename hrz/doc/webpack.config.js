const path = require("path");

let EXAMPLES = (function() {
    if (process.env.EXAMPLES) {
        return process.env.EXAMPLES.split("|");
    }
    return [];
})();

let VARIANT = process.env.VARIANT || "default";

let entries = {};

entries["shared"] = [
    "@siradel/horizon-core",
    "@siradel/horizon-protocol",
    "@siradel/horizon-api",
    "@siradel/horizon-monitoring-protocol",
    "@siradel/horizon-doc-common",
    "vue",
];

for (let example of EXAMPLES) {
    entries["example." + example] = {
        import: `./dist_${VARIANT}/example/${example}.js`,
        dependOn: "shared",
    };
}

entries["ui"] = {
    import: `./dist_${VARIANT}/ui/index.js`,
};

module.exports = {
    resolve: {
        extensions: [".js"],
        alias: {
            "vue$": "vue/dist/vue.esm.js"
        },
    },
    entry: entries,
    output: {
        filename: `js_${VARIANT}/[name].bundle.js`
    }
};
