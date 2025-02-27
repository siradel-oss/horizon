def hrz_genrule(cmd = "", **kwargs):
    native.genrule(
        cmd_bash = cmd,
        cmd_bat = cmd,
        **kwargs
    )
