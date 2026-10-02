# ObtainX: app lifecycle outside the sources

Paths are relative to the ObtainX repo's `lib/`.

## Data model and persistence
- **`App` records:** one JSON file per tracked listing at `app_data/<listingKey>.json`.
  - Writes are atomic: write a temp file, then rename.
  - Writes are serialized through a queue.
  - A sqflite DB caches only the last-check timestamps. The JSON files stay authoritative.
- **Settings:** stored in SharedPreferences.

`App` JSON, which Obtainium shares and is the format we want to import:

```text
id, url, author, name, installedVersion|null, latestVersion,
apkUrls:        JSON-encoded STRING of [[name,url],...]
otherAssetUrls: JSON-encoded STRING of [[name,url],...]
preferredApkIndex: int,
additionalSettings: JSON-encoded STRING of {...per-app settings...},
lastUpdateCheck: int µs-since-epoch|null, pinned: bool, categories: [String],
releaseDate: int µs|null, changeLog, overrideSource, allowIdChange, pendingRepoRenameUrl
(+ ObtainX-only optional fields: listingId, iconUrl, apkSizeBytes, ...)
```

**Export format:**
- Shape: `{schemaVersion: 2, exportedAt, appVersion, apps: [...], settings: {...}}`.
- Import also accepts Obtainium's `{apps, settings}` and a bare `[apps]` list.
- `App.fromJson` contains a chain of legacy migrations that we can mostly ignore.

**Common per-app settings keys:**
- Tracking: `trackOnly`, `onDemandOnly`, `exemptFromBackgroundUpdates`, `skipUpdateNotifications`.
- Version: `versionExtractionRegEx`, `matchGroupToUse`, `versionDetection` (`auto|standard|pseudo|versionCode`).
- Assets: `apkFilterRegEx`, `invertAPKFilter`, `autoApkFilterByArch`.
- Network: `allowInsecure`, `refreshBeforeDownload`.
- Source-specific keys, e.g. GitHub: `includePrereleases`, `fallbackToOlderReleases`,
  `filterReleaseTitlesByRegEx`, `sortMethodChoice`, `verifyLatestTag`.

## Update check flow
1. A scheduler triggers the check: WorkManager, or a foreground service. The interval is at
   least 15 min (default 360 min), with Wi-Fi-only and charging-only options.
2. Choose which apps to check: those whose `lastUpdateCheck` is older than the interval and
   that are not `onDemandOnly`, oldest first.
3. Run the checks in a worker pool.
4. For each app, `SourceProvider.getApp` runs `source.getLatestAPKDetails` plus the shared
   post-processing.
5. Merge the result into the live record, overwriting only the fields the source owns.
6. Decide whether there is an update with `versionDecisionForApp`, which tries in order:
   1. Compare version codes (in versionCode mode).
   2. `compareVersionStrings`:
      - Handles a `v` prefix, dates and hex hashes.
      - Splits numeric core / prerelease / build parts.
      - Prerelease rank: dev < snapshot < nightly < alpha < beta < pre < preview < rc < final.
   3. Use the install receipt (`confirmedInstallRelease`) and pseudo-version tracking.
7. Notify, with separate notifications for updates, track-only and needs-review. Optionally
   install silently.
8. On failure, retry up to 4 times, honouring rate-limit reset times.

**Installed version:** read from the Android PackageManager (`versionName` / `versionCode`).
On load, records are reconciled with the real installed state: package removed, version
changed outside the app, and so on.

**trackOnly:** no package is needed. "Update" just acknowledges the new release.
**Pseudo mode:** for sources without real versions, the installed "version" is the release
that was last installed (the install receipt).

## Download
- Download file names are deterministic: `<id>-<sha256(url,version,asset...)>.<ext>`.
- Downloads go to a `.part` file. With `Accept-Ranges` they resume via a Range request.
  Redirects are followed manually (up to 10, with cookies). Each download gets 3 retries
  5 s apart.
- A complete file of the right length is reused.
- Optional checks before install: GitHub attestation (sha256 digest), F-Droid reproducible
  builds, VirusTotal.

## Install
- **Installer strategies** (selected by the `installMethod` setting): Android session
  installer, Shizuku, Dhizuku, or an external installer app via intent.
- **When several assets survive the arch filter:** the user picks one, so no silent install.
- **Package-id check:** the downloaded package is parsed for its id.
  - A temp id adopts the real id.
  - A mismatch throws `IDChangedError` unless `allowIdChange` is set.
- **Downgrades** are refused by default.
- **Install receipts:** a pending receipt is written before handing off to the installer and
  promoted to confirmed on success.
- **Containers:** zip/xapk/tar archives are extracted, and the package matching the app id is
  preferred.

## What is portable
**Pure logic, reusable as a design:**
- The models and JSON (de)serialization.
- Version comparison and extraction.
- Filtering, the HTTP download with resume, and pseudo-versioning.
- The update-check orchestration and backoff.
- The import/export schema.

**Android-specific:**
- The package manager queries and all the installers.
- ABI detection, WorkManager and foreground services.
- SAF storage, the notification plugin and wake locks.
