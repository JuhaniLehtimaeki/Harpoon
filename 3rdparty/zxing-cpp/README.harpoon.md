# zxing-cpp (vendored)

- Upstream: https://github.com/zxing-cpp/zxing-cpp
- Version: v2.2.1 (commit 99a83b3a6ac514d7f850dda7fa24cddb5120c7e2)
- Licence: Apache-2.0 (`LICENSE`); compatible with Harpoon's GPL-3.0-or-later.

Only `core/` and `zxing.cmake` are included. Harpoon's change: the install and packaging
section at the end of `core/CMakeLists.txt` is removed, because ZXing is linked statically
into the app and must not install headers or libraries.

Vendored because the SailfishOS build targets and Chum's OBS have no network access during
builds, and zxing-cpp is not packaged for SailfishOS.
