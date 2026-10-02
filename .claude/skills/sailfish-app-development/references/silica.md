# Sailfish Silica

Silica is the QML module that makes an app look and behave like a Sailfish app. It is not
Qt Quick Controls with different names — the interaction model is different, and most of what
distinguishes a correct Silica app from a wrong one compiles either way.

Everything below is read from the installed target at Sailfish OS 5.1.0.11. Re-derive it for
another release; the surface moves.

## Read the module itself

The authoritative list of what exists is the module in the build target, not any web page:

```bash
T=~/SailfishOS/mersdk/targets/SailfishOS-<version>-<arch>.default
ls $T/usr/lib64/qt5/qml/Sailfish/Silica/          # every component, as .qml source
cat $T/usr/lib64/qt5/qml/Sailfish/Silica/qmldir   # what is public, and at which version
```

`qmldir` is the contract: a type not listed there is private, whatever the `private/`
subdirectory contains. The components are readable QML — when a property's behaviour is
unclear, read the source rather than guessing.

Two other first-party sources:

- `~/SailfishOS/documentation/sailfishsilica*.qch` — the offline API reference, openable in
  Qt Assistant or the Sailfish IDE.
- `~/SailfishOS/examples/componentgallery/` — a working app demonstrating most components.
  `cameragallery`, `mediagallery` and `notificationgallery` sit beside it.

## What exists at 5.1.0.11

**87 QML components**, from `qmldir`:

| Group | Types |
| --- | --- |
| Application shell | `ApplicationWindow`, `Page`, `PageHeader`, `PageStack`, `FullscreenContentPage`, `Drawer`, `DockedPanel` |
| Dialogs and pickers | `Dialog`, `DialogHeader`, `DatePicker`, `DatePickerDialog`, `TimePicker`, `TimePickerDialog`, `ColorPicker`, `ColorPickerDialog`, `ColorPickerPage` |
| Views | `SilicaFlickable`, `SilicaListView`, `SilicaGridView`, `ColumnView`, `NestedGridView`, `SlideshowView`, `ViewPlaceholder` |
| Scroll decoration | `ScrollDecorator`, `VerticalScrollDecorator`, `HorizontalScrollDecorator` |
| Menus | `PullDownMenu`, `PushUpMenu`, `ContextMenu`, `MenuItem`, `IconMenuItem`, `MenuLabel`, `PulleyAnimationHint` |
| Buttons | `Button`, `SecondaryButton`, `IconButton`, `ValueButton`, `BackgroundItem`, `GridItem` |
| Inputs | `TextField`, `TextArea`, `PasswordField`, `SearchField`, `TextEditorLabel`, `ComboBox`, `IconComboBox`, `MiniComboBox`, `Slider`, `Switch`, `TextSwitch`, `IconTextSwitch`, `Keypad` |
| Text and icons | `Label`, `LinkedLabel`, `InfoLabel`, `DetailItem`, `SectionHeader`, `Separator`, `Icon`, `HighlightImage` |
| List items | `ListItem`, `ExpandingSection`, `ExpandingSectionGroup` |
| Progress and busy | `BusyIndicator`, `BusyLabel`, `PageBusyIndicator`, `ProgressBar`, `ProgressCircle` |
| Undo | `Remorse` (singleton), `RemorseItem`, `RemorsePopup` |
| Covers | `CoverBackground`, `CoverPlaceholder` |
| Hints | `FirstTimeUseCounter`, `InteractionHintLabel`, `TapInteractionHint`, `TouchInteractionHint` |
| Animation and effects | `AddAnimation`, `RemoveAnimation`, `FadeAnimation`, `FadeAnimator`, `OpacityRampEffect` |
| Backgrounds | `PanelBackground`, `HighlightBar` |

**Plus types registered from the C++ plugin**, which have no `.qml` file and so do not appear
in a directory listing: `Theme`, `Screen`, `Clipboard`, `StandardPaths`, `Formatter`,
`Notices`, `Notice`, `Cover`, `CoverAction`, `CoverActionList`, `PagedView`, `ButtonLayout`,
`SilicaControl`, `SilicaItem`, `Palette`, `EnterKey`, `TouchBlocker`, `GlassItem`,
`InverseMouseArea`, `TextEditor`, `FormattingProxyModel`, `DimmedRegion`, plus the enum
namespaces `Orientation`, `PageStatus`, `PageStackAction`, `DialogResult`, `DialogStatus`,
`TruncationMode`, `CutoutMode`, `FocusBehavior`, `Dock`, `RoundedCorner`, `BusyIndicatorSize`.

To regenerate for another release:

```bash
grep -E '^[A-Za-z]' $T/usr/lib64/qt5/qml/Sailfish/Silica/qmldir | awk '{print $1}' | sort -u
grep -oE '"Sailfish\.Silica/[A-Za-z]+ [0-9]' $T/usr/lib64/qt5/qml/Sailfish/Silica/plugins.qmltypes \
  | sed 's|.*/||;s| .*||' | sort -u
```

## Qt Quick Controls reflexes to unlearn

Silica is a *replacement* for Qt Quick Controls, not a companion. `import QtQuick.Controls`
is not on Harbour's allowed-import list, so reaching for it fails at submission rather than at
compile time. The habitual substitutions:

