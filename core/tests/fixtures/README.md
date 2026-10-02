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

When you add a fixture recorded from a live API, strip any tokens first.
