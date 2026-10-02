# Release sources

Each source maps one kind of release host to `Release{tag, title, date, changelog,
prerelease, assets[]}`. Selection, asset filtering (`.rpm`, arch, SFOS tag, user regex)
and version extraction happen afterwards in the shared pipeline
(`core/src/pipeline/`), the same for every source. The upstream behaviour is
summarised in `docs/research/01-obtainx-release-sources.md`.

## Matching

`SourceRegistry` walks the sources in this order:

| # | id (`overrideSource`) | Display name | Hosts | Auto-selected |
|---|---|---|---|---|
| 1 | `Forgejo` | Codeberg / Forgejo / Gitea | codeberg.org, codefloe.com | by host |
| 2 | `GitHub` | GitHub | github.com | by host |
| 3 | `GitLab` | GitLab | gitlab.com | by host |
| 4 | `Jenkins` | Jenkins | none | never |
| 5 | `RpmMdRepo` | RPM repository (rpm-md) | none | never |
| 6 | `SourceForge` | SourceForge | sourceforge.net | by host |
| 7 | `SourceHut` | SourceHut | git.sr.ht | by host |
| 8 | `DirectLink` | Direct RPM link | none | URL path ends in `.rpm` |
| 9 | `HTML` | HTML | none | any http(s) URL, always last |

1. A host-based source whose host matches the URL's host (with or without `www.`) wins.
2. Otherwise the hostless sources that are not `neverAutoSelect` are tried in order:
   `DirectLink`, then `HTML`.
3. An override id forces any source onto any host. Self-hosted forges, Jenkins and
   rpm-md repositories are added this way. A forced source skips its host check but
   still checks the URL's shape.

The ids are stored per app, so never rename one.

Global per-source configuration (`Source::setConfig`) currently has one key, `token`.

## GitHub, Forgejo

See `githubsource.h` and `forgejosource.h`.

## GitLab (`GitLab`)

| | |
|---|---|
| URL forms | `https://gitlab.com/{group}/{subgroups…}/{project}`; anything after `/-/` is cut, `.git` is dropped |
| Requests | `GET {origin}/api/v4/projects/{group%2Fsub%2Fproject}` (project id), then `…/releases?per_page=100`, or `…/repository/tags?per_page=100` when `trackOnly` |
| Auth | config `token`, sent as the `private_token` query parameter; every request sends `Referer: {origin}` |
| Tag / title | `tag_name` / `name` (tags: `name`) |
| Date | `released_at` ?? `created_at` ?? `commit.created_at` |
| Changelog | `description` ?? `release.description` ?? `message` ?? `commit.message` |
| Prerelease | `upcoming_release` (Harpoon-only; upstream ignores it) |
| Assets | `assets.links[]`: `direct_asset_url` ?? `url`. Markdown `](/uploads/….rpm)` in the description becomes `{origin}/-/project/{id}/uploads/…`. `{project}/-/jobs/N/artifacts/file/X` is rewritten to `…/artifacts/raw/X` |

Stored asset URLs do not contain the token, because assets are saved with the app and
can be exported. To download private assets, call `GitLabSource::authorizedAssetUrl()`
at download time. If a link's name is not an RPM file name but its URL is, the URL's
file name becomes the asset name.

## SourceHut (`SourceHut`)

| | |
|---|---|
| URL forms | `https://git.sr.ht/~{owner}/{repo}` (anything after is cut, e.g. `/refs`) |
| Requests | `GET {repo}/refs/rss.xml`, then each item's ref page (`<guid>`), at most 6 |
| Tag / date | item `<title>` / `<pubDate>` |
| Assets | `<a href>` links on the ref page whose path ends in `.rpm`, made absolute |

- Items whose guid is not under `{repo}/refs` are skipped.
- With `fallbackToOlderReleases` off, only the newest ref page is fetched.
- A ref page that returns 404 gives a release with no assets, as upstream.
- A network error or 5xx on a ref page fails the check, so the selector does not
  fall back to an older ref just because the newest page failed to load.

## SourceForge (`SourceForge`)

| | |
|---|---|
| URL forms | `/p/{name}/…` → `/projects/{name}/…`; `/projects/{name}` → `/projects/{name}/files`; canonical `https://sourceforge.net/projects/{name}/files[/{subfolder}]` (query, fragment and trailing `/` dropped) |
| Request | `GET {origin}/projects/{name}/rss?path=/` |
| Items kept | `<guid>` under the canonical URL, ending in `/download` after a `.rpm` file name (scheme and `www.` are ignored when comparing) |
| Tag | the folder that holds the file, relative to the canonical URL (`v1.2.0`, `stable/1.2`); a top-level file uses its own name, as upstream |
| Date / size | newest `<pubDate>` in the group / `<media:content filesize>` |

**Version handling.** Upstream turns off the global version extraction for
SourceForge. Instead it applies `versionExtractionRegEx` to each folder, drops files
whose folder doesn't match, and groups files by the extracted version. Harpoon does
the same filtering and grouping in the source, but keeps the raw folder in
`Release.tag`, so `ReleasePipeline` applies the regex exactly once. Each version group
becomes its own release, newest first, so `fallbackToOlderReleases` works. Upstream
returns only the newest group.

## Jenkins (`Jenkins`, override only)

