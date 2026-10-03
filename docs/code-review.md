# Code review (October 2026)

Four parallel reviews covered networking and sources, the app/install/storage core and the
command-line tool, the Sigstore verification, and the app layer (C++ and QML). Every finding
below was checked against the code before it was fixed, and each fix has a test unless noted.

## Security
| Finding | Fix |
|---|---|
| **High.** Package names reached `rpm -q` as arguments, so a release asset named `--eval=%(cmd)-1-1.noarch.rpm` (or a crafted id in a record or backup) ran a shell command, also in the privileged background job. Confirmed on the desktop. | `rpm` gets `--` before names and paths; names that are not plausible RPM names never reach it. Ids are validated when records are loaded and saved. |
| **High.** A release could install unrelated packages next to the app (e.g. a system package), and only the main one was checked. | Extra packages must be the app's own (`<name>-…`); every package gets the downgrade and vendor checks; the receipt lists them all and uninstall removes them all. Automatic updates skip releases whose package set changed. |
| **High.** "Refuse unless verified" accepted attestations from GitHub's own Sigstore instance, which Harpoon does not verify at all, and such a bundle could hide one that failed verification. | Enforce accepts only signatures verified on the phone; a failed bundle always wins; unverifiable bundles must at least name the repository. |
| A backup kept its ids, so a shared backup could make Harpoon install over or uninstall any package under any name. | Imported apps get temporary ids; the real name is found again. |
| Exports without secrets kept the request headers of intermediate pages. | Stripped too. |
| HTML request headers (cookies) went to every page and file host; redirects to other origins kept any header except three known credential names. | Headers go only to the tracked page's origin; cross-origin redirects keep only Accept, Accept-Language, User-Agent and Range. |
| The identity check could fall back to the SAN, which names a reusable workflow's repository. | Only the source repository extension counts; URLs are compared canonically. |

## Correctness
- Responses cut off by a timeout or a dropped connection looked like complete 200s.
- Responses had no size limit; ETag probes downloaded whole files when Range was ignored.
- Any 403 from GitHub counted as a rate limit; SourceHut treated rate limits as "no packages".
- User-typed `http://` / `WWW.` / upper case leaked into stored URLs and broke later comparisons.
- rpm-md metadata published only with sha512/sha1 was not verified.
- A PackageKit timeout deleted the files of a transaction that was still running.
- The app and the background job overwrote each other's records (lost receipts, duplicate
  apps); both could install the same app at once over the same download files.
- Turning background checks on and off quickly could leave the timer running.
- Settings fields were reset while being typed into; the settings page went blank after a
  first install changed the app's id; a failed save still showed as done.

## Performance and architecture
- One `rpm -q` per app at start (on the GUI thread, before the window) became one call after
  the window is shown; download progress repaints only on 1% steps; pages listen only to
  their own app.
- Logic the app and the command-line tool both need (download folder, token configuration,
  merging check results, the record after an install) is shared in core instead of copied.

## Left as is, then fixed
- Rekor v2 entries are verified: inclusion proof up to a signed checkpoint, and RFC 3161
  timestamps from a trusted timestamp authority for the time.
- An app can restrict which workflow and which branch or tag may sign its builds
  (`attestationWorkflow`, `attestationRefRegEx`; in the app's Security settings).
- Re-reading one app's installed package no longer blocks the UI: rpm runs in the
  background and the page updates when it answers (stale answers are dropped). Adopting an
  already installed package after a check asks rpm once, also without blocking.
