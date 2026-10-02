---
name: port-obtainx-source
description: Use when adding or changing a release source in Harpoon (GitHub, Codeberg/Forgejo/Gitea, GitLab, SourceHut, SourceForge, Jenkins, HTML, direct link, rpm-md repo), or when porting logic from ObtainX/Obtainium's lib/app_sources/*.dart to Harpoon's C++ core.
---

# Porting a release source from ObtainX

Upstream reference: <https://github.com/bikram-agarwal/ObtainX>, files `lib/app_sources/*.dart`
and `lib/providers/source_provider.dart`.

Research summary for every source: `docs/research/01-obtainx-release-sources.md`.
Read the section for the source you are porting first.

To read upstream code, clone it into the scratchpad and do not vendor it into this repo:
```sh
git clone --depth 1 https://github.com/bikram-agarwal/ObtainX "$SCRATCH/ObtainX"
```

## The contract in Harpoon
These are deliberate differences from ObtainX.
- A source returns **all candidate releases** (`fetchReleases`), not only the latest one.
  - Sorting, draft/prerelease filtering, title/notes regex and fallback-to-older all live in
    the shared `ReleaseSelector`.
  - Do not copy GitHub's selection loop into each source.
- A source only maps forge JSON or HTML to `Release{version, tag, title, date, changelog,
  prerelease, assets[]}` and `Asset{name, url, size, sha256}`.
  - If the API gives a sha256 for an asset (e.g. GitHub's `digest: "sha256:…"`), set `sha256`.
- Asset filtering (`.rpm`, arch, SFOS tag, user regex) belongs in `AssetFilter`, never inside
  a source.
- Version extraction (`versionExtractionRegEx` + `matchGroupToUse`) and pseudo-versions belong
  in `VersionExtractor`.

## Porting checklist
1. **Identity**
   - `id()` is stored per app as `overrideSource`, so never change it once released. Use
     ObtainX's class name where one exists (`GitHub`, `GitLab`, `Forgejo`, `SourceHut`,
     `SourceForge`, `Jenkins`, `HTML`), and `DirectLink` for `DirectAPKLink`.
     Codeberg is served by `Forgejo`.
   - `hosts()`: the same list as upstream.
   - Copy upstream's flags `allowSubDomains` and `neverAutoSelect`.
2. **URL standardization**
   - Port the regex exactly; the research doc lists them.
   - Throw `InvalidUrl` when the URL doesn't match.
   - When the source runs against another host via override (`hostChanged`), skip the
     host-specific regex, as upstream does.
3. **Endpoints**
   - Use the same paths and query params as upstream (`per_page=100` and so on).
   - Pagination: upstream reads only page 1. That is fine for v1, but put a `// TODO paginate`
     where it matters.
4. **Auth**
   - Tokens are global per-source config, not per-app:
     - GitHub: `Authorization: Bearer`, and retry without the token on a 401.
     - GitLab: the `private_token` query parameter, also appended to asset URLs.
     - Codeberg/Forgejo: none upstream; adding an optional `Authorization: token …` is fine.
   - Never log tokens, and never put them in exported JSON unless the user explicitly chooses
     to export secrets.
5. **Rate limits**
   - Port GitHub's `rateLimitErrorCheck`: `x-ratelimit-remaining == 0`, 403/429, or body text.
   - Read the reset time from `x-ratelimit-reset`, then `retry-after`, otherwise use 30 min.
   - Raise `RateLimitError(minutes)` so `UpdateChecker` can back off.
6. **Settings**
   - Declare keys in `core/src/model/appsettings.h` (`Harpoon::Keys`).
   - Reuse upstream's key names where the meaning is the same (`includePrereleases`,
     `fallbackToOlderReleases`, `filterReleaseTitlesByRegEx`, `sortMethodChoice`, ...).
     That keeps the code easy to compare with ObtainX.
   - Obtainium backup import is out of scope, so there is no compatibility constraint
     beyond this.
7. **Android leftovers to drop or replace**
   - APK extension checks: assets are any files here; `AssetFilter` keeps the `.rpm` ones.
   - Android ABIs.
   - Gradle app-id inference: on Sailfish the app id is the RPM `%{NAME}`, read after download.
   - The Android Chrome User-Agent: use `Harpoon/<version> (SailfishOS)`.
   - versionCode, F-Droid reproducible builds, VirusTotal.
8. **Tests** (required for every source)
   - Record real API responses as JSON fixtures under `core/tests/fixtures/<source>/`. Strip
     tokens from them.
   - Test URL standardization: valid, invalid and override-host cases.
   - Test the mapping from fixture to `QList<Release>`: version, date, changelog, prerelease
     flag and asset URLs.
   - Include at least one fixture with RPM assets for several arches, e.g. `aarch64`,
     `armv7hl` and `noarch`.
9. **Register** the source in `SourceRegistry` in the same order as upstream: alphabetical
   host-based sources, then `DirectLink`, then `HTML` last.

## Source-specific notes
- **Codeberg/Forgejo/Gitea**
  - Reuse the GitHub JSON mapping. The API base is `{origin}/api/v1/repos/{owner}/{repo}`.
  - Assets have no API `url` field, so use `browser_download_url`.
  - Release date: use `created_at` when `updated_at` is missing.
- **GitLab**
  - URL-encode the project path, including subgroups (`group%2Fsub%2Fproject`).
  - Assets come from `assets.links[].direct_asset_url ?? url`, plus `/uploads/` markdown
    links in the description.
  - Rewrite `/-/jobs/N/artifacts/file/` to `/raw/`.
- **SourceHut**
  - There is no API. Read `{repo}/refs/rss.xml`, then scrape each ref page for asset links.
- **HTML**
  - Port the intermediate-link chain (at most 10 hops) and the version-aware vs
    natural-alphanumeric sort.
  - Port pseudo-versioning: ETag, link hash, or partial sha256 via an HTTP Range request.
- **rpm-md repo** (Harpoon-only)
  - Read `repodata/repomd.xml`, then `primary.xml(.gz)`.
  - Find the package by name and use the highest EVR for the device arch or `noarch`.
  - This is the analogue of ObtainX's `fdroidrepo.dart`.
