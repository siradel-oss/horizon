// SPDX-FileCopyrightText: Copyright 2018 Siradel
// SPDX-License-Identifier: MIT

module.exports = {
    target: "web",
    mode: "production",
    optimization: {
        // @Todo(webpack_protobufjs_concat) Scope hoisting rewrites the generated protocol's
        // `$protobuf.roots["default"] = {}` into an assignment to a call expression, which Terser
        // then rejects. Drop this once webpack stops inlining CommonJS re-exports in assignment
        // targets.
        concatenateModules: false,
    },
    output: {
        filename: "bundle.js",
    },
    resolve: {
        preferRelative: true,
    },
};
