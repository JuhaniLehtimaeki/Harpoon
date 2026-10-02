# Project layout and packaging

Generated and verified with `sfdk init -t qtquick2app` on SDK 3.13.5. Regenerate rather than
trusting this file if the SDK has moved.

## What the template produces

```
harbour-myapp/
  harbour-myapp.pro              qmake project
  harbour-myapp.desktop          launcher entry + sandbox profile
  src/harbour-myapp.cpp          entry point
  qml/harbour-myapp.qml          root ApplicationWindow  <- name is load-bearing
  qml/cover/CoverPage.qml
  qml/pages/FirstPage.qml
  qml/pages/SecondPage.qml
  icons/{86x86,108x108,128x128,172x172}/harbour-myapp.png
  rpm/harbour-myapp.spec
  rpm/harbour-myapp.changes.in         rename to .changes to get a changelog
  rpm/harbour-myapp.changes.run.in
  translations/harbour-myapp.ts
  translations/harbour-myapp-de.ts     example locale; remove or replace
  .gitattributes
```

Two project types exist — `sfdk init --list-types`:

- `qtquick2app` — C++ entry point plus QML. Builders: `qmake` (default) or `cmake`.
- `qtquick2app-qmlonly` — QML only, no compiled binary. `qmake` only.

`sfdk` also creates `.sfdk/` in the project directory to hold build state between invocations.

## The name is load-bearing

The template's `.pro` opens with a NOTICE saying so, and it is the single most common way to
break a Sailfish app. `TARGET` propagates into seven places:

1. `TARGET` in the `.pro`
2. `qml/<TARGET>.qml` — `SailfishApp::main()` resolves the root QML file from `TARGET`
3. `<TARGET>.desktop`
4. `Icon=` and `Exec=` inside that desktop file
5. `icons/<size>/<TARGET>.png`
6. `Name:` in `rpm/<TARGET>.spec`
7. `translations/<TARGET>*.ts`

Rename one and the app builds, packages, validates and installs — then launches to a blank
screen, because `SailfishApp::main()` looks for a QML file that is not there. There is no error
message worth the name. After any rename, confirm all seven agree.

`[X-Sailjail] ApplicationName` is the exception: it does not have to match, and the template
deliberately sets it to a shortened form. It only has to stay stable, because it names the
app's persistent data directory.

## Layout conventions

Beyond the names that must agree, the platform conventions page fixes a few more:

- **Lowercase and dash-separated**, and the `.pro` filename matches the project folder. The
  `harbour-` prefix is a Harbour requirement on top of this; the style is the same either way.
- **The entry point is named for the app** — `src/harbour-myapp.cpp`, never `main.cpp`. The SDK
  template already does this, so it is a rule to preserve rather than one to apply.
- **`tests/auto/`** is where automated tests go. Nothing packages them by default; see
  `testing-and-debug.md`.
- `qml/pages/` and `qml/cover/` as the template generates them.

The conventions page also specifies a layout for QML *modules* installed into
`/usr/lib/qt5/qml/Sailfish/`. That is for Jolla's own middleware — a Harbour app cannot install
there, and this repo does not cover it.

Code style within these files — QML and C++ — is in `coding-style.md`.

## The `.pro` file

```qmake
TARGET = harbour-myapp

CONFIG += sailfishapp                 # links libsailfishapp, sets up deployment
CONFIG += sailfishapp_i18n            # builds translations on every build

SOURCES += src/harbour-myapp.cpp

DISTFILES += qml/harbour-myapp.qml \
    qml/pages/FirstPage.qml \
    rpm/harbour-myapp.spec \
    translations/*.ts \
    harbour-myapp.desktop

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172
TRANSLATIONS += translations/harbour-myapp-de.ts
```

`CONFIG += sailfishapp` is what makes this a Sailfish app rather than a generic Qt one: it
links `libsailfishapp` and arranges deployment into `/usr/share/$TARGET`.

`DISTFILES` does not affect the build — it is what makes files visible in the IDE. Deployment
of `qml/` is handled by the `sailfishapp` config.

**Never `include()` a path outside the project tree.** `sfdk` builds from a copy of the source
and packages from a tarball, so `include($$PWD/../shared/thing.pri)` resolves during a local
build and fails in a clean one. Vendor the directory into the project instead.

## The entry point

```cpp
#include <sailfishapp.h>

int main(int argc, char *argv[])
{
    return SailfishApp::main(argc, argv);
}
```

`SailfishApp::main()` creates the `QGuiApplication` and `QQuickView`, loads
`qml/<TARGET>.qml`, and shows it fullscreen. When you need more control:

- `SailfishApp::application(argc, argv)` → the `QGuiApplication *`
- `SailfishApp::createView()` → a new `QQuickView *`
- `SailfishApp::pathTo(QString)` → a `QUrl` to a resource under the app's data directory
- `SailfishApp::pathToMainQml()` → a `QUrl` to the main QML file

