---
name: jolla-store-rules-check
description: >
  The rules a Sailfish OS app must pass to get into the Jolla Store (submitted through
  Harbour), and how to check an app against them before submitting. Use when: (1) preparing a
  Sailfish app for Harbour or the Jolla Store, (2) choosing a QML import, a shared library or
  a Sailjail permission and needing to know whether it is allowed, (3) `sfdk check` or the RPM
  validator reports an error, (4) naming a Sailfish package, its binary, its .desktop file or
  its icons, (5) deciding where an app may write files under sandboxing, or (6) checking
  whether a Sailfish app will pass Harbour's automated and design review.
metadata:
  written-against:
    sailfish-os: 5.1.0.11
    sdk: 3.13.5
    validator: sdk-harbour-rpmvalidator, master
---

# Jolla Store rules

Apps reach the Jolla Store through Harbour, Jolla's submission portal. Its intake validator is
a fixed, enumerable rule set — not a matter of taste — and almost none of it is enforced by the
compiler. An app that builds and runs perfectly can still fail every rule below.

**The gate is one command:**

```bash
sfdk -c target=<target> check                       # validates what you just built
sfdk -c target=<target> check path/to/harbour-foo-1.0-1.aarch64.rpm   # or a prebuilt RPM
```

Run it before claiming an app is ready. It costs seconds, it is the same validator Harbour QA
runs, and it is the only authoritative answer. Everything in this file explains what it checks
and how to satisfy it — it does not replace running it.

The rules live in `github.com/sailfishos/sdk-harbour-rpmvalidator`, as `*.conf` files beside
`rpmvalidation.sh`. When this file and that repo disagree, the repo is right; the lists move
between releases.

## Naming, and the five names that must agree

The package name must match `^harbour-[-a-z0-9_.]+$` — lowercase, starting `harbour-`.

Everything else is derived from it. With `NAME` as the package name, the validator expects
exactly:

| Thing | Path |
| --- | --- |
| Main binary | `/usr/bin/$NAME` — must be an executable ELF file |
| App data (QML, images) | `/usr/share/$NAME/` |
| Bundled libraries | `/usr/share/$NAME/lib/` — the only other allowed rpath |
| Desktop file | `/usr/share/applications/$NAME.desktop` |
| Icons | `/usr/share/icons/hicolor/<size>/apps/$NAME.png` |

So in practice one string has to be identical in six places: `TARGET` in the `.pro`,
`qml/$NAME.qml` (which `libsailfishapp` resolves from `TARGET`), `$NAME.desktop`, `Name:` in
`rpm/$NAME.spec`, the icon filenames, and the `Icon=`/`Exec=` lines inside the desktop file.

**`sfdk check` catches the packaged half of this, but not a QML entry point that no longer
matches `TARGET`** — that combination builds, validates, installs, and launches to nothing.
Check it by hand when renaming anything.

## The desktop file

Required, and validated line by line:

```desktop
[Desktop Entry]
Type=Application
Name=My App
Icon=harbour-myapp
Exec=harbour-myapp

[X-Sailjail]
OrganizationName=org.example
ApplicationName=harbour-myapp
Permissions=Internet;Pictures
```

- `Name` must be present and non-empty.
- `Icon` must be exactly the package name — no path, no extension.
- `Exec` must be exactly the package name.
- A QML-only app (`sailfish-qml`) additionally needs `X-Nemo-Application-Type=silica-qt5`.

Only four keys are allowed in `[X-Sailjail]`: **`Permissions`, `OrganizationName`,
`ApplicationName`, `ExecDBus`**. Any other key is a validation error.

`OrganizationName` may not be `com.jolla` or `org.sailfishos`.

## Sailjail permissions

Twenty-one are allowed. All declared permissions are granted at launch; there is no runtime
prompt, so ask for the minimum.

`Accounts`, `Audio`, `Bluetooth`, `Camera`, `Compatibility`, `Contacts`, `Documents`,
`Downloads`, `Internet`, `Location`, `MediaIndexing`, `Microphone`, `Music`, `NFC`,
`Pictures`, `PublicDir`, `RemovableMedia`, `Secrets`, `UserDirs`, `Videos`, `WebView`.

Anything else — including permissions that exist in `sailjail-permissions` but are not on this
list — fails intake.

**Where an app may write.** The `OrganizationName`/`ApplicationName` pair defines the only
persistent writable directory: `$HOME/.local/share/<OrganizationName>/<ApplicationName>`. The
validator greps every shipped file for literal `/home/nemo/` and `/home/defaultuser/` strings
and errors on each one. Use XDG base directories, or `StandardPaths` from `Sailfish.Silica`,
and never build a home path yourself. Code that writes to `~/.config/myapp` works unsandboxed
in development and fails silently once installed.

## QML imports

Imports are banned by **prefix**, then re-permitted by an explicit allow list. The banned
prefixes:

