---
name: sailfish-packaging
description: Use when working on how Harpoon packages itself or installs, updates, removes or inspects RPMs on SailfishOS. Covers RPM spec files, privileges.d, sailjail .desktop sections, PackageKit D-Bus calls, the installationhandler fallback, arch detection, EVR/vendor checks, systemd user timers and Nemo notifications.
---

# SailfishOS packaging and installation for Harpoon

Background research: `docs/research/03-sailfishos-installation.md`. Design: `docs/architecture.md`.
Read the relevant section before changing install or packaging code.

## Harpoon's own package
- Distributed via **Chum or OpenRepos, never Harbour**. Silent installs need privileges that
  Harbour rejects.
- The `.desktop` file must contain:
  ```ini
  [X-Sailjail]
  Sandboxing=Disabled
  ```
  Since SFOS 5.x (sailjail, Apr 2026) this shows a one-time warning on first launch. That is
  expected.
- Install `/usr/share/mapplauncherd/privileges.d/harpoon` with the content
  `/usr/bin/harpoon,r`. The binary is `harpoon`, not `harbour-harpoon`, because the app is not
  published on Harbour. It makes mapplauncherd start the binary with egid
  `privileged`. Sailfish's PackageKit then accepts the calls without polkit.
- Never change the `Vendor:` field between releases. libzypp vendor stickiness breaks the
  update path; Storeman keeps `Vendor: meego` for this reason.
- Build targets: `aarch64`, `armv7hl` and `i486` (emulator). Use `noarch` only for pure
  QML/Python subpackages.
- Background checks ship as a systemd **user** service and timer pair in `/usr/lib/systemd/user/`:
  - The service runs `harpoon-cli check --notify --quiet`, which is headless.
  - It posts a `Nemo.Notifications` notification whose remote action opens the app over D-Bus.

## Where the code is
- `core/src/pkg/packagekitbackend.*`: PackageKit D-Bus calls, the error-code mapping and the
  transaction queue.
- `core/src/pkg/installhandlerbackend.*`: the installation-handler fallback.
- `core/src/pkg/rpminspector.*`: `rpm -qp` / `rpm -q` parsing.
- `core/src/app/appinstaller.*`: the pre-install checks listed below.
- `rpm/harpoon.spec`: the package. It ships the app, `harpoon-cli`, the desktop file,
  privileges.d entry, D-Bus activation file and systemd user units, and holds the Chum metadata.
  Release and Chum steps are in `docs/packaging.md`.
- `gui/harpoon.desktop`, `gui/privileges/harpoon`, `gui/dbus/*.service`,
  `gui/systemd/harpoon-check.{service,timer}`: the platform integration files.
- `core/src/app/backgroundscheduler.*`: enables or disables the timer over the systemd user D-Bus
  API and writes the interval drop-in.
- `core/src/notify/notifier.*`: freedesktop notifications with Nemo hints and remote actions,
  encoded the way nemo-qml-plugin-notifications does it.
- Tests: `core/tests/tst_packagebackends.cpp` (mock PackageKit and handler on a private
  dbus-daemon) and `core/tests/tst_appinstaller.cpp` (end to end with real RPMs).
- Device validation steps: `docs/device-testing.md`.

## Installing other RPMs

### Default backend: `PackageKitBackend`
- System bus. Service `org.freedesktop.PackageKit`, object `/org/freedesktop/PackageKit`.
- Calls:
  - `CreateTransaction` returns a transaction path.
  - On that path, interface `org.freedesktop.PackageKit.Transaction`:
    - `InstallFiles(flags, [absolutePaths])`: install downloaded RPMs.
    - `Resolve(filter=installed, [names])`: returns ids `name;version-release;arch;installed`.
    - `RemovePackages([ids], allowDeps=false, autoremove)`.
  - Listen to the `Package`, `ErrorCode` and `Finished` signals.
- PackageKit runs **one transaction at a time**. Push every call through a single global queue,
  and handle stalls with a timeout (packagekitd can hang for up to about 300 s).
- Local RPMs need no GPG signature: the zypp backend sets `gpgcheck=false` for them.
- `GetDetailsLocal` is **not supported** by the zypp backend. Use `rpm -qp` instead (see below).
- Reference implementations:
  - Storeman: `src/ornpktransaction.cpp`, raw QtDBus.
  - Chum GUI: `src/chum.cpp`, PackageKit-Qt.

### Fallback backend: `InstallHandlerBackend`
For sandboxed builds, or when the privileged calls fail.
- Session bus. Service, object path and interface:
  `org.sailfishos.installationhandler` / `/org/sailfishos/installationhandler` / `org.sailfishos.installationhandler`.
- Call `installFiles(QStringList)` with **`file://` URLs**, not plain paths.
- Wait for the `installFinished(bool success, QString error)` signal.
- Requires the sailjail permission `ApplicationInstallation`. The user confirms each install
  in a system dialog, and "Allow untrusted software" must be enabled.

## Inspecting packages
- Read a downloaded RPM before installing it:
  ```sh
  rpm -qp --qf '%{NAME}\t%{EPOCH}\t%{VERSION}\t%{RELEASE}\t%{ARCH}\t%{VENDOR}\n' file.rpm
  ```
- Read the installed state of a package:
  ```sh
  rpm -q --qf '%{NAME}\t%{EPOCH}\t%{VERSION}\t%{RELEASE}\t%{ARCH}\t%{VENDOR}\n' <name>
  ```
  Exit code 1 means the package is not installed.
- Device arch: `rpm --eval %{_arch}`. Fallback: `uname -m`, mapping `armv7l`→`armv7hl` and
  `i686`→`i486`.
- Device OS version: `VERSION_ID` in `/etc/sailfish-release`.
- Compare EVRs with **rpmvercmp semantics**: epoch first, then version, then release, each
  segment-wise, with `~` sorting before everything. Do not use semver or the ObtainX tag
  comparator here; that comparator is only for forge tags versus the installed `VERSION`.

## Checks before every install
1. The file's arch is the device arch or `noarch`. Exclude `.src.rpm`, `-debuginfo` and
   `-debugsource`.
2. `%{NAME}` matches the tracked app id. A temporary id adopts the real name. On a mismatch,
   stop unless `allowIdChange` is set.
3. Compare EVRs. A lower EVR is a downgrade: refuse unless the user confirms. An equal EVR is a
   reinstall: ask the user.
4. If `%{VENDOR}` differs from the installed package's vendor, warn the user that the zypp
   update may be refused, and show both vendors.
5. Install every selected subpackage of one release in **one** `InstallFiles` call.

## Testing
- Unit-test everything in the core library on desktop Linux, with PackageKit and rpm mocked
  behind the `PackageBackend` and `RpmInspector` interfaces.
- On-device checks need a real SFOS 5.x phone or the SDK emulator. These remain open:
  - Whether privileged `InstallFiles` respects the "Allow untrusted software" setting.
  - The exact UX of the `Sandboxing=Disabled` warning.