| Reflex | Silica |
| --- | --- |
| `ApplicationWindow` from Controls | `ApplicationWindow` from `Sailfish.Silica` — different type, `initialPage` and `cover` properties |
| `ToolBar`, `MenuBar`, hamburger | `PullDownMenu` / `PushUpMenu`, or an attached page |
| `StackView` | `pageStack` (a `PageStack`, already on `ApplicationWindow`) |
| `Dialog` with OK/Cancel buttons | Silica `Dialog` with `DialogHeader`; accept and reject are *gestures* |
| `ListView` | `SilicaListView` — adds pulley menu support and view placeholders |
| `Flickable` | `SilicaFlickable` |
| `TextField` from Controls | Silica `TextField`, which requires `label` and `placeholderText` |
| `Drawer` from Controls | Silica `Drawer` — different semantics; usually you want an attached page |
| `BusyIndicator` from Controls | Silica `BusyIndicator`, sized by `BusyIndicatorSize` |
| `Snackbar` / toast | `RemorsePopup` or `RemorseItem` |
| Literal pixel values | `Theme.*` constants |

## Theme is the whole design system

Never write a literal size, margin, colour or font size. The complete `Theme` surface at
5.1.0.11:

**Sizing** — `itemSizeExtraSmall`, `itemSizeSmall`, `itemSizeMedium`, `itemSizeLarge`,
`itemSizeExtraLarge`, `itemSizeHuge`; `iconSizeExtraSmall`, `iconSizeSmall`,
`iconSizeSmallPlus`, `iconSizeMedium`, `iconSizeLarge`, `iconSizeExtraLarge`,
`iconSizeLauncher`; `buttonWidthTiny`, `buttonWidthExtraSmall`, `buttonWidthSmall`,
`buttonWidthMedium`, `buttonWidthLarge`; `coverSizeSmall`, `coverSizeLarge`.

**Spacing** — `paddingSmall`, `paddingMedium`, `paddingLarge`, `horizontalPageMargin`,
`pageStackIndicatorWidth`.

**Fonts** — `fontSizeTiny`, `fontSizeExtraSmall`, `fontSizeSmall`, `fontSizeMedium`,
`fontSizeLarge`, `fontSizeExtraLarge`, `fontSizeHuge`, each with a `…Base` variant that does
not scale with the user's font-size setting; `fontFamily`, `fontFamilyHeading`.

**Colour** — `primaryColor`, `secondaryColor`, `highlightColor`, `secondaryHighlightColor`,
`highlightBackgroundColor`, `highlightDimmerColor`, `highlightFromColor`, `errorColor`,
`overlayBackgroundColor`, `backgroundGlowColor`, `presenceColor`; `lightPrimaryColor`,
`lightSecondaryColor`, `darkPrimaryColor`, `darkSecondaryColor`; `colorScheme`.
`Theme.rgba(color, opacity)` composes them.

**Opacity** — `opacityFaint`, `opacityLow`, `opacityHigh`, `opacityOverlay`,
`highlightBackgroundOpacity`.

**Scaling** — `pixelRatio`, `dp`. Multiply by `Theme.pixelRatio` only for a dimension that has
no `Theme` constant; prefer the constant.

**Interaction** — `startDragDistance`, `flickDeceleration`, `maximumFlickVelocity`,
`minimumPressHighlightTime`.

`Screen.sizeCategory` (`Screen.Small` … `Screen.ExtraLarge`) branches layout by device class.

## Design rules live elsewhere

The eleven documented pitfalls — label colouring, margins, touch-target sizes, pulley limits,
scroll decorators, gestures over buttons, editor labelling, the Enter key, page-stack depth —
are design rules rather than API, and they live in the **`sailfish-ui-design`** skill
(`references/review-checklist.md`), together with Jolla's UI Definition of Done.

Read them before writing a screen. Every one of them compiles cleanly when done wrong.


## Core patterns

**Navigation.** `pageStack.push(Qt.resolvedUrl("MyPage.qml"), { prop: value })`,
`pageStack.pop()`, `pageStack.replace(...)`. One page is visible at a time. An *attached page*
— set on a page's `attachedPage`, reached by a forward gesture — replaces the hamburger menu.

**Orientation.** Declare what the page supports and branch on the result; do not assume
portrait.

```qml
Page {
    allowedOrientations: Orientation.All
    // isPortrait / isLandscape are available on the page
}
```

**Covers.**

```qml
ApplicationWindow {
    initialPage: Component { FirstPage { } }
    cover: Component {
        CoverBackground {
            CoverPlaceholder { text: qsTr("Nothing yet") }
            CoverActionList {
                CoverAction {
                    iconSource: "image://theme/icon-cover-next"
                    onTriggered: player.next()
                }
            }
        }
    }
}
```

**Destructive actions.** Never a confirmation dialog — a `RemorseItem` (in a list) or
`RemorsePopup` (page-level), which counts down and lets the user cancel.

**Icons.** `"image://theme/icon-<size>-<name>"`, where size is `l`, `m`, `s`, `splus` or
`cover`. The available names are in the platform icon set; `Theme.iconSize*` gives the
matching dimension.

**Backgrounding.** Watch `Qt.application.state` and stop animations and polling when the app
is not `Qt.ApplicationActive`.
