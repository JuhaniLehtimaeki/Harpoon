# ObtainX: how releases are loaded from forges

Reference: <https://github.com/bikram-agarwal/ObtainX> (Flutter/Dart fork of Obtainium, v2.20.0).
Paths below are relative to that repo's `lib/`.

## 1. The core contract

Everything revolves around one abstract class, `AppSource`, and one result type, `APKDetails`
(`providers/source_provider.dart`).

```text
AppSource
  hosts: [String]                  # e.g. ['github.com']; empty = matched by URL shape
  allowSubDomains, neverAutoSelect, enforceTrackOnly, canSearch, ...flags
  sourceSpecificStandardizeURL(url)          -> canonical URL or throws InvalidURLError
  getLatestAPKDetails(standardUrl, settings) -> APKDetails
  getRequestHeaders(settings, url, forAPKDownload)   # auth, UA, Accept
  generalReqPrefetchModifier / assetUrlPrefetchModifier  # proxy, token-in-query
  additionalSourceAppSpecificSettingFormItems  # per-app options (declarative forms)
  sourceConfigSettingFormItems                 # global per-source options (tokens)
  search(query), tryInferringAppId(url)

APKDetails
  version, apkUrls: [(name, url)], names: (author, name)
  releaseDate?, changeLog?, allAssetUrls, iconUrl?, releaseTitle?, apkSizeBytes?
  versionCode?, attestation / reproducible-build status
```

**Source resolution** (`SourceProvider._matchSourceIndexForStandardizedUrl`):
1. Walk an ordered list of source factories and match the URL host against
   `^(www\.)?(hosts)$`, or `^([^.]+\.)*(hosts)$` when `allowSubDomains` is set.
2. If no host matches, try the hostless sources by calling their standardizer.
   `DirectAPKLink` (only `*.apk`) is tried first and `HTML` is the universal fallback.
3. **overrideSource** lets the user force any source onto any host. The source's `hosts`
   becomes the URL's host and its URL regex is skipped. This is how self-hosted GitLab,
   Gitea and Forgejo instances work, with no per-instance code.

**Shared post-processing** (`SourceProvider.getApp`), applied after every source:
1. If `versionExtractionRegEx` and `matchGroupToUse` are set, trim the version string.
   If the regex doesn't match, it throws `NoVersionError`.
2. Optional pseudo-versions: the release date (ISO-8601), the release title, the asset name
   or the commit SHA.
3. Filter assets with `apkFilterRegEx` (optionally inverted).
4. `filterApksByArch`: for each device ABI in preference order, keep assets whose name
   matches `.*<abi>.*`. Accept the result only if it is a strict, non-empty subset.
5. `preferredApkIndex`: the last asset for a new app; otherwise keep the previous index.
6. Resolve the app id: explicit, inferred from the repo (Gradle files), or a temporary hash
   that is replaced by the real id after the first download.

## 2. Per-forge details

### GitHub (`app_sources/github.dart`)

**URLs and endpoints**
- Canonical URL: `https://github.com/{owner}/{repo}`. API base: `https://api.github.com/repos/{owner}/{repo}`.
- `GET /releases?per_page=100`. Only the first page is read; there is no further pagination.
- `GET /tags?per_page=100` is a fallback when there are no releases and the app is track-only.
- `GET /releases/latest` (option `verifyLatestTag`) pins the "Latest" badge release to the top.
- `GET /git/ref/tags/{tag}`, then `/git/tags/{sha}` when the commit SHA is used as the version.
- `GET /attestations/sha256:{digest}`, using the asset's `digest` field. Checks build provenance; needs a token.

**Auth, proxy and rate limits**
- Auth: an optional PAT sent as `Authorization: Bearer`. On a 401 the request is retried
  without the token.
- Asset downloads use the API `url` with `Accept: application/octet-stream`. Non-package
  assets use `browser_download_url`.
- Proxy: an optional `GHReqPrefix` rewrites URLs to `https://{prefix}/{url}`.
- Rate limits: detected by `x-ratelimit-remaining == 0`, a 403 or 429 status, or the body
  text. The reset time is read from `x-ratelimit-reset` or `retry-after`, otherwise 30 min.
  The result is a `RateLimitError(minutes)`.

**Release selection**
- Sort by `date` | `smartname` | `smartname-datefallback` | `name` | `none`.
- Always skip drafts. Skip prereleases unless `includePrereleases`.
- `filterReleaseTitlesByRegEx` and `filterReleaseNotesByRegEx` filter releases by title and notes.
- With `fallbackToOlderReleases`, walk down the list until a release has a matching asset.
  Otherwise only the newest release is considered.

**Field mapping**
- Version: `tag_name ?? name`.
- Date: `published_at`, or optionally the newest asset's `updated_at`.
- Changelog: `body`. Size: `asset.size`.
- The source tarball and zipball are added as extra assets.

### Codeberg / Forgejo / Gitea (`app_sources/codeberg.dart`)
- It reuses GitHub's code. `Codeberg` holds a `GitHub(hostChanged: true)` and swaps only the
  URL builder to `{origin}/api/v1/repos/{owner}/{repo}/releases?per_page=100`. The Forgejo
  and Gitea release JSON is close enough to GitHub's that sorting, filters and fallback all
  apply unchanged.
- Asset URLs come from `browser_download_url`. No auth header is sent and there is no
  rate-limit handling.
