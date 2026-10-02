# Harpoon

An Obtainium-style updater for SailfishOS. Harpoon tracks apps that publish RPM release
assets on GitHub, Codeberg/Forgejo/Gitea and other forges, and installs updates straight
from the source.

- Design: [docs/architecture.md](docs/architecture.md)
- Research: [docs/research](docs/research)

## Status
Phase 1 is the core library: forge sources, release and asset selection, and version
comparison. There is no Sailfish UI or installer yet.

## Building the core on desktop Linux
The core needs Qt 5 (Core, Network, Test) and CMake. The phone ships Qt 5.6, so the core
code must use only Qt 5.6 APIs, even though desktop builds use newer Qt.

```sh
sudo apt install qtbase5-dev cmake g++     # Debian/Ubuntu
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Trying a real repository
```sh
./build/tools/harpoon-probe/harpoon-probe https://github.com/sailfishos-chum/sailfishos-chum-gui --arch aarch64 --sfos 5.0.0.62
./build/tools/harpoon-probe/harpoon-probe https://git.example.net/me/app --source Forgejo
```
Set `HARPOON_TOKEN` to pass an API token for the matched forge. Options:
- `--prereleases` includes prereleases.
- `--track-only` drops the need for an installable asset.
- `--set key=value` sets any per-app setting (see `core/src/model/appsettings.h`).
