load("//tools/bazel:run_binary.bzl", "run_binary")

def http_server(name, port = 8080, dir = None, data = []):
    args = [str(port)]
    if dir != None:
        args.append(dir)

    run_binary(
        name = name,
        executable = "//tools/http_server",
        arguments = args,
        data = data,
    )
