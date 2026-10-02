# UI review checklist

Two first-party lists, merged: the eleven *Common Pitfalls in Sailfish Application
Development* from the SDK's own Silica documentation, and Jolla's UI *Definition of Done*.
Walk a screen against these to answer "is this finished?".

Every item here fails silently. None of it is a build error.

## The eleven pitfalls

### 1. The cover does nothing

Active covers are core to Sailfish multitasking, and porting from Android or iOS is exactly
where they get skipped, because neither platform has the concept. A cover should summarise the
app's contents and any open task, and offer shortcuts to common actions — save a new item, run
a search.

A cover showing only a logo is the most common design failure on the platform.

### 2. Non-standard label colouring

Colour signals whether something is interactive:

| Kind of element | Colour |
| --- | --- |
| Interactive — buttons, switches, list entries, anything reacting to touch | `Theme.primaryColor` |
| Purely descriptive — static labels, page and section headers | `Theme.highlightColor` |
| Anything, while pressed | `Theme.highlightColor` |

Note the shape of this: the highlight colour does double duty. It marks text that is *not*
interactive, and it also marks interactive text that is *currently pressed*.

Silica components with built-in text — `Button`, `ComboBox`, `ContextMenu` — handle the press
colour themselves. Custom components must do it by hand:

```qml
ListItem {
    id: listItem
    width: parent.width

    Label {
        text: model.text
        color: listItem.highlighted ? Theme.highlightColor : Theme.primaryColor
    }
}
```

A purely descriptive label needs no condition:

```qml
Label {
    color: Theme.highlightColor
    text: "Terms of Use. By selecting Accept you agree to..."
    width: parent.width
}
```

### 3. Incorrect alignment, sizing or spacing

Graphics and images sit flush with the page edges — gallery images, album art. Text and icons
are inset: `Theme.horizontalPageMargin` left and right, `Theme.paddingLarge` top and bottom.

Some controls already carry the horizontal margin — combo boxes, text fields, `PageHeader` —
and expose `leftMargin` / `rightMargin` to adjust it. Custom items must add it themselves:

```qml
Label {
    text: "A very, very long sentence that will extend beyond the width of the screen."
    truncationMode: TruncationMode.Fade
    color: Theme.highlightColor

    anchors {
        left: parent.left;   leftMargin:  Theme.horizontalPageMargin
        right: parent.right; rightMargin: Theme.horizontalPageMargin
        verticalCenter: parent.verticalCenter
    }
}
```

### 4. Touch areas that are too small

Minimum touchable height is `Theme.itemSizeSmall`. Most touchable items should be
`Theme.itemSizeMedium` or larger, scaled to their complexity.

Most Silica components draw no border around their interactive area, so an undersized touch
target looks completely correct on screen and simply misses. Where items sit in a `Row` or
`Column`, move the spacing *inside* each item's touch area rather than between them, so the
gaps are still touchable.

### 5. Too many pulley items

Never more than four in a `PullDownMenu` or `PushUpMenu`. Most platform apps use one to three.
The comparison the documentation draws: you would not put six to eight items in a traditional
toolbar either.

### 6. A pulley menu whose items are all disabled

Hide the whole menu rather than showing one that cannot be used.

```qml
PullDownMenu {
    MenuItem { text: "Remove" }
    visible: playList.selectionCount > 0
}
```

### 7. Missing scroll decorators

Any view whose content can flow off-screen needs one. It shows where the viewport sits and
hints at how much more there is.

```qml
SilicaListView {
    anchors.fill: parent
    VerticalScrollDecorator {}
}
```

### 8. Buttons where platform gestures belong

Four substitutions, each removing a control the platform already provides:

| Habit | Sailfish |
| --- | --- |
| Accept / Cancel buttons | The dialog accept and cancel gestures, via `DialogHeader` |
| A back button | The back-stepping gesture |
| Exit or Home buttons | The edge-swipe to Home |
| A toolbar | A pulley menu |

```qml
Dialog {
    onAccepted: account.logIn()
    onRejected: accountCreationCanceled()
}
```

### 9. Insufficient text editor labelling

Every `TextField` and `TextArea` sets both `placeholderText` (shown while empty) and `label`
(shown once the user has typed). The same descriptive string serves both.

```qml
TextField {
    placeholderText: "First name"
    label: "First name"
    width: parent.width
}
```

### 10. An unconfigured Enter key

Enter produces no newline in a single-line editor, so it is free to be overloaded. Three
patterns, in order of preference:

Move focus to the next field:

```qml
TextField {
    label: "Username"
    EnterKey.enabled: text.length > 0
    EnterKey.iconSource: "image://theme/icon-m-enter-next"
    EnterKey.onClicked: passwordField.focus = true
}
```

Submit, when the field is last on the page:

```qml
EnterKey.iconSource: "image://theme/icon-m-enter-accept"
EnterKey.onClicked: account.login()
```

Or, if neither applies, dismiss the keyboard:

```qml
EnterKey.iconSource: "image://theme/icon-m-enter-close"
EnterKey.onClicked: focus = false
```

### 11. Unwieldy page hierarchies

A deep stack leaves the user unsure where they are. Where the user is moving sideways rather
than deeper, use `pageStack.replace()`. The platform Maps app replaces its in-app search pages
rather than pushing, so the stack cannot grow without bound.

## Definition of Done

Jolla's own checklist for a finished UI contribution.

**Visual**

- Alignment matches the design.
- Icons, backgrounds, margins, colours and fonts match the platform style.
- Fonts, colours, margins and other layout parameters come from Silica's `Theme`, not from
  literals.

**Performance**

- No dropped frames during interaction.
- Loading times for the app and its pages are not made worse.
- Animations between states stay fluid.
- Memory use stays within reasonable bounds.
- The interface still behaves with a large data set.

**Completeness**

- Every state looks finished and intentional.
- No placeholder content and no half-built functionality.
- The user is offered a way to recover from errors such as no network or corrupt data.

**Code**

- User-visible text goes through working translation hooks.
- Unit tests cover the internal logic.
- Existing component tests still pass, and nothing else regressed.
- The change builds in OBS.

**Review**

- Reviewed and approved by the area maintainer, preferably one with UI development
  background.
- User-facing changes reviewed and approved by design.
