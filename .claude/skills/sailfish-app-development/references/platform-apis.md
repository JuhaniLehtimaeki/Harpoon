# Platform APIs

Silica draws the UI. Everything else an app needs — notifications, stored settings, D-Bus,
background work, file pickers, sharing, OAuth, secrets — comes from separate QML modules.

Read from the installed target at 5.1.0.11 and cross-referenced against the Harbour allow
list. Regenerate both for another release.

## The rule that governs this whole file

**The target ships far more than Harbour permits.** At 5.1.0.11:

| | Count |
| --- | --- |
| QML modules installed in the target | 87 |
| Allowed by the Harbour validator | 38 |
| **Installed, importable, and fatal at submission** | **49** |

An unshippable module imports cleanly, compiles, and runs correctly on a device. Nothing warns
you until `sfdk check`. Confirm a module is allowed *before* building anything on it.

To regenerate the comparison:

```bash
T=~/SailfishOS/mersdk/targets/SailfishOS-<version>-<arch>.default/usr/lib*/qt5/qml
find $T -name qmldir | sed "s|$T/||;s|/qmldir||" | tr / . | sort
# compare against allowed_qmlimports.conf in sailfishos/sdk-harbour-rpmvalidator
```

## Capability map

What to reach for, and what it gives you. Every entry here is Harbour-allowed.

| Need | Import | Main types |
| --- | --- | --- |
| Notifications | `Nemo.Notifications 1.0` | `Notification` |
| Stored settings (dconf) | `Nemo.Configuration 1.0` | `ConfigurationValue`, `ConfigurationGroup` |
| D-Bus, both directions | `Nemo.DBus 2.0` | `DBusInterface`, `DBusAdaptor` |
| Run while backgrounded | `Nemo.KeepAlive 1.2` | `KeepAlive`, `BackgroundJob`, `DisplayBlanking` |
| Pick a file, image, video, music | `Sailfish.Pickers 1.0` | 16 picker pages and dialogs |
| Share content to other apps | `Sailfish.Share 1.0` | `ShareAction`, `ShareResource` |
| Receive content shared from other apps | `Sailfish.Share 1.0` | `ShareProvider` |
| OAuth 1.0a and OAuth 2 | `Amber.Web.Authorization 1.0` | `OAuth10a`, `OAuth2Ac`, `OAuth2AcPkce`, `OAuth2Implicit` |
| Store credentials securely | `Sailfish.Secrets 1.0` (not on Jolla C2 — see below) | `SecretManager`, `StoreSecretRequest`, `StoredSecretRequest` |
| System accounts | `Sailfish.Accounts 1.0` | `AccountManager`, `AccountModel`, `AccountProviderPicker` |
| Contacts | `Sailfish.Contacts 1.0`, `org.nemomobile.contacts 1.0` | 36 UI components, plus models |
| Media player integration (MPRIS) | `Amber.Mpris 1.0` | `MprisPlayer`, `MprisController`, `MprisMetaData` |
| Thumbnails | `Nemo.Thumbnailer 1.0` | `Thumbnail` |
| Web content | `Sailfish.WebView 1.0` | see the note below |
| GPS and location | `QtPositioning 5.2` / `5.4`, `QtLocation` | standard Qt |
| Sensors | `QtSensors 5.0`–`5.2` | standard Qt |
| Haptic feedback | `QtFeedback 5.0` | `ThemeEffect` only — see restriction below |
| Local database | `QtQuick.LocalStorage 2.0` | standard Qt SQLite |
| Sound and video playback | `QtMultimedia 5.0`–`5.6` | standard Qt |
| WebSockets | `QtWebSockets 1.0` / `1.1` | standard Qt |
| Python backend | `io.thp.pyotherside 1.0`–`1.5` | PyOtherSide |
| System state | `org.freedesktop.contextkit 1.0` | ContextKit properties |
| Bluetooth | `Sailfish.Bluetooth 1.0`, `org.kde.bluezqt 1.0` | |
| Time and timers | `Nemo.Time 1.0` | |

Three caveats on that table:

- **The version numbers matter, and this table is not the authority.** Several modules are
  permitted only at specific versions, and `QtFeedback` only in part. The authoritative list
  lives in the **`jolla-store-rules-check`** skill; where the two disagree, that one is right.
- **`Sailfish.WebView` and `Sailfish.WebEngine` are allowed but not installed** in the default
  build target. Add the package to the target before importing them, or the build fails with
  a module-not-found error that looks like a Harbour problem and is not.