`Amber.*`, `Bluetooth.*`, `Meego.*`, `Mer.*`, `Nemo.*`, `NemoMobile.*`, `Sailfish.*`, `Qt*`,
`org.nemomobile.*`, `org.sailfishos.*`, `com.jolla.*`, `com.nokia.*`, `com.meego.*`,
`org.kde.bluezqt`.

`Qt*` bans essentially the whole Qt QML namespace, so the allow list is what you actually have.
Exact strings, version included:

**Sailfish** — `Sailfish.Silica 1.0`, `Sailfish.Pickers 1.0`, `Sailfish.Share 1.0`,
`Sailfish.WebView 1.0` (`.Controls`, `.Pickers`, `.Popups`), `Sailfish.WebEngine 1.0`, and
since 4.5.0 `Sailfish.Secrets`, `.Crypto`, `.Media`, `.Contacts`, `.Accounts`, `.Bluetooth`,
`.Telephony`, all `1.0`.

**Qt** — `QtQml 2.0`–`2.2`, `QtQml.Models 2.1`–`2.3`, `QtQuick 2.0`–`2.6`,
`QtQuick.Layouts 1.0`–`1.1`, `QtQuick.LocalStorage 2.0`, `QtQuick.Particles 2.0`,
`QtQuick.Window 2.0`–`2.2`, `QtQuick.XmlListModel 2.0`, `Qt.labs.folderlistmodel 2.1` (4.6.0+),
`QtMultimedia 5.0`–`5.6`, `QtWebSockets 1.0`–`1.1`, `QtSensors 5.0`–`5.2`,
`QtGraphicalEffects 1.0`, `QtPositioning 5.2`/`5.4`, `QtLocation 5.0`/`5.3`/`5.4`,
`QtFeedback 5.0` (only `ThemeEffect.play()` with `PressWeak`/`Press`/`PressStrong`, and
`.supported`).

**Nemo** — `Nemo.Notifications 1.0`, `Nemo.DBus 2.0`, `Nemo.Configuration 1.0`,
`Nemo.Thumbnailer 1.0`, `Nemo.KeepAlive 1.2`, `Nemo.Time 1.0`, `org.nemomobile.contacts 1.0`
(4.5.0+).

**Other** — `Amber.Web.Authorization 1.0`, `Amber.Mpris 1.0`,
`org.freedesktop.contextkit 1.0`, `io.thp.pyotherside 1.0`–`1.5`, `org.kde.bluezqt 1.0`
(4.5.0+).

Note what is *not* there: **`QtQuick.Controls` in any version**. Silica is the control set.

**Renamed imports.** These still parse but are rejected — the validator names the replacement:

| Deprecated | Use |
| --- | --- |
| `org.nemomobile.notifications 1.0` | `Nemo.Notifications 1.0` |
| `org.nemomobile.dbus 2.0` | `Nemo.DBus 2.0` |
| `org.nemomobile.configuration 1.0` | `Nemo.Configuration 1.0` |
| `org.nemomobile.thumbnailer 1.0` | `Nemo.Thumbnailer 1.0` |

**Your own C++ types.** Every prefix a developer would reach for by habit is banned, so
`qmlRegisterType<Foo>("com.example", 1, 0, "Foo")` fails intake. Register under `harbour.`:

```cpp
qmlRegisterType<DemoModel>("harbour.myapp", 1, 0, "DemoModel");
```

**Relative QML imports** may not point outside `/usr/share/$NAME`.

## Libraries

Linking is allow-listed too, from `allowed_libraries.conf`. Broadly: Qt 5 core modules, Silica,
the standard C/C++ runtime, zlib/bz2/lzma, SQLite, libxml2, OpenGL ES and EGL, PulseAudio,
Ogg/Vorbis, SDL2, GLib, and OpenSSL 3.

Bundled private libraries go in `/usr/share/$NAME/lib/`, which is the only non-standard rpath
the validator accepts.

Deprecated at 5.1: `libssl.so.1.1`, `libcrypto.so.1.1` — link OpenSSL 3. Dropped earlier:
QtWebKit and `libQt5WebKit.so.5` (4.5.0), `libpng15.so.15` and OpenSSL 1.0 (5.1).

Binaries must not require a glibc newer than **2.34** on any architecture.

## Packaging hygiene

The validator also rejects:

- Debug symbols or sources in the package — no `/usr/lib/debug`, no `/usr/src/debug`. `sfdk
  build -d` emits these as separate `-debuginfo`/`-debugsource` RPMs; ship neither.
- Source control directories (`.git`, `.svn`, …).
- Data files with the executable bit set.
- RPM scriptlets and triggers.
- Icons of the wrong dimensions for the directory they sit in.

Icons are expected at **86x86, 108x108, 128x128 and 172x172**. Missing sizes are a warning
rather than an error, but ship all four. In a qmake project:

```
SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172
```

## Before submitting

`sfdk check` answers the mechanical half of the gate. The human half is design review, and it
is the most common reason a technically valid app comes back.

Both halves of that review — Jolla's UI *Definition of Done* and the eleven documented Silica
pitfalls — are in the **`sailfish-ui-design`** skill (`references/review-checklist.md`). Walk the
app against it before submitting.