- `Forgejo extends Codeberg` covers other instances. Any Gitea or Forgejo host works via
  overrideSource.
- Search uses `/api/v1/repos/search?q=`.

### GitLab (`app_sources/gitlab.dart`)
- Canonical URL: `https://gitlab.com/{group}/{subgroups...}/{project}`, cut at `/-/`.
- Requests:
  1. `GET /api/v4/projects/{url-encoded path}` returns the numeric id.
  2. `GET /api/v4/projects/{path}/releases?per_page=100`, or `/repository/tags` when track-only.
- Token: sent as the `private_token=` query parameter, also on asset URLs. Requests also send
  `Referer` to get past Cloudflare.
- Assets:
  - `assets.links[].direct_asset_url ?? url`.
  - Markdown `](/uploads/...)` links scraped from the release description.
  - CI artifact `/-/jobs/N/artifacts/file/` links rewritten to `/raw/`.
- Field mapping:
  - Version: `tag_name`.
  - Date: `released_at ?? created_at`.
  - Changelog: `description`.
  - No sorting (API order is trusted) and no prerelease handling.

### SourceHut (`app_sources/sourcehut.dart`)
- No API is used.
- `GET {repo}/refs/rss.xml` and take the first 6 items. The version is `<title>` and the date
  is `<pubDate>`.
- Then fetch each item's ref page and scrape the `<a href>` links that point to packages.

### SourceForge (`app_sources/sourceforge.dart`)
- `GET /projects/{name}/rss?path=/` and keep `<guid>` URLs ending in `/download` that point to
  a package.
- The version is derived from the folder path. The newest item wins, and every file in the
  same version folder becomes an asset.

### Jenkins (`app_sources/jenkins.dart`)
- Must be chosen manually.
- `GET {job}/lastSuccessfulBuild/api/json`. The version is the build `number`.
- Assets are `artifacts[]`, downloaded from `.../artifact/{relativePath}`.

### Generic HTML (`app_sources/html.dart`)

The universal scraper.
- **Link collection:** collect `<a href>` links. If there are none, or `matchLinksOutsideATags`
  is set, regex URLs out of the JSON or raw body instead.
- **Filtering:** keep links matching `customLinkFilterRegex`, or the default package-extension
  test. Optionally match on the link text instead of the URL.
- **Sorting:**
  - Use version-aware sort when the link keys have a consistent version order, otherwise
    natural alphanumeric sort.
  - Options: `skipSort`, `reverseSort` and `sortByLastLinkSegment`.
- **Intermediate links:** up to 10 hops. Each hop has its own filter regex, and the last
  link of each hop is followed.
- **Version:**
  - Taken from the selected link, or from the whole page with `versionExtractWholePage`.
  - If extraction finds nothing, a pseudo-version is used: the ETag, a hash of the link, or a
    sha256 of the first 128–1024 bytes fetched with an HTTP Range request.
- **Custom headers:** `requestHeader`. The default User-Agent is Android Chrome, which we
  must change.

### Direct link (`app_sources/direct_apk_link.dart`)
- `HTML` with a fixed URL and pseudo-versioning (ETag or partial hash).

### F-Droid / IzzyOnDroid / third-party F-Droid repos
These are Android-only, but useful as a pattern for **repository-index sources**:
- `fdroid.dart` uses `https://f-droid.org/api/v1/packages/{id}`, which returns
  `suggestedVersionCode` and `packages[]`.
- `fdroidrepo.dart` and `izzyondroid.dart` parse the legacy `index.xml`. They try
  `{url}/index.xml`, `{base}/repo/index.xml` and `{base}/fdroid/repo/index.xml`, then pick the
  `marketvercode` version and filter by `nativecode` ABIs.
- **Our equivalent** would be an rpm-md repo source that reads `repodata/repomd.xml` and then
  `primary.xml.gz`, for OBS / Chum / OpenRepos-style repos. Here the "source" is a repo URL
  plus a package name.

### Bulk import: GitHub Stars (`app_sources/githubstars.dart`)
- `GET /users/{user}/starred?per_page=100&page=N` until a short page comes back.

## 3. What ports to SailfishOS and what doesn't

| Piece | Verdict |
|---|---|
| Forge API logic (GitHub, Forgejo/Gitea, GitLab, SourceHut, SourceForge, Jenkins, HTML, Direct) | **Port almost 1:1** |
| Release sort, filter and fallback algorithm; version extraction; pseudo-versions | **Port 1:1** |
| Version comparison (`version/version_comparison.dart`): prerelease ranks, dates, `v`-prefix | **Port** (forge tags are not RPM EVRs) |
| Rate-limit and auth handling, override-source mechanism | **Port** |
| Declarative per-source settings forms | **Port the idea**: a settings schema rendered as Silica forms |
| `.apk/.xapk/.apks` extension predicate | **Replace** with `.rpm` (optionally `.tar.gz` containing an rpm) |
| ABI filter (`supportedAbis`) | **Replace** with RPM arch: `aarch64`, `armv7hl`, `i486`, plus `noarch` fallback |
| App id = Android package name, Gradle inference | **Replace**: id = RPM `%{NAME}`, read with `rpm -qp` after the first download |
| F-Droid / Izzy / APKMirror / store scrapers | **Drop** (optionally replace with an rpm-md repo source) |
| versionCode, reproducible builds, VirusTotal | **Drop for v1**. Keep GitHub attestation as an option. |
