# Building and publishing Harpoon

## Local build with the Sailfish SDK

```sh
sfdk config --global --push target SailfishOS-<version>-aarch64
sfdk build                      # RPMS/harpoon-<version>-1.aarch64.rpm
sfdk check                      # rpmlint and validators (Harbour rules do not apply; see below)
sfdk deploy --sdk               # or copy the RPM and: devel-su pkcon install-local harpoon-*.rpm
```

`rpm/harpoon.spec` works in both build modes:
- With `sfdk`, the build runs in place in the source tree.
- On OBS, `%prep` unpacks the tarball made by `tar_git`. It does this only when that tarball
  exists.

## Releasing

1. Update `Version:` in `rpm/harpoon.spec`, `project(... VERSION ...)` in `CMakeLists.txt`,
   and add an entry to `rpm/harpoon.changes`.
2. Tag the commit with the bare version, for example `0.1.0`, and push the tag.

## Publishing on SailfishOS:Chum

Chum builds packages on the SailfishOS OBS (build.sailfishos.org) from a git tag.

1. On build.sailfishos.org, create a package `harpoon` in your home project.
2. Copy `rpm/_service.example` to that package as `_service`, and set `revision` to the
   release tag.
3. Add the SailfishOS repositories you target (5.x for aarch64, armv7hl and i486) and check
   that the package builds.
4. Submit the package to `sailfishos:chum:testing` with a submit request, then ask for it to be
   promoted to `sailfishos:chum` once it has been tested. The current procedure is described
   at https://github.com/sailfishos-chum/main.

The `%if 0%{?_chum}` block in `%description` holds the Chum metadata: title, categories, icon
and links. The Chum GUI displays it.

## Why not Harbour (the Jolla Store)

Silent installs need PackageKit, which needs the `privileged` group and an unsandboxed app
(`Sandboxing=Disabled` plus `privileges.d`). Harbour rejects both. The installation-handler
backend would fit Harbour's model, but `ApplicationInstallation` is not on Harbour's allowed
list either (see `docs/research/03-sailfishos-installation.md`).

## What the package installs

| File | Purpose |
|---|---|
| `/usr/bin/harpoon` | the app |
| `/usr/bin/harpoon-cli` | command line tool |
| `/usr/bin/harpoon-autoupdate` | the background job: checks, notifies, and installs updates when enabled |
| `/usr/share/harpoon/{qml,translations}` | UI |
| `/usr/share/applications/harpoon.desktop` | launcher; `[X-Sailjail] Sandboxing=Disabled` |
| `/usr/share/mapplauncherd/privileges.d/harpoon` | starts the app and `harpoon-autoupdate` in the `privileged` group |
| `/usr/share/dbus-1/services/io.github.juhanilehtimaeki.harpoon.service` | opens the app from a notification |
| `/usr/lib/systemd/user/harpoon-check.{service,timer}` | background checks; the app enables or disables the timer |
| `/usr/share/licenses/harpoon-<version>/` | GPL-3.0 and the zxing-cpp Apache-2.0 licence |
