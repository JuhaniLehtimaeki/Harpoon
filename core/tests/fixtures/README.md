# Test fixtures

API responses in the shape returned by each forge. They are trimmed to the fields
Harpoon reads, and some are synthesised from the real schema, so tests run offline
and stay deterministic.

- `github/chum-gui-releases.json`: GitHub `/releases`, modelled on
  sailfishos-chum/sailfishos-chum-gui. It contains RPMs for aarch64, armv7hl and i486,
  a debuginfo and a src.rpm, a draft and a prerelease.
- `github/sfos-tagged-noarch.json`: one release with `_sfosX.Y` tagged noarch RPMs
  and an APK.
- `github/tags.json`: GitHub `/tags`.
- `forgejo/releases.json`: Forgejo `/api/v1/repos/{o}/{r}/releases`. Assets have no
  API `url` and only `created_at`.
- `gitlab/project.json`, `gitlab/releases.json`, `gitlab/tags.json`: GitLab API v4
  project, `/releases` and `/repository/tags`. The releases cover an `upcoming_release`,
  `direct_asset_url` vs `url`, a CI job artifact link, a non-RPM link name, a markdown
  `/uploads/` RPM (and a screenshot that must be ignored), and a release with only
  `created_at`.
- `sourcehut/rss.xml`, `sourcehut/ref-v1.2.0.html`: a git.sr.ht refs feed (with an item
  from another repository) and one ref page with aarch64/armv7hl/noarch RPM links,
  a tarball, a signature and a commented-out link.
- `sourceforge/rss.xml`: a SourceForge `/rss?path=/` feed with version folders, a
  "nightly" folder, a top-level file, a README, a src.rpm, `media:content filesize`
  and an `%20` folder name.
- `jenkins/build.json`: `lastSuccessfulBuild/api/json` of a pipeline job in a folder,
  with RPM artifacts, a log with a space in its path and change set messages.
- `html/index.html`: an Apache-style directory listing (natural sort 1.2 < 1.9 < 1.10,
  src/debuginfo RPMs, an uppercase `<A HREF='...'>`, a comment).
  `html/releases-root.html` and `html/releases-1.1.html`: an intermediate-link chain.
  `html/api.json`: a JSON body for link matching outside `<a>` tags.
- `rpmmd/repomd.xml`, `rpmmd/primary.xml`, `rpmmd/primary.xml.gz`: an rpm-md
  repository. `primary.xml.gz` is `gzip -9n primary.xml`; its sha256 is the primary
  checksum in `repomd.xml`, so regenerate both together. The packages cover several
  arches, a src.rpm, an epoch, an `xml:base` location, a sha1-only checksum, another
  package name, and an EVR whose file time is older than a lower EVR's.

When you add a fixture recorded from a live API, strip any tokens first.
