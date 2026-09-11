+++
title = "Visual tests"
+++

# Visual tests

This document explains how visual tests are managed in Horizon.

> [!note] Open-source test samples
> Although we don't publish our internal visual tests suite, a sample is given in `tests/visual_tests_sample/` in case anyone wants to learn how to use the visual testing tools.

## General

Horizon has automatic integration tests that use image comparison. They are run as part of the GitLab continuous integration process for every merge request as well as the main branches. They are a way to limit the introduction of regressions in the code base.

The file `<test_folder>/manifest.json` is called the _manifest_ and gives a description of every test that needs to be performed.

A visual test needs three pieces of information to run properly:

- A scene file, which can be either a [Horizon scene dump](scene_model_versioning.md) in Protobuf format, or a [Mapbox style](https://docs.mapbox.com/mapbox-gl-js/style-spec/) in JSON format,
- A reference image that serves as the ground truth for the image comparison,
- All the data required to load the scene.

Scenes and reference images are stored in `<test_folder>/hrz_scenes` or `<test_folder>/mapbox_styles`, depending on the scene format. Both files of a test **MUST** be named after the unique test name set in the manifest with the following scheme: `<test_name>.hrz_scene.pbf` or `<test_name>.json` for the scene file, depending on the format, and `<test_name>.hrz_ref.png` for the reference image.

> [!important] Internal tests data
> For internal tests, all data required to load a scene must be stored on Horizon’s asset server, and can be accessed at `https://redacted.localhost/assets/`.
>
> Files on this server **MUST NOT** be deleted, updated or tampered with in any way as tests from other branches (`main`, maintenance branches, other merge requests) may still need them. Therefore any modification to a dataset needs a new upload leaving the older version unchanged.

## Testing tool

Tests are managed and run through a tool which can be launched by running `python tools/visual_testing/visual_tests.py [-m <manifest_file>]`. It runs a CLI by default but a GUI is available by passing the `--gui` argument.

Here are some command examples:

- `python tools/visual_testing/visual_tests.py` runs all the tests and writes results in a sub-directory of the working directory.
- `python tools/visual_testing/visual_tests.py subset test0 test1` runs a subset of tests and writes results in a sub-directory of the working directory.
- `python tools/visual_testing/visual_tests.py exclude test0 test1` runs all tests except those specified and writes results in a sub-directory of the working directory.
- `python tools/visual_testing/visual_tests.py --gui` runs the tool in GUI mode.

More flags can be passed, for example `-c opt` to use the optimized version of Horizon. See the script itself for all the flags and options.

Running the tests writes image captures as well as a JSON report that can be visualised in the tool when used in GUI mode.

