---
name: sailfish-ui-design
description: >
  How a Sailfish OS app should look and behave — the platform's gesture-driven interaction
  model, its navigation structure, ambience-based theming, and the standards a finished UI is
  reviewed against. Use when: (1) designing or laying out a Sailfish screen, page or app
  before or while writing QML, (2) deciding between a page, a dialog and an attached page, or
  between a pulley menu, a context menu and a cover action, (3) porting an app from Android or
  iOS and needing to know which patterns do not transfer, (4) designing an application cover,
  (5) making a UI work across screen sizes or respect the user's ambience, or (6) reviewing
  whether a Sailfish UI is finished. For the Silica component API, see sailfish-app-development.
metadata:
  written-against:
    sailfish-os: 5.1.0.11
    sdk: 3.13.5
---

# Designing for Sailfish OS

Sailfish's interaction model is unusual, and the ways it differs are load-bearing rather than
cosmetic. Almost every Android and iOS navigation pattern has a Sailfish equivalent that is
*not* a rename — it is a different mechanism. All of them compile, so nothing warns you.

The platform's own framing, condensed to what changes a decision:

- **Interaction is effortless and one-handed.** Gestures over small targets, because the device
  is used in a hectic mobile environment.
- **All screen estate belongs to the content.** The OS keeps almost no permanent chrome, and
  neither should an app.
- **The ambience is the user's.** Colours come from their choice, not the app's brand.
- **Consistency over novelty.** The same task should work the same way it does in platform
  apps.

## The OS owns the edges

This is the constraint that reshapes everything else. Swipes from the screen edge belong to the
system:

| Edge | System action |
| --- | --- |
| Top | Top Menu — device lock, silent mode, ambience switching |
| Left and right | Back to Home |
| Bottom | App Grid |

So the platform, not the app, already provides going back, leaving the app, and switching
apps. What an app gets instead:

| Need | Sailfish mechanism |
| --- | --- |
| Go back | The back-stepping gesture, on the page stack |
| Go forward, or to a secondary screen | An attached page |
| A menu of page actions | A pulley menu — `PullDownMenu` or `PushUpMenu` |
| Actions on one item | A `ContextMenu`, opened by long-press |
| Confirm or cancel | The `Dialog` accept and reject gestures |
| Undo something destructive | A `Remorse` countdown |
| Act on the app while it is minimised | Cover actions |

Design the navigation from that second table. Every row of it replaces something an app on
another platform would have built out of buttons and edges.

## Where the app lives

An app is one surface inside a shell the user moves through constantly:

- **Lock screen** — time, notifications and status without unlocking.
- **Home** — minimised apps shown as live **covers**, in the order they were opened.
- **Events** — notifications, weather and system actions, reached from the left.
- **Top Menu** — device settings and ambience, from the top edge.
- **App Grid** — the launcher, from the bottom edge.

The consequence for design work: your app is visible to the user while it is *not* in the
foreground, as a cover on Home. That surface is part of the app, not an afterthought.

## The cover earns its place

A cover is a live summary plus up to two actions — not a logo, not an icon. It should show the
app's current state and any open task, and offer the one or two things a user most often wants
without opening the app: save a new item, start a search, skip a track.

"Lack of useful active covers" is the *first* documented pitfall on the platform, and it is
specifically called out as what goes wrong when porting from Android or iOS, since neither has
the concept.

## Deciding structure

**Page, dialog, or attached page?**

- A **page** is a step deeper into the app's hierarchy. Push it onto the stack.
- A **dialog** is a page that needs a decision. It is accepted or rejected by gesture, and it
  reports back through `onAccepted` / `onRejected`. Use it for input that the user can abandon.
- An **attached page** is a sibling reached by a forward gesture rather than a push. This is
  what replaces a hamburger menu or a tab bar — a second surface of equal standing.

If the user is moving *sideways* through equivalent screens rather than deeper, use
`pageStack.replace()` so the stack does not grow without bound.

**Pulley menu, context menu, or cover action?**

- **Pulley** — actions that apply to the whole page. Keep it short, and hide it entirely when
  everything in it is disabled. The checklist gives the limit.
- **Context menu** — actions that apply to one item, opened by long-pressing that item.
- **Cover action** — the one or two actions worth doing without opening the app at all.

**Confirmation.** Sailfish does not use "are you sure?" dialogs for destructive actions. Do the
thing immediately and offer a `RemorseItem` (within a list) or `RemorsePopup` (page-level) that
counts down and lets the user take it back. Design for undo, not for confirmation.

**Reflow or re-layout?** Minor differences in screen size are handled by `Theme` constants and
anchoring. When a large screen genuinely wants a different structure — a split view rather than
a list — branch on `Screen.sizeCategory` and write two layouts. See
`references/scaling-and-assets.md`.

## The ambience is the user's

The user picks an ambience; the system derives a palette and a wallpaper from it, and every
`Theme` colour follows. An app that hardcodes a palette fights the platform and looks broken
against the user's own choice.

- Take every colour from `Theme`. Never a literal.
- `Theme.colorScheme` flips between light-on-dark and dark-on-light. **Design for both**, and
  check both — an app tested only on the default dark ambience will have unreadable text for
  anyone using a light one.
- Monochrome assets served through `SilicaImageProvider` recolour to the theme's primary colour
  automatically, which is the only way to make icons follow the ambience without shipping
  variants.

Colour also carries meaning here: `Theme.primaryColor` marks interactive elements,
`Theme.highlightColor` marks descriptive ones *and* the pressed state. That rule is in
`references/review-checklist.md`.

## Errata: the docs contradict the SDK

Both of these appear in official documentation and are wrong at 5.1.0.11:

| Documentation says | Actually |
| --- | --- |
| `Theme.fontSizeNormal` | Does not exist. Use `Theme.fontSizeMedium`. |
| `Theme.dp` used as a property | It is a method: `Theme.dp(size)`. |

Check any `Theme` member before relying on it:

```bash
T=~/SailfishOS/mersdk/targets/SailfishOS-<version>-<arch>.default
grep -c '"fontSizeMedium"' $T/usr/lib64/qt5/qml/Sailfish/Silica/plugins.qmltypes
```

The installed module is the authority; the published documentation lags it.

## Reference files

- **`references/review-checklist.md`** The eleven documented pitfalls and Jolla's UI Definition
  of Done, merged into one pass/fail list. Use it to answer "is this screen finished?".
- **`references/scaling-and-assets.md`** The `Theme` vocabulary as design units, `Theme.dp()`,
  `Screen.sizeCategory` branching, SVG constraints, `SilicaImageProvider`, and launcher icons.

For the Silica component inventory and API, see the **`sailfish-app-development`** skill
(`references/silica.md`). For submission rules, see **`jolla-store-rules-check`**.