`pathTo()` and `pathToMainQml()` resolve relative to the installed binary's location, so they
behave differently for a binary run from a build directory than for an installed one.

## Exposing C++ to QML

Register the type, then import it. The namespace must start `harbour.` — every prefix a
developer reaches for by habit (`com.example`, `org.*`) is banned by Harbour.

```cpp
#include <sailfishapp.h>
#include <QtQuick>

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    QScopedPointer<QQuickView> view(SailfishApp::createView());

    qmlRegisterType<DemoModel>("harbour.myapp", 1, 0, "DemoModel");

    view->setSource(SailfishApp::pathTo("qml/harbour-myapp.qml"));
    view->show();
    return app->exec();
}
```

```qml
import harbour.myapp 1.0

SilicaListView {
    model: DemoModel { id: dmodel }
    delegate: BackgroundItem { onClicked: dmodel.activate(index) }
}
```

Models derive from `QAbstractListModel`; methods callable from QML are `Q_INVOKABLE`.

## The desktop file

```desktop
[Desktop Entry]
Type=Application
X-Nemo-Application-Type=silica-qt5
Icon=harbour-myapp
Exec=harbour-myapp
Name=My App
Name[de]=Meine App

[X-Sailjail]
OrganizationName=org.example
ApplicationName=myapp
Permissions=Internet;Pictures
```

`Icon` and `Exec` must be exactly the package name. `X-Nemo-Application-Type=silica-qt5` is
required for QML-only apps and harmless otherwise.

`OrganizationName` and `ApplicationName` name the three folders Sailjail creates for the app at
its first launch, and the only places it can keep files:

| Folder | `QStandardPaths` |
| --- | --- |
| `~/.local/share/<OrganizationName>/<ApplicationName>` | `AppDataLocation` |
| `~/.config/<OrganizationName>/<ApplicationName>` | `AppConfigLocation` |
| `~/.cache/<OrganizationName>/<ApplicationName>` | `CacheLocation` |

A write anywhere else in `$HOME` succeeds inside the app, lands in a scratch layer, and is gone
at the next launch — no error, no warning. The common victim is `QSettings`: constructed with no
file name, it writes `~/.config/<OrganizationName>/<ApplicationName>.conf`, the file *beside*
the config folder, so settings silently reset on every restart. Give it a path inside the folder:

```cpp
QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                   + "/settings.ini", QSettings::IniFormat);
```

Seen on a Jolla C2 at 5.1.0.11. Whether the cache folder survives a restart was not checked, so
keep nothing there you cannot rebuild. Changing either name after release orphans the users'
data.

Permission names and the allowed `[X-Sailjail]` keys are in the `jolla-store-rules-check` skill.

## The spec file

```spec
Name:       harbour-myapp
Summary:    My harbour-myapp application
Version:    0
Release:    1
License:    LICENSE
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  desktop-file-utils

%build
%qmake5
%make_build

%install
%qmake5_install
desktop-file-install --delete-original \
  --dir %{buildroot}%{_datadir}/applications \
  %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
```

`Version: 0` means the version is taken from git tags at build time. Express build
dependencies with `pkgconfig()` where the package provides one.

**rpm expands macros inside comments.** A `#` comment is not protected: one naming `%qmake5`
in `%install` runs qmake right there, and the build fails with `Cannot find file:` for every
other word of the comment. Write `%%qmake5`, or name the macro without its percent sign.

SDK 3.10 and later generate the spec directly; older projects may carry a `.yaml` that
generates it. If you find a `rpm/*.yaml`, that file is the source and the spec is derived.

**Bundled prebuilt libraries** need RPM's automatic dependency scanning switched off, or RPM
advertises the library's symbols system-wide and tries to satisfy its `NEEDED` entries from
your package:

```spec
%define __provides_exclude_from ^%{_datadir}/%{name}/lib/.*$
%define __requires_exclude ^lib(foo|bar)\\.so.*$
```

## Changelog

`rpm/<name>.changes.in` is a template. Rename it to `rpm/<name>.changes` to have entries
appear in the RPM. `sfdk changelog` extracts one from the git commit log instead.

## Translations

`CONFIG += sailfishapp_i18n` builds `.ts` files into `.qm` on every build and deploys them.
List each locale in `TRANSLATIONS`. Wrap user-visible strings in `qsTr()` in QML and `tr()` in
C++, then use the standard Qt tools — `lupdate` to harvest strings into `.ts`, `lrelease` to
compile. Jolla's community translation site is `translate.sailfishos.org`.

The template ships a German example. If you are not localising, comment out both the
`TRANSLATIONS` line and the `Name[de]` line in the desktop file — a stale `Name[de]` will show
a wrong app name to German users.

Keep the `translations/` folder even then. `sailfishapp_i18n` runs `lupdate` into it; without the
folder that step fails, the `|| :` after it swallows the error, and the build succeeds with
nothing translated.
