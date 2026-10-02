# Harpoon: architecture

Harpoon is an Obtainium-style updater for SailfishOS. It tracks apps published as RPM
release assets on code forges, then downloads and installs them.

Status: **Phases 1–3 implemented; awaiting device validation** (`docs/device-testing.md`). This builds on the research in `docs/research/01-03`.

## Decisions (Oct 2026)
| # | Question | Decision |
|---|---|---|
| 1 | Language / stack | **C++17 + Qt 5.6, Silica QML UI** |
| 2 | Distribution | **Chum** (maybe OpenRepos too). No Harbour build for now, but the installation-handler backend stays in the design. |
| 3 | Android APKs via AppSupport | **Out of scope** |
| 4 | Obtainium/ObtainX backup import | **Out of scope.** Harpoon has its own backup format. |
| 5 | Licence | **GPL-3.0-or-later** |

## Goals (v1)
- Add an app by pasting a forge URL: GitHub, Codeberg/Forgejo/Gitea, GitLab, SourceHut,
  SourceForge, Jenkins, a generic HTML page, or a direct `.rpm` link.
- Pick the correct RPM asset for the device: arch, plus an optional SFOS-version tag.
- Show installed vs. latest version and the changelog. Install, update and uninstall.
- Check for updates in the background and post notifications.
- Export and import Harpoon's own app list and settings.

## Non-goals (v1)
- Android APKs via AppSupport.
- Importing Obtainium/ObtainX backups.
- Managing ssu repositories. Chum and Storeman already do that.
- Harbour distribution. Silent installs need privileges that Harbour forbids.

## Stack
- **C++17 / Qt 5.6 core library + QML/Silica UI.** This is the native SFOS stack.
  - QtNetwork, QtDBus, Nemo.Notifications and Nemo.KeepAlive are all first-class.
  - It is the same stack as Storeman and Chum GUI, so their code is reference material.
  - Flutter on SFOS is experimental and aarch64-only (see research 03).
- The ObtainX Dart code is **ported, not reused**. Its algorithms are small and well defined.
- **Core library rules:**
  - No Silica dependency, and it builds on desktop Linux. Unit tests run in CI against
    recorded API responses.
  - Use **only Qt 5.6 APIs**. Desktop builds use newer Qt, so the compiler won't catch a
    newer API. Where a newer replacement exists, branch on `QT_VERSION`; see
    `net/networktransport.cpp` for an example.
  - Exceptions are not used. Fallible calls return `Result<T>` (`model/error.h`).
  - Style follows the `sailfish-app-development` skill: CamelCase namespaces
    (`Harpoon`, `Harpoon::Keys`), function-pointer `connect()`, ranged-for.

## Module layout

```text
harpoon/
  core/                       # libharpoon-core (Qt Core/Network/DBus only)
    model/        App, Release, Asset, InstalledPackage, AppSettings, Error/Result
    sources/      Source (interface), SourceRegistry,
                  GitHubSource, ForgejoSource(Codeberg/Gitea), GitLabSource,
                  SourceHutSource, SourceForgeSource, JenkinsSource,
                  HtmlSource, DirectLinkSource, (later) RpmMdRepoSource
    pipeline/     ReleaseSelector  (sort, prerelease/draft, title/notes regex, fallback)
                  AssetFilter      (.rpm predicate, user regex, arch, sfos-tag preference)
                  VersionExtractor (regex + match group, pseudo-versions)
                  VersionCompare   (ObtainX-style for tags; rpmvercmp for EVR)
    net/          HttpClient (redirects, headers, rate-limit errors), Downloader (resume, .part)
    pkg/          PackageBackend (interface)
                    PackageKitBackend        (privileged; InstallFiles/Resolve/Remove over D-Bus)
                    InstallHandlerBackend    (sandboxed; org.sailfishos.installationhandler)
                  RpmInspector  (rpm -qp / rpm -q: name, EVR, arch, vendor)
                  DeviceInfo    (rpm --eval %{_arch}, /etc/sailfish-release VERSION_ID)
    store/        AppRepository (one JSON per app, atomic writes), SettingsStore, Import/Export
    update/       UpdateChecker (worker pool, backoff, rate-limit aware), UpdateDecision
  app/            harbour-less Silica UI (QML) + QObject facades over core
  cli/            harpoon-check  (headless: check, notify; used by systemd user timer)
  rpm/            spec, privileges.d entry, .desktop ([X-Sailjail] Sandboxing=Disabled),
                  systemd user timer/service
```

## Key interfaces