| | |
|---|---|
| URL forms | `https://ci.example.org[/prefix]/job/{name}[/job/{name}…]`; anything after the last job is cut |
| Request | `GET {job}/lastSuccessfulBuild/api/json` |
| Tag / title / date | `number` / `fullDisplayName` / `timestamp` (ms) |
| Changelog | `changeSets[].items[].msg` (or `changeSet`), as a list |
| Assets | `artifacts[]`: `fileName` at `{job}/{number}/artifact/{relativePath}` |

Differences from upstream:
- Jobs in folders and Jenkins served under a path prefix are accepted.
- Artifact URLs are pinned to the build number rather than `lastSuccessfulBuild`.

## HTML (`HTML`, catch-all)

Settings (per app, `Harpoon::Keys`; names as in ObtainX):

| Key | Meaning |
|---|---|
| `intermediateLink` | list of up to 10 maps; each hop loads the page, applies the link options below with its own values, and follows the **last** link. Hops without `customLinkFilterRegex` are ignored |
| `customLinkFilterRegex` | keep links matching this (against the decoded URL). Default: installable `.rpm` links (no `.src.rpm`, no `-debuginfo`/`-debugsource`) |
| `filterByLinkText` | match the link text instead of the URL |
| `matchLinksOutsideATags` | use bare URLs from JSON strings or the page text (also used automatically when the page has no `<a href>`) |
| `skipSort`, `reverseSort`, `sortByLastLinkSegment` | sorting. The default is ascending, by version when every key has a comparable version, otherwise natural alphanumeric. The newest link is last |
| `versionExtractWholePage` | extract the version from the page text instead of the link |
| `requestHeader` | `"Name: value"` lines, or ObtainX's list of `{"requestHeader": "…"}` maps; sent with every page request and the ETag probe |
| `defaultPseudoVersioningMethod` | `ETag` (default) or `linkHash` |

The `assetFilterRegEx`/`invertAssetFilter` filter is applied to the final links before
the last one is chosen, as upstream's `filterApks` does. `AssetFilter` applies it again
later, which has no further effect.

**Output: one release.** Its assets are the selected link plus the links of the same
version, so that `AssetFilter` can pick the device's arch:
- With `versionExtractionRegEx`, these are the links whose decoded URL extracts to the
  same version.
- Otherwise, these are the links that differ from the selected one only by an arch
  token (`.aarch64.rpm` vs `.armv7hl.rpm`).

Upstream returns only the single selected link. A link whose file name isn't `*.rpm`
(e.g. `download.php?id=7`) gets `.rpm` appended to the asset name, so a link chosen on
purpose is not filtered out. Its RPM header is checked after download.

**Version.** `ReleasePipeline` applies `versionExtractionRegEx` to `Release.tag`, so
the source never extracts the version itself:
- **With a regex:** the tag is the decoded selected URL. With `versionExtractWholePage`,
  it is the page text with newlines escaped as `\n` (as upstream). That page text is
  shortened to the regex's own match when re-extracting from the match gives the same
  version.
- **Without a regex:** the tag is a pseudo-version.
  - `ETag`: the first 12 hex digits of sha256 of the selected link's ETag, quotes
    removed. The ETag is read from a `GET` with `Range: bytes=0-0`. If the response
    has no ETag, the link hash is used.
  - `linkHash`: the first 12 hex digits of sha256 of the link URL. No request is made.
  - Upstream's partial-download hash (`partialAPKHash`) is not ported.

No Android Chrome User-Agent is sent. The transport's `Harpoon/<version> (SailfishOS)`
is used unless `requestHeader` sets one. `autoLinkFilterByArch` for intermediate hops is
not ported.

## Direct RPM link (`DirectLink`)

- It is auto-selected when the URL's path ends in `.rpm`; a query string is allowed.
  Upstream requires `.apk` at the very end of the URL.
- When forced via override, any URL works.
- The release has one asset: the URL itself.
- The version is a pseudo-version, exactly as for HTML (ETag, falling back to the
  link hash). With `versionExtractionRegEx`, the tag is the decoded URL instead.
- `requestHeader` applies to the ETag probe.

## rpm-md repository (`RpmMdRepo`, override only, Harpoon-only)

| | |
|---|---|
| URL forms | the repository base, i.e. the directory containing `repodata/`. A trailing `/repodata/`, `/repodata/repomd.xml` or `/` is removed. Only the `package` query parameter is kept: `https://repo.example.org/sailfishos_5.0_aarch64?package=harbour-foo` |
| Settings | `packageName` (required unless the URL has `?package=`; the setting wins) |
| Requests | `GET {base}/repodata/repomd.xml`, then the `<data type="primary">` location |
| Compression | gzip (zlib, `16 + MAX_WBITS`) or plain XML. zstd, xz and bzip2 are reported as unsupported |
| Integrity | the primary file's sha256 must match `<checksum>`, or `<open-checksum>` when the server sent it pre-inflated via Content-Encoding |
| Assets | every `<package type="rpm">` with that `<name>`, except `src`/`nosrc`: `location href`, resolved against `xml:base` or the base URL; `size package`; `checksum type="sha256"` → `Asset.sha256`; `time file` → `Asset.updatedAt` |
| Releases | one per EVR, tag `ver-rel` or `epoch:ver-rel`, newest EVR first |

**Ordering.** Releases are ordered by rpm EVR (`compareEvr`). `Release.date` is the
newest file time of that EVR, raised where needed so that dates increase strictly with
EVR. The default `date` sort and `none` therefore both keep EVR order, and so does the
selector's fallback to older releases. A shown date can be a few seconds later than the
real file time when an older EVR was rebuilt after a newer one.

Change logs (`other.xml`) are not read yet.
