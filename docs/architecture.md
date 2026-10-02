# Harpoon: draft architecture

Harpoon is an Obtainium-style updater for SailfishOS. It tracks apps published as RPM
release assets on code forges, then downloads and installs them.

Status: **draft for discussion**. It builds on the research in `docs/research/01-03`.

## Goals (v1)
- Add an app by pasting a forge URL: GitHub, Codeberg/Forgejo/Gitea, GitLab, SourceHut,
  SourceForge, Jenkins, a generic HTML page, or a direct `.rpm` link.
- Pick the correct RPM asset for the device: arch, plus an optional SFOS-version tag.
- Show installed vs. latest version and the changelog. Install, update and uninstall.
- Check for updates in the background and post notifications.
- Import apps from Obtainium/ObtainX JSON exports where the source is a forge.
- Export and back up in a compatible schema.

## Non-goals (v1)
- Android APKs via AppSupport. Possible later through `com.jolla.apkd`.
- Managing ssu repositories. Chum and Storeman already do that.
- Harbour distribution. Silent installs need privileges that Harbour forbids.

## Stack decision (proposed)
- **C++17 / Qt 5.6 core library + QML/Silica UI.** This is the native SFOS stack.
  - QtNetwork, QtDBus, Nemo.Notifications and Nemo.KeepAlive are all first-class.
  - It is the same stack as Storeman and Chum GUI, so their code is reference material.
  - Flutter on SFOS is experimental and aarch64-only (see research 03).
- The ObtainX Dart code is **ported, not reused**. Its algorithms are small and well defined.
- **Core library rules:** no Silica dependency, and it builds on desktop Linux. Unit tests
  run in CI against recorded API responses.

## Module layout

```text
harpoon/
  core/                       # libharpoon-core (Qt Core/Network/DBus only)
    model/        App, Release, Asset, InstalledPackage, AppSettings (JSON <-> Obtainium schema)
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
- `~/.local/share/<org>/harpoon/apps/<id>.json`, one file per app, written atomically. The
  field names follow Obtainium/ObtainX: `id, url, author, name, installedVersion,
  latestVersion, apkUrls (assets), additionalSettings, overrideSource, ...`. This keeps
  import and export compatible.
- Settings live in QSettings. Tokens (GitHub PAT, GitLab token) go to Sailfish Secrets if
  available, otherwise into the settings file with 0600 permissions.
- Downloads go to `~/.cache/<org>/harpoon/downloads/<id>-<hash>.rpm.part`, which is renamed on
  completion and pruned after a successful install.

## Phased plan
1. **Core skeleton:** models, HttpClient, GitHub + Forgejo sources, ReleaseSelector,
   AssetFilter, VersionCompare, and unit tests on desktop with recorded JSON fixtures.
2. **Package layer:** RpmInspector, DeviceInfo, PackageKitBackend. Then an end-to-end CLI:
   `harpoon add <url>`, `harpoon check`, `harpoon install <id>`. Validate on a device
   (privileges.d + PackageKit).
3. **Silica UI:** app list, add-app page with settings form, app details/changelog, settings,
   and notifications.
4. **More sources:** GitLab, SourceHut, SourceForge, Jenkins, HTML/Direct. Background timer,
   Obtainium import/export.
5. **Optional extras:** rpm-md repo source, InstallHandler fallback, attestation, Chum packaging.

## Open questions
1. **Language:** C++/Qt as proposed, or Python + PyOtherSide for faster iteration on the
   logic layer?
2. **Distribution:** Chum (recommended) vs OpenRepos. Is Harbour compatibility (handler-only
   mode) wanted at all?
3. **Obtainium import scope:** forge-based apps only? Store-based entries (Play, APKMirror)
   are meaningless here.
4. **APK support via AppSupport:** in or out of scope for v1?
5. **Device tests:** we need answers on 5.x for whether privileged `InstallFiles` respects
   "untrusted software", and whether the `Sandboxing=Disabled` warning prompt is present.
