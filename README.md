# Harpoon

An Obtainium-style updater for SailfishOS. Harpoon tracks apps that publish RPM release
assets on GitHub, Codeberg/Forgejo/Gitea and other forges, and installs updates straight
from the source.

- Design: [docs/architecture.md](docs/architecture.md)
- Research: [docs/research](docs/research)

## Status
The core library and the `harpoon-cli` command-line tool are implemented and tested on
desktop Linux. They cover:
- forge sources;
- release and asset selection;
- downloading;
- RPM inspection;
- installing through PackageKit or the system installation handler.

A Silica UI (`gui/`) sits on top of it. Nothing has been tested on a device yet; see
[docs/device-testing.md](docs/device-testing.md).

## Using harpoon-cli (on the phone)
```sh
harpoon-cli add https://github.com/owner/repo      # track an app
harpoon-cli list                                    # installed vs latest
harpoon-cli check                                   # check all apps for updates
devel-su -p harpoon-cli install <app>               # install; needs the privileged group
devel-su -p harpoon-cli upgrade                     # install every available update
```
Run `harpoon-cli --help` for all commands.

## Building the core on desktop Linux
The core needs Qt 5 (Core, Network, DBus, Test) and CMake. With Qt Quick installed
(`qtdeclarative5-dev`), the QML smoke test also runs. The tests also use `rpm`,
`rpmbuild` and `dbus-daemon` when they are available. The phone ships Qt 5.6, so the core
code must use only Qt 5.6 APIs, even though desktop builds use newer Qt.

```sh
sudo apt install qtbase5-dev qtdeclarative5-dev qml-module-qtquick2 cmake g++ rpm dbus   # Debian/Ubuntu
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Building for SailfishOS
```sh
sfdk build      # uses rpm/harpoon.spec; builds the app (harpoon) and harpoon-cli
```

## Licence
GPL-3.0-or-later, see [LICENSE](LICENSE). Parts of the core are ported from
[ObtainX](https://github.com/bikram-agarwal/ObtainX) (GPL-3.0).

## Trying a real repository
```sh
./build/tools/harpoon-probe/harpoon-probe https://github.com/sailfishos-chum/sailfishos-chum-gui --arch aarch64 --sfos 5.0.0.62
./build/tools/harpoon-probe/harpoon-probe https://git.example.net/me/app --source Forgejo
```
Set `HARPOON_TOKEN` to pass an API token for the matched forge. Options:
- `--prereleases` includes prereleases.
- `--track-only` drops the need for an installable asset.
- `--set key=value` sets any per-app setting (see `core/src/model/appsettings.h`).
