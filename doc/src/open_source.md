# Open-source tooling

## Publishing a branch to the open-source repository

To publish code on the open-source repository using the `ci/oss_publish/publish.py` script, you must either:

- **(Recommended)** Authenticate as the "Copysira" GitHub App. For this you need the GitHub App private key (as a PEM file). Ask your teammates how to obtain it. Then use the `--ghapp_pk_pem` option of the `publish.py` script to use it.
- Have write access to the repository (using an SSH key linked to your GitHub account, itself linked to the organization & repository). In which case you have nothing more to do. The commits will be marked as committed by you (author is unchanged in iterative mode).

Once you have write access, to publish the branch:

- The three elements you need are:
    - The name of the branch you want to publish (we'll call it `my_branch`)
    - The name of the branch from which `my_branch` diverged (we'll call it `my_base`). This branch MUST also be present on the public repository.
    - The commit where those branches diverged. This can be obtained with `git merge-base my_branch my_base`. (We'll call it `the_commit`.)
- In the public repository, try looking for a commit whose message contains `GitOrigin-RevId: the_commit`.
    - If this commit is on the branch `my_base` (this can be checked with `git branch -a --contains public_commit`, `my_base` should be in the list), you're done, just create `my_branch` here using the normal Git workflow (GitHub does not provide a way to create a branch at a specific commit in their web UI).
- Otherwise, try synchronizing `my_base` with the `ci/oss_publish/publish.py` script, then retry the step above.
- Otherwise, try manually looking for the closest commit corresponding to `the_commit` on the public version of `my_base` and if found, create `my_branch` there. This is because some commits might not be mirrored to the public repository, for instance when they only contain modifications to files that are not public.
- If all this fails, you are on your own.

### Troubleshooting

We have sometimes noticed issues when pushing changes from Copybara to the public repository in the form of errors 55 in Curl. This seems to be related to the size of commits to push. Manually executing the `publish.py` script from another machine has fixed the issue, but there seem to exist other (untested) options, such as adjusting the `http.postBuffer` git option.

## Selecting files to publish

In the `ci/oss_publish/files.json` file, the `include` and `exclude` arrays contain the patterns for publication. We begin with all the files
described by `include`, and then exclude those matching an entry in `exclude`.

## Selecting files to license

For licensing, we begin with the set of files to publish declared in `ci/oss_publish/files.json`, as explained [above](#selecting-files-to-publish). Then we exclude all files matching
any entry of `license_exclude`. For example images or documentation files. Those patterns must be simple matches,
no recursive globing, unless it is prefixed by `glob:`.

- `*.txt` matches all txt files.
- `hrz/BUILD` matches exactly the `hrz/BUILD` file.
- `glob:hrz/doc/**` matches all files in `hrz/doc/`.
- It is highly discouraged to do globing on the root, such as `**/*.txt`, for performance reasons. Simply write `*.txt` in this case.

Finally the `license_rules` map gives for each file pattern (no globbing) the type of comment that will be used to
include the license header. For example, the `c` type uses comments `//` and is usable in all languages that use
this syntax for single-line comments. The types of comments are defined in the `license.py` file, and can be
extended at will.

## Applying the license

The license must be applied to all source files by the developer (it is checked during CI). This is done by running the `ci/oss_publish/license.py` script. Use the `--check` flag for a dry run.

## Configuring the open-source build

The `--//:opensource={True|False}` flag can be used to build the user and developer documentations in "open-source" mode, which will exclude private content. The corresponding `config_setting` is `//:is_opensource`.

!!! warning ""
    Only the documentations can depend on this flag. No other part of the engine is allowed to be different in open-source and private mode.

## Redacting parts of the code

By wrapping parts of the code between `BEGIN(dash)INTERNAL` and `END(dash)INTERNAL` comments, some code can be redacted when publishing files to the open-source repository. The `//tools:redact` script can be used to do the same thing at build time, which can be useful with the [`//:opensource`](#configuring-the-open-source-build) flag.

!!! note ""
    Replace `(dash)` with the `-` character when inserting these tokens. They cannot be written here or the paragraph above itself would be partially redacted!

!!! warning ""
    As explained above, only the documentations can include such excluded parts, and no other part of the engine.

## Publishing a release to the open-source repository

**After all artifacts have been published to the private repository, and the version tag has been created on the public repository**, it is possible to publish the artifacts as a GitHub release on the public repository. To do so, execute the `ci/oss_publish/create_release.py` script with the version name and and GitHub App private key.

For example to publish version `a.b.c`:

- `python3 ci/oss_publish/create_release.py --ghapp_pk_pem path/to/key.pem a.b.c`

The artifacts to publish are listed in the `ci/oss_publish/artifacts.json` file.
