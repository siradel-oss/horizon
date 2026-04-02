This is a custom Bazel modules registry we use to store custom modules, or
modules that we have customised.

All modules should have the `.hrz.n` suffix, where `n` is a number that we
increment when we update the module. This allows us to easily identify which
modules are from the BCR and which are our custom versions.

Note that once pushed to the repository, all modules are considered immutable. You can edit a module that has never left your machine, however this might create issues with Bazel's cache. This cache can be cleared by deleting the directory that can be found with `bazel info repository_cache`, then running `bazel shutdown`.

## Create a module from a non-Bazel archive

First create the `MODULE.bazel` and `BUILD.bazel` files for the module.
To do so, you can download and extract the archive and setup the Bazel build manually.
You can then use `local_path_override` in Horizon's `MODULE.bazel` to point to the local directory where you extracted the archive in order to test it.

```python
bazel_dep(name = "my_module", version = "x.y.z")
local_path_override(
    module_name = "my_module",
    path = "../my_module_checkout",
)
```

When this is done and the build works.

1. `python tools/create_from_archive.py <module_name> <version> <url> <prefix>` to create the Bazel archive for the module.
2. Copy the `MODULE.bazel` file to `modules/<module_name>/<version>/MODULE.bazel`.
3. Copy the `BUILD.bazel` file to `modules/<module_name>/<version>/overlay/BUILD.bazel`.
4. Create the `MODULE.bazel` file in `modules/<module_name>/<version>/overlay/MODULE.bazel` with the content: `../MODULE.bazel`.
5. In the `modules/<module_name>/<version>/source.json` file, add the following content:
```json
{
    "overlay": {
        "MODULE.bazel": "",
        "BUILD.bazel": ""
    }
}
```
6. Update the integrity values for the module by running `python tools/update_integrity.py <module_name> <version>`.

You can now remove the local path override and use the module from the registry.

## Create a module from a non-Bazel Github repository with a tag

The process is similar to the one described above, but you can use `create_from_github_tag.py` instead of `create_from_archive.py` and provide the repository and tag instead of the URL and prefix.

## Create a module from a non-Bazel Github repository with a commit id

The process is similar to the one described above, but you can use `create_from_github_commit.py` instead of `create_from_archive.py` and provide the repository and commit id instead of the URL and prefix.

## Create a module from a Bazel Github repository with a tag

This is useful for importing a Git repository that contains a Bazel module but that is not present in the BCR.

Simply run the `create_from_bazel_github_tag.py` script with the repository and tag, and it will create the module for you and update the version number if you provided one that differs from the one in the repository.

## Customize a module from the Bazel Central Registry

When you want to copy a module from the BCR to customize it, do the following:

1. `python tools/copy_from_bcr.py <module_name> <version> <new_version>` where `new_version` is "our" version of the package. It should be suffixed by something like `.hrz.1`, similarly to BCR's `.bcr.n`.
2. Modify the files as necessary.
3. `python tools/update_integrity.py <module_name> <version>` to update the integrity values for the module.