- **`Sailfish.Secrets` is the opposite case: in the target, missing on the device.** The
  `SailfishOS-5.1.0.11-aarch64` target ships it, so the app compiles and passes the allow list.
  A Jolla C2 at 5.1.0.11 has no QML module, no library and no `sailfishsecretsd` daemon — even
  though `/etc/sailjail/permissions/Secrets.permission` points at that daemon. Check the device
  you target (`ls /usr/lib64/qt5/qml/Sailfish/`) before relying on it, and keep a fallback;
  see OAuth below.

## Installed, but you cannot ship it

These are all present in the target and all rejected at intake. The tempting ones:

| Module | What it looks like it does |
| --- | --- |
| `Sailfish.Gallery` | Image and video gallery components |
| `Sailfish.Ambience` | Reading and setting the ambience |
| `Sailfish.TransferEngine`, `org.nemomobile.transferengine` | Upload and download transfers |
| `Sailfish.FileManager`, `Nemo.FileManager` | File operations |
| `Sailfish.Store` | Jolla Store integration |
| `Sailfish.TextLinking` | Turning text into actionable links |
| `Sailfish.Timezone`, `Sailfish.Policy`, `Sailfish.Mdm`, `Sailfish.AccessControl` | System policy and management |
| `Nemo.Email`, `org.nemomobile.email` | Email accounts and messages |
| `Nemo.Mce` | Display and device state |
| `Nemo.Ngf`, `org.nemomobile.ngf` | Non-graphic feedback (vibration, tones) |
| `Nemo.Connectivity`, `Connman`, `MeeGo.Connman` | Network connection management |
| `Nemo.Ssu` | Software update and repositories |
| `QOfono`, `MeeGo.QOfono`, `org.nemomobile.ofono` | Modem and cellular |
| `QtContacts`, `QtOrganizer`, `QtDocGallery` | Qt Mobility backends |
| `org.nemomobile.commhistory` | Call and message history |
| `org.nemomobile.lipstick`, `org.nemomobile.systemsettings`, `org.nemomobile.devicelock` | Shell and settings internals |
| `com.jolla.settings`, `com.jolla.connection` | Jolla settings UI |

Note the pattern in the `org.nemomobile.*` rows: several are the *old names* of modules that
are allowed under a new name — `org.nemomobile.notifications` became `Nemo.Notifications`,
`org.nemomobile.dbus` became `Nemo.DBus`, `org.nemomobile.configuration` became
`Nemo.Configuration`, `org.nemomobile.thumbnailer` became `Nemo.Thumbnailer`. Both spellings
are installed; only the new one ships. `org.nemomobile.contacts` is the exception — it is
still the allowed spelling.

## Permissions travel with APIs

Reaching a platform API is not enough — Sailjail has to allow it too, and permissions are
declared in the `.desktop` file rather than requested in code. Network access needs
`Internet`; an image picker reaching the user's photos needs `Pictures`; and so on through
`Audio`, `Camera`, `Contacts`, `Location`, `Microphone`, `Documents`, `Downloads`, `Music`,
`Videos`, `Secrets`, `Accounts`, `Bluetooth`, `NFC`, `WebView`, `RemovableMedia`, `UserDirs`,
`PublicDir`, `MediaIndexing`, `Compatibility`.

An app that works in development and fails once installed is usually missing a permission, not
an import. The allowed set and the `[X-Sailjail]` syntax are in the `jolla-store-rules-check` skill.

## Patterns

### Notifications

`Notification` has 28 properties; these are the ones that matter. `publish()` sends it,
`close()` withdraws it, and reusing `replacesId` updates a notification in place instead of
stacking a second one.

```qml
import Nemo.Notifications 1.0

Notification {
    id: notification
    category: "x-nemo.example"
    appName: "My App"
    summary: "Download finished"
    body: "report.pdf is ready"
    previewSummary: "Download finished"   // shown as a banner
    previewBody: "report.pdf is ready"
    urgency: Notification.Normal          // Low, Normal, Critical
    itemCount: 1
    onClicked: pageStack.push(downloadsPage)
}
// notification.publish()
```

`expireTimeout`, `isTransient`, `resident`, `progress` and `remoteActions` cover the rest:
a transient notification does not persist in the Events view, a resident one stays after being
clicked, and `remoteActions` attaches D-Bus-invoked buttons.

### Settings

`ConfigurationValue` binds a single dconf key as a QML property, with a default:

