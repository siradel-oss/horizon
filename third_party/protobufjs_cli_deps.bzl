# protobufjs lists those under "cliDependencies" instead of "dependencies" so
# npm doesn't fetch them until execution, which is not allowed with Bazel...
PROTOBUFJS_CLI_DEPS = [
    "//:node_modules/semver",
    "//:node_modules/chalk",
    "//:node_modules/glob",
    "//:node_modules/jsdoc",
    "//:node_modules/minimist",
    "//:node_modules/tmp",
    "//:node_modules/uglify-js",
    "//:node_modules/espree",
    "//:node_modules/escodegen",
    "//:node_modules/estraverse",
]