```cpp
struct Asset   { QString name; QUrl url; qint64 size = -1; QString sha256; };
struct Release { QString version; QString tag; QString title; QDateTime date;
                 QString changelog; bool prerelease = false; QList<Asset> assets; };

class Source {                     // one per forge; mirrors ObtainX AppSource
public:
    virtual QString id() const = 0;                  // "GitHub", "Forgejo", ... (= overrideSource)
    virtual QStringList hosts() const = 0;           // empty = matched by URL shape
    virtual QUrl standardizeUrl(const QUrl&) const = 0;   // throws InvalidUrl
    virtual Future<QList<Release>> fetchReleases(const QUrl& std, const AppSettings&) = 0;
    virtual SettingsSchema appSettingsSchema() const;     // per-app options
    virtual SettingsSchema sourceConfigSchema() const;    // tokens, proxies
    virtual QHash<QByteArray,QByteArray> headers(const AppSettings&, bool forDownload) const;
};
```

**Differences from ObtainX:**
- `fetchReleases` returns **the release list**, not the single latest release. Selection
  happens in the shared `ReleaseSelector`. This removes the duplication between GitHub,
  Codeberg and GitLab, and lets the UI offer "install an older release".
- **App identity:**
  - It is the **RPM `%{NAME}`**, read from the RPM header with `rpm -qp` after the first
    download. This is the equivalent of ObtainX's temp-id adoption.
  - Until then the app has a temporary id derived from its URL.
  - An optional `packageName` setting lets the user pin the name upfront. When it is set,
    we can check whether the package is already installed before downloading anything.

## Update decision
1. **Installed state:** `rpm -q <name>` (or PackageKit `Resolve`) gives the EVR, or nothing.
2. **Latest version:** the tag or title after version extraction, e.g. `v1.4.2` → `1.4.2`.
3. **Comparison:**
   - Compare the installed `VERSION` with the latest version using the ObtainX-style
     comparator (handles `v` prefixes, `-rc` and similar).
   - If the comparator can't order the two (pseudo-versions, dates), fall back to the install
     receipt. A receipt records which release and asset sha256 were last installed through
     Harpoon. This is ObtainX's pseudo mode.
4. **After download:** compare the RPM's EVR with the installed EVR using rpmvercmp. If it is
   not newer, flag it before installing; it would be a reinstall or downgrade.
5. **trackOnly apps:** only notify.

## Asset selection (replaces the APK/ABI filtering)
1. Keep only assets matching `\.rpm$`, plus the user's `assetFilterRegEx`.
2. **Arch:** keep `.<deviceArch>.rpm`. If none match, keep `.noarch.rpm`. Exclude
   `-debuginfo` / `-debugsource` / `.src.rpm` by default.
3. **SFOS tag** (optional): prefer assets whose name contains the matching tag. Matching
   means the highest `sfosX.Y` that is ≤ the device's VERSION_ID.
4. **Ties:** if more than one asset remains, ask the user and remember the choice
   (`preferredAssetIndex`, like ObtainX).
5. **Multi-package releases** (e.g. `app` plus `app-data` subpackages): install every
   selected RPM in a single `InstallFiles` transaction. The user selects which ones; by
   default this is the name match.

## Install backends
| Mode | How | When |
|---|---|---|
| `PackageKitBackend` (default) | `privileges.d` entry `/usr/bin/harbour-harpoon,r`, unsandboxed; PackageKit D-Bus `InstallFiles` / `RemovePackages` | Silent installs and background updates |
| `InstallHandlerBackend` | sailjail `ApplicationInstallation`; `installationhandler.installFiles(file://…)` | Fallback when privileged calls fail; requires a confirmation each time |

**Rules for both backends:**
- Use one global install queue, because PackageKit runs one transaction at a time.
- Before install: check arch and EVR, and check that the vendor matches the installed
  package. If it doesn't, show a clear "vendor change" explanation.
- Optionally verify the GitHub attestation via the asset `digest` field.

## Background updates
- Ship `harpoon-check.timer` as a systemd **user** timer. The interval comes from the
  settings, for example every 6 h.
- The timer runs `harpoon --check` headless: load apps, run `UpdateChecker`, save, and post a
  Nemo notification whose remote action opens the app.
- In-app checks run at startup and on pull-to-refresh.
- Silent background install is opt-in.

## Storage
- `~/.local/share/<org>/harpoon/apps/<id>.json`, one file per app, written atomically.
- The record format is Harpoon's own. Field names follow ObtainX's where an equivalent
  exists, which makes cross-referencing easier, but Obtainium compatibility is not a goal.
- Settings live in QSettings at `~/.config/<Org>/harpoon/harpoon.conf`, never the default
  `~/.config/<Org>/<App>.conf`. Sailjail only persists the three per-app folders, and
  following that rule keeps a sandboxed build possible.
