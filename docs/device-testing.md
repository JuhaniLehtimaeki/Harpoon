# Testing Harpoon on a SailfishOS device

Phase 2 can't be fully checked off a phone. The desktop tests cover the logic with real
RPM files, a local HTTP server and a mock PackageKit on a private D-Bus. Only a device can
answer how SailfishOS itself behaves. This page lists what to run and what to report back.

## 1. Build

Use the Sailfish SDK (see the `sailfish-app-development` skill for `sfdk` details):

```sh
cd Harpoon
sfdk config --global --push target SailfishOS-<version>-aarch64   # pick a target from: sfdk tools list
sfdk build
```

**Report:** whether the build succeeds. The core is C++17 (`std::optional`, inline
variables). If the target compiler refuses it, paste the first error.

Copy the RPM from `RPMS/` to the phone and install it there:

```sh
devel-su pkcon install-local harpoon-0.1.0-1.aarch64.rpm
```

## 2. Track and check (runs as the normal user)

```sh
harpoon-cli add https://github.com/sailfishos-chum/sailfishos-chum-gui
harpoon-cli list
harpoon-cli show sailfishos-chum-gui
harpoon-cli check
```

Good test candidates are projects whose GitHub releases contain SailfishOS RPMs, such as
sailfishos-chum-gui or harbour-storeman. Optional: `export HARPOON_TOKEN_GITHUB=<token>`
avoids GitHub's anonymous rate limit.

**Report:**
- Does `add` pick the right package for the phone (aarch64 vs armv7hl)?
- Does `list` show the installed version correctly for an app that is already installed?
- Any network or TLS errors.

## 3. Install through PackageKit

PackageKit admits callers whose effective group is `privileged`. From a terminal:

```sh
devel-su -p harpoon-cli install sailfishos-chum-gui --reinstall
```

Also try it without `devel-su -p`. That should fail with a clear "not authorized" error.

**Report:**
- Whether the privileged install succeeds, and the full output.
- Whether the result depends on Settings → System → Untrusted software being on or off.
- Any vendor warning, and whether zypp then refused the update.
- The unprivileged error message.

## 4. Install through the system installation handler

```sh
harpoon-cli install sailfishos-chum-gui --reinstall --backend handler
```

**Report:** whether a system confirmation dialog appears, and what happens when you accept or
decline it.

## 5. Uninstall

```sh
devel-su -p harpoon-cli remove <some-test-app> --uninstall
```

Use an app you don't need. This removes the package.

## Where things live

| What | Path |
|---|---|
| App records | `~/.local/share/harpoon/harpoon/apps/*.json` |
| Downloads (deleted after install) | `~/.cache/harpoon/harpoon/downloads/` |

`HARPOON_DATA_DIR` and `HARPOON_CACHE_DIR` override these locations.
