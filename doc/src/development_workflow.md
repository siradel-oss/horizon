# Development workflow

## Issue lifetime

- Once an issue is well defined, it is switch to the "To Do" state.
- Someone choses an issue in the "To Do" state, switches it to "In progress" and assigns themselves to it.
- All subsequent developments must be done on branch named `usXXXX_short_name` for user stories, or `bfXXXX_short_name` for bugs, where XXXX is the JIRA numerical ID of the issue, and `short_name` is a short descriptive name, written in snake case.
- Create a partial changelog file in `changelogs/unreleased` with a unique name (for instance your branch name).
- Update your changelog file as you go to log all important changes, especially deprecated and removed features, upgrade notes, and integration notes.
    - Each section must start with a line like `# Section name`. The section names are given in the `changelogs/unreleased/CHANGELOG_TEMPLATE.md.tpl` file.
    - Integration notes should only be a way to highlight something important that is already written somewhere else in the documentation.
    - Inside each section, write the changelog as a list with the `*` character followed by a whitespace as list marker.
    - The list can contain multiple levels, which must be indented correctly.
    - See the [changelog guidelines](guidelines/changelog.md).
- After development is finished and CI is OK, the issue is switched to the "To review" state. A merge request is created on Gitlab.
- Someone other that the person who was assigned to the issue assigns themselves as a reviewer in the JIRA issue and starts reviewing the merge request.
- Once the reviewer has finished reviewing the merge request, they approve the issue in GitLab.
- See the merge request process below.
- If the changes induced by the merge request are too important, the issue can be switched back to "In progress" and the cycle starts again.
- Once the merge request in accepted, it must be merged into `master` and the issue branch deleted. The issue can then be switched to "Done".

## Merge request process

- The merge request title must start with the full JIRA ID of the issue in brackets: `[HRZ-XXXX]`.
- For a merge request to be accepted, all comments must be closed and one or two reviewers must approve it (depending on current staffing).
- Each comment has to be closed *by the person who opened it* to validate the changes that were made.
    - Trivial changes like typos can be closed by the submitter instead.

## Version numbers

Version numbers are based on the current year: YY.M.m.

