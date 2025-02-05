# Continuous integration

Horizon is automatically built and tested each time a merge request is created, updated, or merged.

## Jobs’ configuration

All the CI jobs are described in the `.gitlab-ci.yml` file at the root of the project repository. Multiple jobs call Python scripts (in the `scripts/ci` directory), because Bash is terrible, and even more so when embedded in a YAML file.

### Docker images

The majority of the jobs run inside a Docker container. They allow setting up the environment precisely in advance, and properly isolating each build by starting from a clean state each time.

`Dockerfile`s are in the `docker` directory of this repository. They can be compiled to Docker images and uploaded to [the Nexus repository](http://nexus-int.siradel.com:8081/) when they are updated. This is done manually.

To build an image, and then upload it, go to its directory and type:

```shell
sudo docker build -t nexus-int.siradel.com:8093/hrz/$IMAGE_NAME:$IMAGE_VERSION .
sudo docker push nexus-int.siradel.com:8093/hrz/$IMAGE_NAME:$IMAGE_VERSION
```

Where `$IMAGE_NAME` and `$IMAGE_VERSION` are the name and the version of the image, respectively. You have to upgrade the version number when uploading a new version, and not replace the existing image (by reusing an existing version number). This is so that jobs on other branches can keep on using the image they are meant to use.

The image is then available with the name `nexus-int.siradel.com:8092/hrz/$IMAGE_NAME:$IMAGE_VERSION`. (Note that the port number is different. This is because Docker includes the repository URL in the image name, combined with the use of a façade repository grouping multiple repositories on the Nexus server.)

You have to log into the Nexus Docker repository to be able to push the image. If you don’t have the necessary credentials, ask the IT support, or someone in the team for the shared account credentials.

### Environment variables

The build scripts use a number of environment variables to get data such as paths or connection tokens. Some are set automatically by GitLab: the [predefined variables](https://docs.gitlab.com/ee/ci/variables/predefined_variables.html). Others are set in the CI/CD page of the projet’s configuration on GitLab.

### Publication

Build artefacts are published to [the Nexus instance](http://nexus-int.siradel.com:8081/). Commands are executed in publication jobs, running Bazel targets to push the artefacts. (One command per artefact.)

Release builds are automatically published for tagged releases, as well as snapshot builds (compiled in release mode) everytime the `master` branch is updated.

Additional CI jobs can be executed manually to publish snapshot builds from merge requests, built in either release or debug mode.

### Deployment

There are jobs for deploying the web test client, for both merge requests and `master`. This is done by creating a sub-directory for the branch (if it doesn’t already exist) and then copying files into that sub-directory. The sub-directory is created in a dedicated root directory for web files.

When a branch is merged, its sub-directory is deleted by a dedicated job. This job is called automatically by GitLab, as it is configured as `on_stop` for the corresponding deployment job.

## Infrastructure

A few virtual machines and a physical machine run the CI jobs and host deployments, as well as a number of support services. Ask someone for the credentials. Linux machines are accessed with SSH, Windows machines with RDP.

### Machines and services

#### lfrn1mmp01

* Full name: `lfrn1mmp01.siradel.local`
* Type: VM
* Architecture: x86_64
* OS: Ubuntu 16.04
* Duties:
    * Linux and web build GitLab runner.

The runner is configured in `/etc/gitlab-runner/config.toml`.

The GitLab runner service starts automatically when the machine is booted up. This is handled by a Systemd job, which is itself configured automatically when the runner package is updated.

#### lfrn1mmp02

* Full name: `lfrn1mmp02.siradel.local`
* Type: VM
* Architecture: x86_64
* OS: Windows 10
* Duties:
    * Windows build GitLab runner.

The runner is configured in `C:\gitlab-runner\config.toml`.

The GitLab runner service starts automatically when the machine is booted up.

#### lfrn1mmp03

* Full name: `lfrn1mmp03.siradel.local`
* Type: VM
* Architecture: x86_64
* OS: Ubuntu 16.04
* Duties:
    * Reverse proxy,
    * Linux and web build GitLab runner,
    * Linux and web deployment runner,
    * Web server for deployments,
    * Scene data files hosting,
    * Scene definition file repository,
    * OpenStreetMap proxy,
    * Debug raster tile server,
    * PMTiles server,
    * Bazel cache.

##### Reverse proxy

A reverse proxy redirects requests on `https://hrz.siradel.com:443/` to `http://lfrn1mmp03.siradel.local:80/`. (This redirection is managed by the IT team.) Then lfrn1mmp03’s nginx instance acts as another reverse proxy. It redirects the requests again if necessary, based on the URL, to the many services that run on this machine.

##### GitLab runners

The GitLab runners are configured in `/etc/gitlab-runner/config.toml`. The GitLab runner service starts automatically when the machine is booted up. This is handled by a Systemd job, which is itself configured automatically when the runner package is updated.

##### HTTP file server

Files are served (in HTTP) by an Nginx instance. The sites are configured in `/etc/nginx/sites-enabled/`. They comprise:

* `index`: Serves the home page at [`https://hrz.siradel.com/`](https://hrz.siradel.com/). This page is generated by a PHP script at `/var/www/index/index.php`. Part of the links on the page are generated by listing the sub-directories in the client deployment directory `/home/gl/www-data/`. The PHP script file is versioned in this repository as `ci/www/index.php`.
* `pages`: Serves the web test client deployments. Their base directory is `/home/gl/www-data/`. Inside this directory are sub-directories for different types of deployments (client and documentation for `master`,  merge requests, and releases, as well as demos). They are created and deleted automatically by the CI scripts, apart from demos, which are handled manually.
* `assets`: Serves the scene assets at [`https://hrz.siradel.com/assets/`](https://hrz.siradel.com/assets/). They are stored in the `/home/gl/hrz_assets/` directory. New files can be copied into this directory when needed.

Nginx starts automatically.

##### Scene repository

The scene definition file repository is available at [`https://hrz.siradel.com/scenes/`](https://hrz.siradel.com/scenes/). It is managed by a Systemd service (`/etc/systemd/system/scenes-repo.service`) and starts automatically. It is a native executable that has been build and copied to `/home/gl/scenes_repo_server/` manually. The source code is in the `tools/scene_repo_server` directory of this repository.

##### OSM proxy

The OpenStreetMap proxy is available at [`https://hrz.siradel.com/osm/`](https://hrz.siradel.com/osm/). It is managed by a Systemd service and starts automatically. It is a native executable that has been built and copied to `/home/gl/osm_proxy/` manually. The source code is in the `tools/osm_proxy` directory of this repository.

##### Debug raster tile server

The debug raster tile service is available at [`https://hrz.siradel.com/debug-raster-tiles/`](https://hrz.siradel.com/debug-raster-tiles/). It is managed by a Systemd service (`/etc/systemd/system/debug-tile-server.service`) and starts automatically. The server is a Python script, running inside a Docker container. The source code for the script and the Docker image is in the `tools/debug_tile_server` directory of this repository.

##### PMTiles server

Tiles extracted from [PMTiles](https://docs.protomaps.com/pmtiles/) archives are available at [`https://hrz.siradel.com/pmtiles/`](https://hrz.siradel.com/pmtiles/). The server is managed by a Systemd service (`/etc/systemd/system/pmtiles-server.service`) and starts automatically. The server is [go-pmtiles](https://github.com/protomaps/go-pmtiles), running inside a Docker container. The source code for the Docker image is in the `tools/pmtiles_server` directory of this repository.

The PMTiles archives located in `~/hrz_assets/pmtiles` on `lfrn1mmp03` are served. If an archive is named `osm`, its tiles are available at `https://hrz.siradel.com/pmtiles/osm/{z}/{x}/{y}.mvt` and its metadata JSON file at `https://hrz.siradel.com/pmtiles/osm/metadata`.

##### Bazel cache

The Bazel cache is available at [`http://lfrn1mmp03.siradel.local:8090/`](http://lfrn1mmp03.siradel.local:8090/). It is managed by a Systemd service (`/etc/systemd/system/bazel-remote-cache.service`) and starts automatically. It runs the `buchgr/bazel-remote-cache` Docker image, stores the artefacts in `/home/gl/bazel-remote-cache/`, and limits its disk-space consumption to 10 GB.

#### cus-wks-008

* Full name: `cus-wks-008.siradel.local`
* Type: Physical machine, with a GPU
* Architecture: x86_64
* GPU: Nvidia Quadro 2000
* OS: Windows 10
* Duties:
    * Windows test GitLab runner,
    * Windows integration GitLab runner.

This machine hosts two instances of the GitLab runner executable. One is for unit tests, its configuration file is `C:\gitlab-runner\config.toml`. It is configured as a service and starts automatically.

Another instance, for integration tests, is configured in `C:\gitlab-runner-integration\config.toml`. This one is not a service. This is because the jobs it runs use the GPU for material acceleration. This isn’t allowed for services, so it has to run as a regular executable. This instance has to be started manually when the machine is rebooted. See the explanations in `C:\gitlab-runner-integration\README.txt`. (A scheduled task has been created in order to start the instance automatically, but it fails because of [this bug](https://gitlab.com/gitlab-org/gitlab-runner/-/issues/27986).)

This machine is a physical machine, sitting somewhere in the company’s office in Saint-Grégoire. It can sometimes fail to restart properly. In that case, open a new ticket on the [IT HelpDesk](http://helpit.siradel.local/) and ask for someone to go and manually reboot it.

### Updates

#### System updates

All machines should have system updates applied regularly. To update a machine, on Linux call `sudo apt update && sudo apt upgrade`; on Windows, use the update panel. Reboot the machine once the updates have been applied.

System settings are often reset when installing updates on Windows, so it’s best to check if everything is still okay. In particular, ensure that the machines are not configured to sleep or hibernate.

#### GitLab runner upgrade

The GitLab runner executables should be upgraded when the GitLab server instance itself is upgraded (preferrably before that). If the versions are not too far apart, the runner and the server are usually compatible.

See https://docs.gitlab.com/runner/install/linux-manually.html and https://docs.gitlab.com/runner/install/windows.html for information on how to upgrade the runner executable to a new version. On Linux, follow the deb package procedure. On Windows, download and replace the executable itself directly.

Configuration files stay the same.
