load("//tools/bazel:run_binary.bzl", "run_binary")

def http_server(name, port = 8080, dir = None, data = []):
    args = ["--port", str(port)]

    if dir != None:
        args += ["--directory", dir]

    run_binary(
        name = name,
        executable = "//tools/http_server",
        arguments = args,
        pass_user_arguments = True,
        data = data,
    )

    run_binary(
        name = name + "_tls",
        executable = "//tools/http_server",
        arguments = args + [
            "--tls",
        ],
        pass_user_arguments = True,
        data = data,
    )