- Tokens (GitHub PAT, GitLab token) go to Sailfish Secrets if
  available, otherwise into the settings file with 0600 permissions.
- Downloads go to `~/.cache/<org>/harpoon/downloads/<id>-<hash>.rpm.part`, which is renamed on
  completion and pruned after a successful install.

## Phased plan
1. **Core skeleton:** models, HttpClient, GitHub + Forgejo sources, ReleaseSelector,
   AssetFilter, VersionCompare, and unit tests on desktop with recorded JSON fixtures.
2. **Package layer:** RpmInspector, DeviceInfo, PackageKitBackend. Then an end-to-end CLI:
   `harpoon-cli add <url>`, `check`, `install <id>`. Validate on a device
   (`devel-su -p` for now; privileges.d once the GUI exists).
3. **Silica UI:** app list, add-app page with settings form, app details/changelog, settings,
   and notifications.
4. **More sources:** GitLab, SourceHut, SourceForge, Jenkins, HTML/Direct. Background
   timer, Harpoon backup export/import.
5. **Optional extras:** rpm-md repo source, attestation, Chum packaging.

## Open questions
1. **C++17 on the SDK targets.** The core uses `std::optional` and inline variables. The
   SailfishOS SDK compiler should be GCC 8, which supports both, but no `sfdk build` has
   confirmed it yet. If it fails, those two features are easy to replace.
2. Device tests on SailfishOS 5.x (steps in `docs/device-testing.md`):
   - Does privileged `InstallFiles` respect the "untrusted software" setting?
   - Does `devel-su -p` give the CLI the `privileged` group PackageKit expects?
   - Does the installation handler accept calls from an unsandboxed terminal process?
   - Does the `Sandboxing=Disabled` warning prompt appear (GUI phase)?

## Phase 1 status
Implemented in `core/src`, built with CMake, tests in `core/tests`:
- `model/`: `Release`, `Asset`, `AppSettings` (setting keys), `Error`/`Result`.
- `version/`:
  - `versioncompare`: the ObtainX tag comparator, without the Android-only schemes.
  - `rpmversion`: rpmvercmp and EVR comparison, checked against rpm's own test vectors.
  - `versionextractor`: regex and match-group extraction.
- `net/`: the `HttpTransport` interface, the QNetworkAccessManager transport with timeout
  and redirects, and GitHub rate-limit detection.
- `sources/`:
  - `Source` base class and `SourceRegistry`, with host matching and override for
    self-hosted forges.
  - `GitHubSource`: releases, tags fallback for track-only apps, token with retry without it
    on 401, and GitHub Enterprise via `/api/v3`.
  - `ForgejoSource`: Codeberg, Forgejo and Gitea, reusing the GitHub parser.
- `pipeline/`:
  - `DeviceInfo`: arch and OS version detection.
  - `AssetFilter`: RPM-only, arch with noarch fallback, sfos-tag preference, user regex.
  - `ReleaseSelector`: the 5 sort methods, draft/prerelease/title/notes filters, fallback.
  - `ReleasePipeline`: version source plus extraction.
- `tools/harpoon-probe`: a CLI that runs the whole pipeline against a live URL.

Not yet in Phase 1:
- `verifyLatestTag`, release-title-as-version for GitHub's commit SHA, and attestation.
- Pagination beyond 100 releases (same as upstream).

## Phase 2 status
Implemented and tested on desktop. Not yet validated on a device; see `docs/device-testing.md`.

**Package layer** (`core/src/pkg/`):
- `RpmInspector` uses `rpm -qp` / `rpm -q` behind a `ProcessRunner`, so tests can fake it.
- `PackageKitBackend` talks to PackageKit over D-Bus:
  - Install: `CreateTransaction` then `InstallFiles` (with reinstall and downgrade flags).
  - Remove: `Resolve` with the installed filter, then `RemovePackages`.
  - Error codes map to Harpoon error kinds, transactions run one at a time, and there is a
    timeout.
- `InstallHandlerBackend` uses `org.sailfishos.installationhandler`:
  - `installFiles` with `file://` URLs, answered by the `installFinished` signal.
  - `removePackages`, answered by the `removalFinished` signal.

**Downloader** (`net/downloader`):
- Streams to a `.part` file.
- Resumes with Range requests and restarts if the server answers 200, 416 or a mismatched range.
- Checks size and sha256, reuses a download that already verifies, and keeps the `.part` when
  a transfer is interrupted.

**Apps** (`core/src/app/`):
- `App` record and `AppStore`: one JSON file per app, with atomic writes and safe file names.
- `updateStatusFor`:
  - Compares the RPM VERSION (and VERSION-RELEASE) with the forge version.
  - Falls back to the install receipt for pseudo-versions such as dates.
  - Handles track-only apps.
