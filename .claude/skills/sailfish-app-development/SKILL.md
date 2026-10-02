---
name: sailfish-app-development
description: >
  Write, build and package applications for Sailfish OS with Silica QML, qmake and the sfdk
  command line tool. Use when: (1) writing or reviewing QML for a Sailfish app — pages, covers,
  pulley menus, list views, dialogs, (2) working in a project with a harbour- prefix, a
  .desktop file, an rpm/*.spec or CONFIG += sailfishapp, (3) running sfdk to init, build,
  deploy, check or debug, (4) exposing C++ types to QML on Sailfish or setting up
  translations, (5) integrating with the phone — notifications, stored settings, D-Bus,
  background execution, file pickers, sharing, OAuth or secrets, or (6) the words Sailfish,
  Silica, Jolla, sfdk, sailjail or harbour appear in a build, packaging or UI task. For UI design guidance see sailfish-ui-design; for the Harbour
  submission rules see jolla-store-rules-check.
metadata:
  written-against:
    sailfish-os: 5.1.0.11
    sdk: 3.13.5
    targets: aarch64, armv7hl, i486
---

# Sailfish OS app development

A Sailfish app is a Qt 5 / QML application built against **Silica**, packaged as an RPM, and
sandboxed by **Sailjail**. It is written in QML with an optional C++ core, built by qmake or
CMake inside a cross-compilation build engine, and driven from the command line by `sfdk`.

## Read the SDK, not the web

The published documentation is thinner and older than what is installed on this machine. The
SDK carries the authoritative copies:

| Where | What |
| --- | --- |
| `~/SailfishOS/mersdk/targets/<target>.default/usr/lib*/qt5/qml/Sailfish/Silica/` | Every Silica component, as readable QML source, plus `qmldir` — the public API contract |
| `~/SailfishOS/documentation/sailfishsilica*.qch` | The offline API reference |
| `~/SailfishOS/examples/` | Working first-party apps: `componentgallery`, `cameragallery`, `mediagallery`, `notificationgallery` |
| `sfdk --help-all` | The complete command reference |

When a component's behaviour is unclear, read its `.qml` source in the target. It is right
there and it cannot be out of date.

## The compiler is not the gate

Almost everything that makes an app a *Sailfish* app is invisible to qmake and to QML's own
parser. Four bodies of rules fail at validation, at launch, or at review — never at build:

**Silica idiom.** The interaction model is gestures, pulley menus and covers, not buttons and
toolbars. Literal margins, undersized touch targets, uncoloured labels and unlabelled text
fields all compile perfectly. There is a documented list of eleven such mistakes; see
`references/silica.md`.

**Harbour's allow lists.** QML imports are banned by prefix and re-permitted by an explicit
list. `import QtQuick.Controls 2.0` — the reflex import — is not on it, and neither is a C++
type registered under `com.example` instead of `harbour.`. Neither is a compile error. See the
`jolla-store-rules-check` skill.

**The name that must agree in seven places.** `TARGET` propagates into the root QML filename,
the desktop file and its `Icon=`/`Exec=` lines, the icons, the spec `Name:`, and the
translations. `SailfishApp::main()` resolves the root QML file from `TARGET`, so breaking the
chain gives you a package that builds, validates, installs — and launches to a blank screen.
See `references/project-layout.md`.

**Sailjail.** Permissions are declared in the `.desktop` file, not in code, and all are granted
at launch. `OrganizationName`/`ApplicationName` name the only three folders an app can keep
files in: `~/.local/share/<Org>/<App>`, `~/.config/<Org>/<App>` and `~/.cache/<Org>/<App>`.
A write anywhere else in `$HOME` succeeds, lands in a scratch layer, and is gone at the next
launch, with no error. That includes the file a default `QSettings` writes,
`~/.config/<Org>/<App>.conf`. See `references/project-layout.md`.

## Run the cheapest loop that can fail

| Loop | Needs | Catches |
| --- | --- | --- |
| Read `references/silica.md` and the Harbour lists | nothing | idiom and disallowed-import errors |
| `sfdk build` | build engine, one at a time | compile, link, spec, packaging |
| `sfdk check` | a built RPM | every Harbour intake rule, rpmlint, the spec's `%check` |
| `sfdk deploy --rsync` | a device or emulator | QML changes, without repackaging |
| `sfdk deploy --pkcon` + launch | a device or emulator | install, sandbox, real layout |

`sfdk check` is the most under-used gate in Sailfish development: it costs seconds, needs no
device, and is the same validator Harbour QA runs.

## Orienting in an unfamiliar Sailfish project

```bash
grep -E '^TARGET|CONFIG \+=' *.pro          # the name, and whether it is a sailfishapp
cat .sfdk/target 2>/dev/null                 # which target it was last built against
sed -n '/\[X-Sailjail\]/,$p' *.desktop       # permissions and the data directory
sfdk tools list                              # which targets this SDK actually has
```

Then confirm the name agrees everywhere — this is the cheapest real bug to find:

```bash
N=$(grep -oP '^TARGET\s*=\s*\K\S+' *.pro)
ls qml/$N.qml $N.desktop rpm/$N.spec icons/*/$N.png 2>&1
grep -E '^(Icon|Exec)=' $N.desktop
```

## Working rules

- **Pass `-c target=<target>` on every `sfdk` call**, or set it once with
  `sfdk config --global --push target <target>`. The default session scope is lost whenever a
  command runs in a fresh shell, which is every time for a script or an agent.
- **One build at a time.** Simultaneous builds collide during packaging.
- **Keep the project inside the SDK workspace** (`$HOME` by default), or move it with
  `sfdk config workspace=<dir>`. `sfdk` refuses to build outside it.
- **Never `include()` a path outside the project tree.** `sfdk` builds from a copy and packages
  from a tarball, so an outside path works locally and fails in a clean build.
- **Take sizes, spacing, colours and fonts from `Theme`.** A literal pixel value is a bug on a
  device with a different `pixelRatio`.
- **Follow the platform's own style** — no semicolons in QML, grouped properties, `_` for
  private symbols, Qt 5 function-pointer `connect()`. See `references/coding-style.md`.
- **Register your own QML types under a `harbour.` namespace** from the start. Changing it
  later means touching every import.
- **Run `sfdk check` before calling an app done**, not just before submitting.
- **Verify by building and running.** Claims about this platform are cheap and the published
  documentation is demonstrably behind the installed SDK.

## Reference files

Read the one that matches the task rather than all of them.

- **`references/silica.md`** The component inventory at this release, the `Theme` constants,
  the eleven documented pitfalls, and the Qt Quick Controls habits to unlearn. Read this before
  writing QML.
- **`references/coding-style.md`** How the code is written rather than what it does: the QML
  and C++ style rules, the declaration order, and which Qt conventions they sit on top of. Read
  it with `silica.md` before writing QML, and before reviewing someone else's.
- **`references/project-layout.md`** What `sfdk init` generates, the seven places the name
  appears, the `.pro` / `.desktop` / `.spec` files, exposing C++ to QML, and translations.
- **`references/sfdk.md`** The command surface, the three configuration scopes and the trap in
  the middle one, deploy modes, and the SDK behaviours that waste an afternoon.
- **`references/platform-apis.md`** The platform integration surface: notifications, settings,
  D-Bus, background work, pickers, sharing, OAuth, secrets — which module to import for what,
  and the 49 modules that are installed but cannot be shipped.
- **`references/testing-and-debug.md`** What the platform gives you for tests and diagnostics,
  and — stated plainly — where it gives you nothing.

For how a screen should look and behave — the gesture model, navigation structure, covers,
ambience, and the review checklist a finished UI is walked against — use the
**`sailfish-ui-design`** skill.

For submission rules, allowed imports, permissions and the validator, use the
**`jolla-store-rules-check`** skill.
