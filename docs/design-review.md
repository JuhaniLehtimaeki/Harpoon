# Design review (October 2026)

Every page was checked against the Sailfish UI checklist (the eleven pitfalls and Jolla's
Definition of Done, in `.claude/skills/sailfish-ui-design`). What was found and changed:

| Where | Finding | Change |
|---|---|---|
| Cover | Showed a large "✓" glyph when everything was up to date; did not say *which* apps had updates. | Shows the number of updates and the names of up to four apps with updates; with no updates, the number of apps and "all up to date". Keeps the refresh action. |
| App list | Rows had no icons, so apps were hard to tell apart at a glance. | Installed apps show their own launcher icon; others (and icons that fail to load) show the theme's package icon, coloured for the pressed state. |
| App page | Up to three stacked buttons: "Reinstall" competed with the primary action on every up-to-date app. | "Reinstall" and "Open release page" moved to a push-up menu at the end of the page. The pulley has Uninstall, Share as QR code, App settings and Check now (four items). Only Install, Update or Mark as seen remain as buttons. |
| App page | Errors used `InfoLabel`, which is meant for empty pages and is large and centred. | A wrapped `Label` in the error colour with page margins. |
| Settings | Nine token fields, one per source, though only GitHub, Forgejo and GitLab use tokens; no way to set a token for a self-hosted server. | Fields only for sources that use tokens, plus one per self-hosted server among the tracked apps (e.g. "Forgejo (git.example.org) token"). |
| Settings | "Notify about updates" could be toggled while background checks were off, where it does nothing. | Disabled unless background checks are on, as is the new "Install updates automatically" (which also needs PackageKit). |
| App settings | Clearing the name field saved an empty name. | An empty name is reverted; names are trimmed. |
| Scan page | The camera image was inset by the page margins. | Graphics go edge to edge: the viewfinder spans the page width and is clipped to a square. |
| About | Listed only GitHub, Codeberg, Forgejo and Gitea. | Lists every source; credits zxing-cpp and its licence. |
| Launcher icon | A plain arrow on a circle; read as "go", not as a harpoon. | A barbed harpoon rising from the sea, on the round Sailfish icon shape. |

Checked and fine as they were: pulley sizes (four items at most) and order (the most used
item nearest the content), scroll decorators on every flickable page, `label` and
`placeholderText` on every text field, Enter key handling, colours taken from `Theme` (the QR
codes keep a white background on purpose so they scan on any ambience), remorse instead of
confirmation dialogs, and the empty-list placeholder.

Still to check on a device: both light and dark ambiences, landscape on a tablet, the
`icon-m-file-rpm` theme icon, and how the cover looks with long app names.