- `AppChecker`: concurrent update checks that write the result into the record.
- `AppInstaller`, in order:
  1. Download and verify.
  2. Read the RPMs and check the arch.
  3. Pick the main package (the temporary id becomes the RPM name).
  4. Guard against id changes, downgrades and reinstalls, and warn about vendor changes.
  5. Install everything in one transaction.
  6. Confirm the result with rpm, record the receipt and clean up.

**CLI** (`cli/`):
- `harpoon-cli` with `add`, `list`, `show`, `check`, `install`, `upgrade`, `set`, `ack` and
  `remove`.

**Packaging:**
- `rpm/harpoon.spec` builds and installs `harpoon-cli`.

**Tests** cover 12 suites:
- `rpmbuild` produces real RPMs, including cross-arch ones.
- A local HTTP server plays the forge and the download host, with Range support and dropped
  connections.
- A private `dbus-daemon` hosts the mock PackageKit and installation handler.
- An end-to-end check-then-install runs through a fake rpm database.

**Licence:** GPL-3.0-or-later (decided Oct 2026). Parts of the core are translated from
ObtainX, which is GPL-3.0.

## Phase 3 status
Implemented. The C++ layer and QML are tested on desktop; the real Silica rendering is not
validated yet.

**QML-facing C++** (`gui/src/`):
- Built as `harpoon-ui`, a QtCore-only library, so it is unit-tested on desktop.
- `AppListModel`: updates first, then by name. Roles cover state, versions, busy state, stage
  and progress.
- `HarpoonController`: add (validates before saving, refuses duplicates), check / checkStale /
  checkAll, install / updateAll, uninstall, stop tracking, acknowledge, per-app settings,
  rename, `appDetails`.
- `HarpoonSettings`: stored in `<AppConfigLocation>/harpoon.conf`, mode 0600. Holds the
  install backend, the background-check toggle and interval, notifications and per-source
  tokens.
- `HarpoonDBus`: the session service `io.github.juhanilehtimaeki.harpoon` at `/harpoon`, with
  `activate`, `showUpdates` and `showApp`. It is D-Bus-activatable through
  `gui/dbus/*.service`.

**Silica UI** (`gui/qml/`):
- `AppListPage`:
  - Sections: Updates and Apps.
  - Each item shows its state line, last error or progress.
  - Context menu: install/update, mark as seen, check, stop tracking (with remorse).
  - Pulley: Settings, Add app, Update all, Check for updates.
  - A placeholder when the list is empty.
- `AddAppDialog`: checks the URL live, offers a source-type override for self-hosted servers,
  and options for prereleases, track-only and a package filter.
- `AppPage`: state, progress, an install/update/reinstall/mark-as-seen button, the latest and
  installed release, the source and plain-text release notes. Pulley: uninstall (remorse),
  release page, app settings, check now.
- `AppSettingsPage`: every per-app setting. Leaving the page re-checks the app if anything
  changed.
- `SettingsPage` (backend, background checks, interval, notifications, tokens, device) and
  `AboutPage`.
- Cover: update count or a check mark, plus a refresh action.
- Style follows the `sailfish-ui-design` checklist:
  - `Theme` constants only.
  - Labels coloured to show what is interactive.
  - At most 4 pulley items, hidden when unusable.
  - Scroll decorators, labelled text fields and configured Enter keys.
  - Remorse instead of confirmation dialogs.

**Packaging:**
- `gui/harpoon.desktop` with `[X-Sailjail] Sandboxing=Disabled`.
- `privileges.d/harpoon` containing `/usr/bin/harpoon,r`.
- Icons in 4 sizes, rendered from `gui/icons/harpoon.svg`.
- A translation catalogue, `gui/translations/harpoon.ts`.
- `rpm/harpoon.spec` ships both the GUI and `harpoon-cli`.

**Verification on desktop:**
- `tst_controller` covers the controller.
- `tst_qmlsmoke` loads every page offscreen against stand-ins for Silica and
  Nemo.Notifications (`gui/tests/silica-stub/`), with apps in every state. Any QML warning fails
  it. It already caught one real bug: invalid dates reaching `Format.formatDate`.
- `harpoon.cpp` is compiled against a stand-in `sailfishapp.h`.
- A staged install was compared with the spec's `%files`.

**Not covered by these tests:**
- Whether the real Silica components accept every property used. The stand-ins mirror Silica's
  documented API; `sfdk build` plus a device run will tell.
- Qt 5.15 prefers the `function onFoo()` form in `Connections`. The code keeps `onFoo:`
  because Qt 5.6 only understands that form.
