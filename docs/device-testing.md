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
variables), and so is the vendored zxing-cpp QR decoder. If the target compiler refuses
either, paste the first error.

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

## 6. The app

Launch Harpoon from the app grid. On SailfishOS 5.x the system should first warn once that the
app runs without a sandbox.

1. Pull down → **Add app** and paste a GitHub URL. The line under the field should name the
   source, and the dialog should only accept once the URL is valid.
2. In the list, long-press an app → **Install** / **Update**. Watch the progress line.
3. Open an app, then check the release notes, the **App settings** page and **Uninstall**.
4. Minimise the app. The cover should show the number of updates; the refresh action runs a
   check.
5. Switch between a light and a dark ambience and check that all text stays readable.
6. **QR codes.** On a computer, make a code with
   `qrencode -t ANSIUTF8 "https://github.com/sailfishos-chum/sailfishos-chum-gui"`. Then in
   Harpoon, pull down → **Add app** → **Scan QR code** and point the camera at it. The
   dialog should fill in the URL within about a second. Check that:
   - the viewfinder shows a live, correctly rotated picture. The scanner grabs frames from
     `VideoOutput`; if they come out black on the device, nothing will ever be found, so
     report it;
   - the camera turns off when you leave the page or minimise the app;
   - **Read from image** works with a screenshot of a code.
7. **Links.** Open `harpoon://add?url=https%3A%2F%2Fcodeberg.org%2Fsomeone%2Fthing`, for example
   by tapping it in a note or on a web page, or with `xdg-open` if it is installed. Harpoon
   should open with the add dialog filled in. Also try scanning that link with the system
   camera app's code reader.

8. **Sharing.** Open an app → pull down → **Share as QR code**, and scan the code with
   another phone running Harpoon.
9. **Automatic updates.** In Settings, turn on **Install updates automatically**. Install an
   older release of an app through Harpoon (pick one with an update), then run the background
   job by hand:

   ```sh
   systemctl --user start harpoon-check.service
   journalctl --user -u harpoon-check.service
   ```

   The app should be updated without a prompt, with a "… was updated" notification. If the
   log shows `invoker` failing or PackageKit refusing ("not authorized"), report it: then
   `invoker` from a systemd user service does not grant the privileged group.
10. **Build provenance.** For an app on GitHub that publishes attestations (for example one
    built with `actions/attest-build-provenance`), set **Check build provenance** to **Warn if
    missing** and install. The app page should show **Signature verified**.

**Report:** anything that looks wrong or un-Sailfish-like, any QML errors from
`devel-su journalctl -fa | grep -i harpoon`, and whether installing from the app works without
`devel-su -p`. That last one tests the privileges.d entry. For QR codes, report whether the
scanner found codes and whether `harpoon:` links open Harpoon.

## Where things live

| What | Path |
|---|---|
| App records | `~/.local/share/io.github.juhanilehtimaeki/harpoon/apps/*.json` |
| Downloads (deleted after install) | `~/.cache/io.github.juhanilehtimaeki/harpoon/downloads/` |

`HARPOON_DATA_DIR` and `HARPOON_CACHE_DIR` override these locations.