- YY: two-digit year (23 for 2023, we'll need to revisit this in 2100)
- M: major version (0-based)
- m: patch number (0-based)

There are no rule for what should increment the major or patch number, but generally, larger releases with new features should be major versions, and smaller releases with mostly fixes should be patches. The patch number is reset to 0 upon releasing a major version.

## Release process

- In this example the current version is `a.b.c` and the next version is `d.e.f`.
- Start from `master` or a maintenance branch, with a clean local clone.
    - Of course, the branch should be in a releasable state.
- Check the current version: `cat version.bzl`. It should contain:
    ```
    HRZ_VERSION = "a.b.c-SNAPSHOT"
    ```
- Create a release branch:
    - `git checkout -b release_a.b.c`
- Change the version number from snapshot to release:
    - `python3 tools/build_info/set_version.py a.b.c`
- Update the changelog:
    - Merge all unreleased changelog fragments:
        - `python changelogs/changelog.py merge_into changelogs/a.b.c.md a.b.c changelogs/unreleased/*.md`
    - Reorder the changelog entries so that their order makes sense. More important changes should be higher in each list.
    - Changelog entries that refer to the same systems should be grouped together. See the [changelog guidelines](guidelines/changelog.md).
    - Check that upgrade notes have been filled in the merged file.
    - Delete all unreleased changelog fragments from `changelogs/unreleased`:
        - `git rm changelogs/unreleased/*.md`
- Format everything:
    - `python tools/format.py`
- Check the diff:
    - `git diff`
    - The only changes should be the version numbers in `version.bzl` and `.gitlab-ci.yml`, as well as the changelogs.
- Commit:
    - `git commit -am "Release a.b.c"`
- Push:
    - `git push origin release_a.b.c`
- Open a new merge request on GitLab, name it “Release a.b.c”. The target branch is either `master`, or for maintenance releases, the corresponding maintenance branch.
- A new CI pipeline should be created.
    - Wait until it succeeds.
- Tag the release in the GitLab interface.
    - Go to “Code” > “Tags” then click on the “New tag” button.
    - Set the tag name to `va.b.c`.
    - Set the tag message to “Release a.b.c”.
- A new CI pipeline should be created.
    - This pipeline performs the actual release, by compiling and then uploading the release artifacts to the release binary repositories on the Nexus instance.
        - Artifacts on these repositories cannot be replaced, making each release actually unique.
    - Ensure everything went well.
- Create the release in the GitLab interface.
    - Go to “Deploy” > “Releases” then click on the “New release” button.
    - Select the tag `va.b.c`.
    - Set the release title to “Release a.b.c”.
    - Set the date to the present day.
    - In the release notes field, put the relevant section of the `CHANGELOG.md` file, as well as anything you deem useful. (Don’t include the line with the version number and the date.)
- Publish the version to the open-source repository.
    - First publish the `release_a.b.c` branch at the point where it diverged from `master` or `maintenance_a.b`. (See the "Publishing a branch to the open-source repository" section below.)
    - Then execute the `ci/oss_publish/publish.py` script:
        - `python ci/oss_publish/publish.py release_a.b.c --tag va.b.c`
        - See the [Publishing a branch to the open-source repository](#publishing-a-branch-to-the-open-source-repository) section for a guide on how to authenticate.
    - If somehow this fails (for example if the release branch was already published), you can create the tag manually.
    - The tag should point to the public version of the release commit.
    - Once the tag has been published, the public `release_a.b.c` can be deleted, but you can also keep it if you want.
- Back on your local clone, set the version number to the next snapshot:
    - `python3 tools/build_info/set_version.py d.e.f-SNAPSHOT`
- Commit:
    - `git commit -am "Set version to d.e.f-SNAPSHOT"`
- Push.
- Merge the release branch into its target branch.
- Mark the version as released on Jira.
- Send an email announcing the release to [`redacted@siradel.com`](mailto:redacted@siradel.com) & ['redacted@siradel.com'](mailto:redacted@siradel.com).
- Pat your colleagues and yourself on the back.

## Maintenance process

If a previously released version needs to be patched, and eventually have patch releases, follow this process:

- Create a branch starting at the revision of the release, which is identified by a tag of the form `va.b.c`. The maintenance branch’s name derives from the version number: it starts with `maintenance_` followed by the original version number, minus the components that will change during the life of the maintenance branch.
    - For example, if the previously released version is `0.7.0`, and you want to create a branch from which `0.7.1`, `0.7.2`, and so on, will be released, name the branch `maintenance_0.7`.
    - `git branch maintenance_a.b va.b.c`
- Update the version number to the new snapshot.
    - For example, if the previously released version is `0.7.0` and the next one will be `0.7.1`, set the version to `0.7.1-SNAPSHOT`.
    - `python3 tools/build_info/set_version.py a.b.d-SNAPSHOT`
- Commit:
    - `git commit -am "Set version to a.b.c-SNAPSHOT"`
- Push the branch (you might need to temporarily remove branch protection):
    - `git push origin maintenance_a.b`
- Publish the maintenance branch to the open-source repository on the release tag (see below, but doing this manually is probably simpler).
    - Because a pipeline might have been triggered when you pushed the branch to the private repo, and before you could publish the branch, this first pipeline might fail at the open-source publish step. If so, that's OK, you can just re-run it later.
- The new maintenance branch is automatically protected against unreviewed changes in GitLab.
- From then on, create merge requests and make releases in the same fashion as what is done on the `master` branch.
    - Do not forget to start from the maintenance branch before committing, and target this branch when opening merge requests.
- If the changes you want to include in the maintenance branch are also relevant for `master`, do not forget to add them there as well.
    - It may be convenient to make a bug fix on `master`, and then an equivalent merge request on the maintenance branch. `git cherry-pick` can be useful here, but be careful and do not take too many changes in.
- Make releases using the process described above. Make sure to use version numbers that are suitable for the maintenance branch.
- Update the changelogs on master to reflect the release of the maintenance version.
    - This should include deleting potentially duplicated changelog files from `changelogs/unreleased`.
    - Cherry-picking the release commit from the maintenance branch should delete the unreleased files and create the maintenance release changelog. Use `--no-commit` to be safe.
- Never ever merge the `master` branch in the maintenance branch, or vice-versa! Their commit subtrees should eternally remain separate.

## Publishing a branch to the open-source repository

To publish code on the open-source repository using the `ci/oss_publish/publish.py` script, you must either:

- Have write access to the repository (using an SSH key linked to your GitHub account, itself linked to the organization & repository). In which case you have nothing more to do. The commits will be marked as committed by you (author is unchanged in iterative mode).
- Or authenticate as the "Copysira" GitHub App. For this you need the GitHub App ID & the private key (as a PEM file). Ask your teammates how to obtain those. Then use the `--ghapp_id` and `--ghapp_pk_pem` options of the `publish.py` script to use them.

Once you have write access, to publish the branch:

- The three elements you need are:
    - The name of the branch you want to publish (we'll call it `my_branch`)
    - The name of the branch from which `my_branch` diverged (we'll call it `my_base`). This branch MUST also be present on the public repository.
    - The commit where those branches diverged. This can be obtained with `git merge-base my_branch my_base`. (We'll call it `the_commit`.)
- In the public repository, try looking for a commit whose message contains `GitOrigin-RevId: the_commit`.
    - If this commit is on the branch `my_base` (this can be checked with `git branch -a --contains public_commit`, `my_base` should be in the list), you're done, just create `my_branch` here.
- Otherwise, try synchronizing `my_base` with the `ci/oss_publish/publish.py` script, then retry the step above.
- Otherwise, try manually looking for the closest commit corresponding to `the_commit` on the public version of `my_base` and if found, create `my_branch` there. This is because some commits might not be mirrored to the public repository, for instance when they only contain modifications to files that are not public.
- If all this fails, you are on your own.