```qml
import Nemo.Configuration 1.0

ConfigurationValue {
    id: fontSize
    key: "/apps/harbour-myapp/fontSize"
    defaultValue: 16
}
// read: fontSize.value      write: fontSize.value = 20
```

`ConfigurationGroup` holds a set of keys under one path. Both read and write asynchronously
and emit change notifications, so bindings update when the value changes elsewhere. Call
`sync()` when a write must be flushed immediately.

### D-Bus

Calling another service:

```qml
import Nemo.DBus 2.0

DBusInterface {
    id: iface
    service: "org.freedesktop.NetworkManager"
    path: "/org/freedesktop/NetworkManager"
    iface: "org.freedesktop.NetworkManager"
    watchServiceStatus: true          // status tracks availability
    propertiesEnabled: true           // expose D-Bus properties
    signalsEnabled: true              // route signals to on<SignalName> handlers
}
// iface.call("Method", [arg])
// iface.typedCall("Method", [{ type: "s", value: "x" }], onSuccess, onError)
// iface.getProperty("State")
```

`typedCall` is the one to use when argument types matter — plain `call` guesses. Publishing
your own service uses `DBusAdaptor` with `service`, `path`, `iface` and an `xml` introspection
string, plus `emitSignal()`.

### Background work

Three separate things, often confused:

```qml
import Nemo.KeepAlive 1.2

KeepAlive { enabled: player.playing }        // stop the device suspending

DisplayBlanking { preventBlanking: true }    // stop the screen blanking

BackgroundJob {
    frequency: BackgroundJob.FifteenMinutes
    enabled: true
    onTriggered: {
        begin()                               // must be paired
        refresh()
        finished()
    }
}
```

`BackgroundJob.frequency` takes a fixed set of intervals — `ThirtySeconds`,
`TwoAndHalfMinutes`, `FiveMinutes`, `TenMinutes`, `FifteenMinutes`, `ThirtyMinutes`,
`OneHour`, `TwoHours`, `FourHours`, `EightHours`, `TenHours`, `TwelveHours`,
`TwentyFourHours` — or `Range` with `minimumWait` and `maximumWait` in seconds. The device
batches wakeups across apps, which is why the choices are coarse.

`begin()` and `finished()` must bracket the work, or the device may suspend mid-job.

### Pickers

Do not build a file browser. Push one of the 16 ready-made pages and read the result:

```qml
import Sailfish.Pickers 1.0

pageStack.push(Qt.resolvedUrl("ImagePickerPage.qml"))   // or via a Component
```

A single-selection page sets `selectedContent` (a `url`) and `selectedContentProperties`
(metadata). A multi-selection dialog sets `selectedContent` to a `ListModel` of the chosen
items.

Pages: `FilePickerPage`, `ImagePickerPage`, `VideoPickerPage`, `MusicPickerPage`,
`DocumentPickerPage`, `DownloadPickerPage`, `ContentPickerPage`, `FolderPickerPage`.
Dialogs, each multi-select: `MultiFilePickerDialog`, `MultiImagePickerDialog`,
`MultiVideoPickerDialog`, `MultiMusicPickerDialog`, `MultiDocumentPickerDialog`,
`MultiDownloadPickerDialog`, `MultiContentPickerDialog`, and `FolderPickerDialog`.

### Sharing

`Sailfish.Share 1.0` has two halves, and they are separate types. Sending content *out* is
`ShareAction`. Receiving content *in* — being one of the targets in someone else's share sheet
— is `ShareProvider`. Both are on Harbour's allowed-import list.

The full contract, read from `$T/Sailfish/Share/plugins.qmltypes` at 5.1.0.11 (`$T` as set at
the top of this file):

| Type | Properties | Signals | Methods |
| --- | --- | --- | --- |
| `ShareAction` | `resources`, `mimeType`, `title`, `selectedTransferMethodInfo` | `done` | `trigger()`, `loadConfiguration()`, `toConfiguration()`, `replaceFileResourcesWithFileDescriptors()`, `writeContentToFile()`, `removeFilesAndRmdir()` |
| `ShareProvider` | `method`, `registerName`, `capabilities` | `triggered(resources)` | — |
| `ShareResource` | `type`, `name`, `data`, `filePath` — all read-only | — | — |

**Sending.**

```qml
import Sailfish.Share 1.0

ShareAction {
    id: share
    mimeType: "text/plain"
    title: qsTr("Share note")
    resources: [ { "data": note.text, "name": "note.txt" } ]
}
// share.trigger()
```

