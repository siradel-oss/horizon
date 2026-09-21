+++
title = "Open-source tooling"
+++

# Open-source tooling

## Publishing a branch to the open-source repository

To publish code on the open-source repository using `//infra/oss_publish:publish`, you must either:

- **(Recommended)** Authenticate as the "Copysira" GitHub App. For this you need the GitHub App private key (as a PEM file). Ask your teammates how to obtain it. Then use the `--ghapp_pk_pem` option of `//infra/oss_publish:publish` to use it.
- Have write access to the repository (using an SSH key linked to your GitHub account, itself linked to the organization & repository). In which case you have nothing more to do. The commits will be marked as committed by you (author is unchanged in iterative mode).

Before you (or the CI) publish code to a public branch, that branch must already exist on the public repository. To do so:

- Identify where the branch diverged from the base branch in the private repository.
- Find the corresponding (or closest) commit on the corresponding base branch in the public repository.
    - If the public base branch is not up-to-date enough, wait until it is (it is done as part of the CI process for the main and maintenance branches), or run the publish script to update it.
- Create the new public branch at that commit in the public repository (from the GitHub web UI or the git CLI).

### Troubleshooting

We have sometimes noticed issues when pushing changes from Copybara to the public repository in the form of errors 55 in Curl. This seems to be related to the size of commits to push. Manually running `//infra/oss_publish:publish` from another machine has fixed the issue, but there seem to exist other (untested) options, such as adjusting the `http.postBuffer` git option.

## Publishing a tag to the open-source repository

Tags must be created manually in the public repository because Copybara has too many pitfalls when working with tags.

- Publish the branch the tag is based on (for example `release_a.b.c`).
- Manually create the tag at the closest commit corresponding to the tag in the private repository (from the GitHub web UI or the git CLI).

## Selecting files to publish

In the `infra/oss_publish/files.json` file, the `include` and `exclude` arrays contain the patterns for publication. We begin with all the files
described by `include`, and then exclude those matching an entry in `exclude`.

## Selecting files to license

For licensing, we begin with the set of files to publish declared in `infra/oss_publish/files.json`, as explained [above](#selecting-files-to-publish). Then we exclude all files matching
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

The license must be applied to all source files by the developer (it is checked during CI). This is done by running the `//infra/oss_publish:license` Bazel target. Use the `--check` flag for a dry run. This also checks that all files are explicitly included in or excluded from publishing.

## Redacting parts of the code

By wrapping parts of the code between `BEGIN(dash)INTERNAL` and `END(dash)INTERNAL` comments, some code can be redacted when publishing files to the open-source repository. The `//tools:redact` script can be used to do the same thing at build time.

> [!note]
> Replace `(dash)` with the `-` character when inserting these tokens. They cannot be written here or the paragraph above itself would be partially redacted!

> [!warning]
> As explained above, redacted parts of the code must not have any effect on the published artifacts except for the documentations.

## Publishing a release to the open-source repository

**After all artifacts have been published to the private repository, and the version tag has been created on the public repository**, it is possible to publish the artifacts as a GitHub release on the public repository. To do so, execute `//infra/oss_publish:create_release` with the version name and and GitHub App private key.

For example to publish version `a.b.c`:

- `bazel run //infra/oss_publish:create_release -- --ghapp_pk_pem path/to/key.pem a.b.c`

The artifacts to publish are listed in the `infra/oss_publish/artifacts.json` file.

## Publish a release's npm packages to npmjs.org

After the release has been created and the assets uploaded to GitHub:

- Go to the GitHub repository, then "Actions".
- Select the "Publish npm packages" workflow in the sidebar.
- On top of the table of runs, click "Run workflow".
- Set the Git tag for which you wish to release the npm packages.
- Click "Run workflow", wait a little bit, the packages should be published to npmjs.org.

## Deploy the website to GitHub Pages

The website is built from the documentation and gallery assets of a release, and deployed to GitHub Pages at https://horizon.redacted.localhost. Its sources live in the [horizon-website](https://github.com/siradel-oss/horizon-website) repository, and are fetched by the workflow.

This is not triggered automatically when creating a release, because a release is not necessarily the latest one. Only deploy the website for a version more recent than the one currently deployed. For example, a patch release on an older maintenance branch must not be deployed.

- Go to the GitHub repository, then "Actions".
- Select the "Deploy website" workflow in the sidebar.
- On top of the table of runs, click "Run workflow".
- Set the Git tag of the release whose documentation and gallery should be published.
- Click "Run workflow", wait a little bit, the website should be updated.

## Using GitHub issues

Strive to answer all issues in as small a delay as possible, even if it's just to acknowledge it.

If a GitHub issue is deemed important enough to be tracked internally, create a Jira issue and associate the two. (We don't have a plugin or anything for that ATM so just add links to each-other.)

Don't forget to update the GitHub issue when the associated Jira issue changes state. This includes assigning yourself to it when you start working on it. It can also be useful to notify to we plan on working on the issue, even if work has not started yet, so that external contributors don't start working on them at the same time.

## Importing a pull request from GitHub

Since the public and private Git repositories do not share history, pull requests cannot be simply merged. They need to be imported to the private repository, and then republished. There is nothing automated here but here is a possible workflow:

1. Review the PR carefully on GitHub. When you feel that it can and should be merged, continue.
2. Import the PR code on your machine. Here is a possible way to do that.
    - Create a branch at the location the public branch diverged from its base branch.
    - Export the patch from GitHub by adding `.patch` to its URL. For example a PR at https://github.com/siradel-oss/horizon/pull/4 has its patch as https://github.com/siradel-oss/horizon/pull/4.patch.
    - Import the patch on your branch with `git am`.
3. Locally build, test, clean, check conflicts, etc. The same you would do before opening a MR for your own code. If fixes are needed, choose whether to do them yourself if they are small enough, or circle back to the PR author.
4. Open an MR on the private repository. Follow the usual MR workflow. Again, you can circle back to the PR author. In the description, include something like "GitHub: Closes #1234" to close the PR on GitHub once the commit is published.
5. Once satisfied, merge the MR. Thanks to the commit message, the PR should have been closed. Check that it is, and **don't forget to thank the PR author.**

> [!warning]
> Be very attentive to suspicious payloads when reviewing a PR.
> This can include binary files and base64 strings.
> Many attacks have been designed to infiltrate systems by, for instance, decoding a malicious payload during tests execution.
> Hence you must be extremely careful to only import and execute safe code both on your machine and during CI.

> [!note] Using Copybara
> An attempt was made to use Copybara for this task.
> However the fact that the transforms applied when publishing the code publicly are irreversible made it impossible,
> hence why we have this manual workflow.
