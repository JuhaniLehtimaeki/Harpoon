# UI/UX review (after 0.1.2)

A pass over every page for platform correctness, ease of use and delight.
What changed, and what was looked at and left alone.

## Changed

- **Release notes are readable.** Forges write release notes in Markdown,
  often with HTML. They were shown as raw text (`## Fixes`, `**bold**`,
  `[link](url)`). `releaseNotesToStyledText()` now turns them into the
  StyledText subset a Silica `Label` shows: headings in bold, bullets,
  working http(s) links (opened externally), HTML stripped and everything
  else escaped. Other link schemes stay plain text.
- **App page header.** The app's own launcher icon (or a generic package
  icon) sits next to its state and any error, like the list row it came
  from, so the page is recognisable at a glance.
- **Download size.** The latest release shows the total size of the
  packages it would download, formatted with `Format.formatFileSize`.
- **App settings: essentials first.** The page opened with a wall of
  regular expressions and sort orders that most apps never need. It now
  shows the name, the source's key field (repository package name or web
  page link filter), the update switches and the security choice. The
  rest (link matching, release ordering and filters, package filters,
  version extraction) sits behind "Show advanced settings". The advanced
  part opens by itself when the app already uses one of those settings, so
  nothing set is hidden.
- **Token help.** Settings links straight to GitHub's page for creating a
  fine-grained token; one with no permissions is enough to raise the rate
  limit for public repositories.
- **Cover.** A faint Harpoon icon in the corner, as Sailfish covers usually
  carry, so the cover is recognisable among others.
- **Empty list.** The hint mentions that apps can be added by QR code.
- **Add dialog.** The keyboard no longer opens over a form already filled
  in from a QR code or a `harpoon://` link.

## Looked at, left as is

- Pull-down menus keep the most used item nearest the finger
  ("Check for updates" on the list, "Check now" on the app page), with
  destructive actions behind a remorse timer.
- The list's section headers (Updates / Apps), context menus and busy
  indicators follow Silica conventions.
- The About page already links to the source code and issue tracker.
- The list header could show when apps were last checked, but the
  controller only knows the time of the last manual "check all"; the
  per-app "Last check" on the app page is the reliable place for it.

## Visual refresh

Harpoon's look comes from its name: a harpoon over the sea. Every graphic is a
monochrome PNG (`gui/art/*.svg` rendered to `gui/qml/images/`) drawn with
`HighlightImage`, so it takes the colours of the user's ambience, light or dark.

- **Waves** (`components/Waves.qml`): layered, seamless waves. They drift slowly on the
  About page and the empty list, only while the page is shown and the app is in front,
  and stay still on the cover.
- **Empty list:** a harpoon bobbing over the waves, "Nothing on the line yet", and what to do.
- **List:** an "updates ready" band under the header installs all updates with one tap.
  Rows end in a status icon (update waiting, check failed, busy). Apps that are not
  installed get a round tile with their initial instead of a generic package icon.
- **App page:** a centred hero (large icon, name, author, state) over a calm sea; the
  install button carries an icon; "What's new in X" comes first, as a card that folds
  long notes (Show all / Show less).
- **About:** the icon over moving waves, the name in large type, buttons for the source
  code and for reporting a problem, then thanks and licence.
- **Settings:** icon switches for the background options; every explanatory text uses
  one `HintLabel` (page margin, extra-small, secondary highlight colour), so hints line
  up and read the same on every page.
- **Add dialog:** a check mark or warning next to what the address was recognised as;
  the scan button has the QR icon.
- **Cover:** tinted waves and harpoon behind the update count.

Only platform icons that exist in SailfishOS 5.1 are used (checked against the SDK's
icon theme). `HARPOON_SCREENSHOTS=<dir>` makes the QML smoke test save rough layout
screenshots for checking alignment.
