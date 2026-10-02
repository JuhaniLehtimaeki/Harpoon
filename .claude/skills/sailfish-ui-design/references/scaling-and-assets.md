# Scaling and assets

Sailfish runs across a range of screen sizes and pixel densities, and the user picks the
colours. A design that hardcodes either is broken on someone's device.

Sourced from *Scaling Sailfish Application UIs* in the SDK's offline Silica documentation
(`sailfish-application-scalability.html`), which is not on the public documentation site, and
verified against the installed module at 5.1.0.11.

## The vocabulary

Design in `Theme` units, not pixels. These are the names to think in.

**Vertical rhythm** — `paddingSmall`, `paddingMedium`, `paddingLarge`.

**Page insets** — `horizontalPageMargin` for text and icons. Images and graphics go flush to
the edge instead.

**Row heights** — `itemSizeExtraSmall`, `itemSizeSmall`, `itemSizeMedium`, `itemSizeLarge`,
`itemSizeExtraLarge`, `itemSizeHuge`.

**Icons** — `iconSizeExtraSmall`, `iconSizeSmall`, `iconSizeSmallPlus`, `iconSizeMedium`,
`iconSizeLarge`, `iconSizeExtraLarge`, `iconSizeLauncher`.

**Buttons** — `buttonWidthTiny`, `buttonWidthExtraSmall`, `buttonWidthSmall`,
`buttonWidthMedium`, `buttonWidthLarge`.

**Covers** — `coverSizeSmall`, `coverSizeLarge`.

**Type** — `fontSizeTiny`, `fontSizeExtraSmall`, `fontSizeSmall`, `fontSizeMedium`,
`fontSizeLarge`, `fontSizeExtraLarge`, `fontSizeHuge`. Each has a `…Base` twin that ignores the
user's font-size setting; use the plain name unless a layout genuinely cannot reflow.

**Colour** — `primaryColor`, `secondaryColor`, `highlightColor`, `secondaryHighlightColor`,
`highlightBackgroundColor`, `highlightDimmerColor`, `errorColor`, `overlayBackgroundColor`.
Compose transparency with `Theme.rgba(color, opacity)` and the `opacityFaint` / `opacityLow` /
`opacityHigh` / `opacityOverlay` constants.

## When there is no constant

`Theme.dp(size)` converts a design pixel to a device pixel. It is a **method**, not a property:

```qml
Rectangle {
    color: "red"
    anchors.centerIn: parent
    width: Theme.dp(100)
    height: Theme.itemSizeSmall
}
```

`Theme.pixelRatio` is available for branching on density:

```qml
Label {
    font.pixelSize: Theme.pixelRatio > 1.5 ? Theme.fontSizeMedium : Theme.fontSizeSmall
}
```

Reach for `Theme.dp()` only when nothing in the vocabulary above fits. A named constant carries
design intent that a converted number does not.

## Branching on screen size

`Screen.sizeCategory` gives device classes — `Screen.Small` through `Screen.ExtraLarge` — for
adjusting a layout:

```qml
Label {
    text: "Hello world!"
    font.pixelSize: Screen.sizeCategory >= Screen.Large
                    ? Theme.fontSizeLarge : Theme.fontSizeMedium
}
```

Where the layout should be *structurally* different rather than merely bigger, write separate
layout components and pick between them — including at the top level:

```qml
ApplicationWindow {
    initialPage: Screen.sizeCategory >= Screen.Large
                 ? Qt.resolvedUrl("SplitViewPage.qml")
                 : Qt.resolvedUrl("ListViewPage.qml")
}
```

For this to stay maintainable, the pieces each layout is built from have to be reusable
components. If choosing a layout means copy-pasting the screen, the components are too coarse.

## Graphical assets

Three options, cheapest first.

**Ship size variants and pick one.** Simple, and predictable to render:

```qml
Image {
    width: Theme.iconSizeSmall
    height: Theme.iconSizeSmall
    sourceSize.width: width
    sourceSize.height: height
    source: closestMatchingIcon()
}
```

**Ship SVG.** Avoids variants entirely, with three real constraints:

- Qt SVG supports only the static subset of the old SVG 1.2 Tiny standard.
- **Masks are not supported.** Designers use them routinely, so masked paths must be clipped
  before export or they will silently not render.
- Large, complex vector files are expensive to draw.
- One drawing rarely reads well at both small and large sizes.

**Use `SilicaImageProvider`.** The most capable option: give it an icon root with a defined
directory structure and it selects by pixel ratio. It also supports **monochrome icons that
recolour to the theme's primary colour** — shades of white on a dark ambience, shades of black
on a light one — which is the only asset approach that follows the user's ambience
automatically. Setting it up requires the C++ `ImageProvider` class.

Always set `sourceSize` on an `Image` to the displayed size. Without it, Qt decodes at the
source's native resolution.

## Launcher icons

Installed to `/usr/share/icons/hicolor/<size>x<size>/apps/<package-name>.png`. The supported
sizes are **86×86, 108×108, 128×128 and 172×172**.

In a qmake project:

```
SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172
```

The Harbour validator warns on missing sizes rather than failing, but ship all four.

Jolla publishes an icon story PDF and a template ZIP at `sailfishos.org/design/icons/`. The
grid, safe areas and style specifications are in those files rather than on the page — read
them before designing a launcher icon rather than guessing dimensions.
