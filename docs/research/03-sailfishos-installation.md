# SailfishOS: how a third-party updater can install RPMs

State as of Oct 2026 (SFOS 5.1 "Pispala"; 5.2 on the Jolla Phone). Items marked
**unverified** must be tested on a device.

## Two install paths (both end in PackageKit + libzypp)

### A. Privileged PackageKit, the way Storeman and Chum do it (silent installs)
- **What to call:** `org.freedesktop.PackageKit` at `/org/freedesktop/PackageKit` on the system bus.
  - Create a transaction, then call `org.freedesktop.PackageKit.Transaction` methods:
    `InstallFiles(flags, [paths])`, `Resolve(FilterInstalled, [names])`, `GetPackages`,
    `RemovePackages`, `RefreshCache`, `GetUpdates`.
  - Storeman uses raw `QDBusInterface::asyncCall`s (`src/ornpktransaction.cpp`).
    Chum GUI uses PackageKit-Qt (`packagekitqt5`).
- **Authorization: membership of the `privileged` group, not polkit.**
  - The package ships `/usr/share/mapplauncherd/privileges.d/<app>` containing `/usr/bin/<app>,r`.
  - mapplauncherd then starts the binary with egid `privileged`.
  - Sailfish's PackageKit fork (`pk_transaction_builtin_policy_allow()` in `src/pk-transaction.c`)
    allows requests from `privileged`-group callers without asking polkit.
- **Sandboxing:** sailjail must not sandbox the app. Chum GUI sets
  `[X-Sailjail] Sandboxing=Disabled`. Since sailjail commit b934730 (Apr 2026), that shows a
  one-time warning on first launch.
- **Local RPMs need no GPG signature.** The zypp backend copies the file into a temporary
  plaindir repo with `setGpgCheck(false)`.
- **Unverified:** whether the "Allow untrusted software" setting applies to this path.
- **Not allowed in Harbour.** Distribute via **Chum** (preferred) or OpenRepos.

### B. System installation handler (sandboxed, user confirms each install)
- **Interface:** `org.sailfishos.installationhandler` on the session bus, object
  `/org/sailfishos/installationhandler`.
  - Call `installFiles(QStringList fileUrls)` with **`file://` URLs**.
  - Wait for the `installFinished(bool success, QString error)` signal.
  - `removePackages(QStringList)` also exists.
- **Sailjail permission:** `ApplicationInstallation`. It whitelists the D-Bus name and
  `/var/cache/PackageKit/downloads`.
- **Example:** harbour-music-sleep-timer (`src/daemoninstaller.cpp`).
- **User experience:** a system SideloadDialog appears for each install. "Allow untrusted
  software" must be enabled (**unverified detail**).
- **Unverified:** whether Harbour accepts `ApplicationInstallation`. It is not in the
  published allowed-permissions list, so assume it is not accepted.

## Other relevant facts
- **Architectures:**
  - `aarch64`: Xperia 10 II and later, Jolla C2, the new Jolla Phone.
  - `armv7hl`: older devices.
  - `i486`: the emulator.
  - `noarch`: QML/Python apps.
  - Detect with `rpm --eval %{_arch}`, or `uname -m` mapped (`armv7l`→`armv7hl`, `i686`→`i486`).
  - **Assume no armv7hl multilib on aarch64.**
- **Asset naming:** `name-version-release.arch.rpm`, often with an SFOS-target suffix in the
  release (e.g. `1.2-1_sfos4.6`).
- **Installed version:**
  - `rpm -q --qf '%{NAME} %{EPOCH}:%{VERSION}-%{RELEASE} %{ARCH}\n' <name>` works unprivileged.
  - PackageKit `Resolve` returns `name;ver-rel;arch;installed`.
  - Compare installed packages with **rpmvercmp (EVR)** semantics.
- **Before installing:** `rpm -qp --qf ...` reads name, EVR, arch and vendor from the file.
  Storeman does this, because PackageKit's `GetDetailsLocal` is unsupported with zypp.
- **Vendor stickiness:** libzypp refuses vendor changes on update. Storeman keeps
  `Vendor: meego` for exactly this reason. Detect a vendor mismatch and explain it to the user.
- **PackageKit runs one transaction at a time.** Serialize all calls.
  `pkcon`/packagekitd can stall (300 s idle timeout).
- **Repo management:** `org.nemo.ssu` on D-Bus (`addRepo`, `modifyRepo`). Only needed if we
  also manage rpm-md repos.

## UI / runtime stack options

| Option | Status |
|---|---|
| **Qt 5 (5.6) + QML + Sailfish Silica, C++** | Standard and best supported. Storeman and Chum use it. QtNetwork, QtDBus, packagekitqt5, Nemo.Notifications and Nemo.KeepAlive are all available. |
| Python via PyOtherSide + Silica QML | Allowed in Harbour. Fine for HTTP/JSON logic; D-Bus needs dbus-python or a C++ shim. |
| Flutter (MrCyjaneK/flutter-sailfishos, Aug 2026) | Experimental: aarch64 only, a separate runtime RPM, no Silica look. Not a base for a product. |
| Aurora OS Flutter (OMP) | Targets Aurora OS, not SailfishOS. |

## Background checks and notifications
- **nemo-keepalive `BackgroundActivity`:** iphb wakeups at slots from 30 s to 24 h. Requires a
  running process (e.g. the app kept on its cover).
- **systemd user `.timer` + a headless `--check` mode:** works while the GUI is closed. A
  sandboxed app needs the `AppLaunch` permission to manage units.
- **Notifications:** `Nemo.Notifications`, with `remoteActions` to open the app via D-Bus.

## Prior art
- **Exist:** Storeman (OpenRepos client), Storeman Installer, SailfishOS:Chum GUI and the
  closed-source Jolla Store.
- **Reusable forge code:** Chum GUI already queries GitHub GraphQL, GitLab GraphQL and
  Forgejo `/api/v1` for project and release info (`src/projectgithub.cpp`,
  `projectgitlab.cpp`, `projectforgejo.cpp`).
- **Gap:** no tool was found that tracks forge-release RPMs the way Obtainium does.

## Sources
- harbour-storeman: <https://github.com/storeman-developers/harbour-storeman>
- harbour-storeman-installer: <https://github.com/storeman-developers/harbour-storeman-installer>
- sailfishos-chum-gui: <https://github.com/sailfishos-chum/sailfishos-chum-gui>
- Sailfish PackageKit fork: <https://github.com/sailfishos/packagekit>
- sailjail and its permissions: <https://github.com/sailfishos/sailjail>,
  <https://github.com/sailfishos/sailjail-permissions>
- mapplauncherd: <https://github.com/sailfishos/mapplauncherd>
- D-Bus API reference: <https://docs.sailfishos.org/Reference/Core_Areas_and_APIs/D-Bus_APIs/>
- Harbour allowed permissions: <https://docs.sailfishos.org/Develop/Apps/Harbour/Allowed_Permissions/>
- installationhandler example: <https://github.com/RikudouSage/harbour-music-sleep-timer>
- nemo-keepalive: <https://github.com/sailfishos/nemo-keepalive>
- flutter-sailfishos: <https://github.com/MrCyjaneK/flutter-sailfishos>
