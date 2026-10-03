# Harpoon

An Obtainium-style updater for SailfishOS. Harpoon tracks apps that publish RPM release
assets on GitHub, Codeberg/Forgejo/Gitea, GitLab and other sources, and installs updates straight
from the source.

- Design: [docs/architecture.md](docs/architecture.md)
- App developers: [let users add your app with a QR code](docs/add-to-harpoon.md)
- Research: [docs/research](docs/research)

## Installing
Harpoon needs SailfishOS 5.0 or later. It is not in the Jolla Store, which does not allow
apps that install other apps.

1. In **Settings → Untrusted software**, allow untrusted software.
2. On the phone, open the [latest release](https://github.com/JuhaniLehtimaeki/Harpoon/releases/latest)
   and download the package for your system:

   | SailfishOS (Settings → About product) | Package |
   |---|---|
   | 5.1 or later | `harpoon-…sfos5.1.aarch64.rpm` |
   | 5.0 | `harpoon-…sfos5.0.aarch64.rpm` |

   `aarch64` fits most phones (Xperia 10 II and newer, Jolla C2). Older 32-bit phones need
   `armv7hl`, and Intel tablets and the emulator need `i486`. In a terminal, `uname -m` tells
   (`armv7l` means `armv7hl`).
3. Open the download (from the browser's downloads, or **Settings → Transfers**) and confirm
   the installation. From a terminal: `devel-su pkcon install-local harpoon-*.rpm`.
4. Open Harpoon and add `https://github.com/JuhaniLehtimaeki/Harpoon`. Harpoon recognises
   itself as installed and from then on updates itself, picking the right package for the
   phone.

For the strictest check of Harpoon's own updates, set the app's **Check build provenance**
to "Refuse unless verified", the workflow to `release.yml` and the refs to `refs/tags/.*`:
every release package is signed by this repository's release workflow.

**Get updates from one place only.** Harpoon installed this way updates itself. If you
later install it from another repository (such as SailfishOS:Chum), stop tracking it in
Harpoon, so that two sources do not replace each other's package.

## Status
The core library and the `harpoon-cli` command-line tool are implemented and tested on
desktop Linux. They cover:
- forge sources;
- release and asset selection;
- downloading;
- RPM inspection;
- installing through PackageKit or the system installation handler.

A Silica UI (`gui/`) sits on top of it. It has been tested on a phone with SailfishOS 5.1;
see [docs/device-testing.md](docs/device-testing.md).

## Using harpoon-cli (on the phone)
```sh
harpoon-cli add https://github.com/owner/repo      # track an app (or a harpoon://add link)
harpoon-cli list                                    # installed vs latest
harpoon-cli check                                   # check all apps for updates
sg privileged -c 'harpoon-cli install <app>'       # install; needs the privileged group
sg privileged -c 'harpoon-cli upgrade'             # install every available update
harpoon-cli background on --hours 6                 # periodic checks with notifications
harpoon-cli auto-update on                          # let those checks install updates too
harpoon-cli link https://git.example.org/me/app --source Forgejo   # a harpoon:// link for a QR code
harpoon-cli export ~/Documents/harpoon.json         # backup (import with: harpoon-cli import FILE)
```
Supported sources: GitHub, Codeberg/Forgejo/Gitea, GitLab, SourceHut, SourceForge, Jenkins,
plain web pages, direct `.rpm` links and rpm-md repositories. See [docs/sources.md](docs/sources.md).
Run `harpoon-cli --help` for all commands.

## Building the core on desktop Linux
The core needs Qt 5 (Core, Network, DBus, Test) and CMake. With Qt Quick installed
(`qtdeclarative5-dev`), the QML smoke test also runs. The tests also use `rpm`,
`rpmbuild` and `dbus-daemon` when they are available. The phone ships Qt 5.6, so the core
code must use only Qt 5.6 APIs, even though desktop builds use newer Qt.

```sh
sudo apt install qtbase5-dev qtdeclarative5-dev qml-module-qtquick2 cmake g++ rpm dbus zlib1g-dev libssl-dev   # Debian/Ubuntu
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Building for SailfishOS
```sh
sfdk build      # uses rpm/harpoon.spec; builds the app (harpoon), harpoon-cli and harpoon-autoupdate
```
Releases and publishing on SailfishOS:Chum: [docs/packaging.md](docs/packaging.md).

## Licence
GPL-3.0-or-later, see [LICENSE](LICENSE). Parts of the core are ported from
[ObtainX](https://github.com/bikram-agarwal/ObtainX) (GPL-3.0). QR codes use the bundled
[zxing-cpp](https://github.com/zxing-cpp/zxing-cpp) (Apache-2.0, `3rdparty/zxing-cpp`).

## Trying a real repository
```sh
./build/tools/harpoon-probe/harpoon-probe https://github.com/sailfishos-chum/sailfishos-chum-gui --arch aarch64 --sfos 5.0.0.62
./build/tools/harpoon-probe/harpoon-probe https://git.example.net/me/app --source Forgejo
```
Set `HARPOON_TOKEN` to pass an API token for the matched forge. Options:
- `--prereleases` includes prereleases.
- `--track-only` drops the need for an installable asset.
- `--set key=value` sets any per-app setting (see `core/src/model/appsettings.h`).