`trigger()` opens the platform share sheet, so the app does not implement one. `done` fires
when the sheet finishes. Omitting `mimeType` makes it derive one; `"image/*"` or `"*"` widen
the set of targets offered.

`resources` is a plain list. Each entry is either a file path (a string or a `url`) or a map of
raw data with metadata, `{ "data": …, "name": … }`. **`ShareResource` is not creatable from
QML** — `isCreatable: false` in the type info. It is the read-only view you get on the
*receiving* side, not something you construct to send. Its `type` is `StringDataType` (1) or
`FilePathType` (2), which is how you tell the two apart in a `triggered` handler.

**Receiving.**

```qml
import Sailfish.Share 1.0

ShareProvider {
    method: "myShareMethod"          // must match the desktop file
    registerName: true
    capabilities: ["text/plain", "image/*"]

    onTriggered: {
        for (var i = 0; i < resources.length; ++i) {
            var r = resources[i]
            if (r.type === ShareResource.FilePathType) {
                importFile(r.filePath)
            } else {
                importText(r.data, r.name)
            }
        }
    }
}
```

- `method` names the share method and **must match a name declared in the desktop file**.
- `capabilities` is the MIME types accepted, and should match the desktop file's `Capabilities`.
  Empty, undefined, or containing `"*"` means no MIME filtering happens at all.
- `registerName: true` registers the D-Bus bus name on the session bus for you. It only works
  for the simple case of a single `ShareProvider`; with more than one, register the name from
  C++ instead.

**Do not trust the MIME type you are handed.** The documentation says so explicitly: the
reported type comes from the sending app and is not validated for you. Check the content
yourself before acting on it — this is a trust boundary, and the sender is another application.

**The desktop-file half is not documented.** The `ShareProvider` page says `method` must
correspond to a name in `X-Share-Methods` with a matching `X-Share Method` section carrying
`Capabilities`, and then does not give the format. It is not in the `declarative-transferengine`
docs, there is no example in `~/SailfishOS/examples/`, and no `.desktop` file in the build
target declares `X-Share-Methods`. So the QML side above is solid and the registration side is
not — treat it as the thing to work out against a device, not as something these notes settled.

Two things that are known about it: the Harbour validator does not whitelist `[Desktop Entry]`
keys (it only restricts `[X-Sailjail]` to four), so the extra keys are not a validation error;
and no first-party source confirms a third-party Harbour app actually working as a share target
under Sailjail. Verify on a device before building on it.

### OAuth

Four flow types, so pick by what the service requires: `OAuth10a`, `OAuth2Ac`
(authorization code), `OAuth2AcPkce` (authorization code with PKCE, the right default for a
new integration) and `OAuth2Implicit`. `RedirectListener` handles the callback.

Do not store the resulting tokens in dconf: `Nemo.Configuration` is plain-text settings that
other processes can read, not a credential store. `Sailfish.Secrets` is the intended home, but
it is missing on the Jolla C2 (caveats above), so use it only with a fallback.

A missing module fails the whole QML file that imports it, not just the import. So put the
`Sailfish.Secrets` code in a file of its own and load that file through a `Loader`; the rest of
the app survives and can switch to the fallback:

```qml
// SecretStore.qml is the only file that imports Sailfish.Secrets
Loader {
    id: secretStore
    source: "SecretStore.qml"
    onStatusChanged: if (status === Loader.Error) useFileStore = true
}
```

The fallback is a file in the app's own config folder (`AppConfigLocation`), which Sailjail
creates readable by the owner only. Be clear about what that buys: the token is plain text on
disk. Other sandboxed apps cannot read it; unsandboxed processes running as the same user, and
backups, can. It matches Android's private preferences — do not describe it as secure storage.

## Checking any module yourself

The target is readable, so an unfamiliar module's real API is one command away:

```bash
T=~/SailfishOS/mersdk/targets/SailfishOS-<version>-<arch>.default/usr/lib64/qt5/qml
cat $T/Nemo/Notifications/qmldir              # what is public
ls  $T/Sailfish/Pickers/*.qml                 # QML-defined components, readable source
grep -o '"[\w.]*/\w* [0-9]' $T/Nemo/DBus/plugins.qmltypes | sort -u   # plugin-registered types
```

Prefer this over the published API documentation, which for several of these modules is an
index page with no properties, methods or examples on it.
