# Coding style

How Sailfish code is *written*, as distinct from what it does. Almost none of this is caught by
a compiler or by `sfdk check` — it is the layer a reviewer sees.

Read from `docs.sailfishos.org/Develop/Apps/Coding_Conventions/` on 2026-09-11, then checked
against first-party code — the 168 QML files of the Silica source at 5.1.0.11 and the 41 in
`~/SailfishOS/examples/`. The semicolon, spacing and default-value rules hold there without
exception, and `_`-prefixed private properties appear 418 times. The grouping rule does not
hold; see below. Where a rule and the platform's own code disagree, this file says so rather
than picking one.

The page is written for contributors to Jolla's own projects. Its naming *style* applies to a
Harbour app; its paths do not — see the note at the end.

## Qt first, Sailfish on top

Sailfish does not restate the Qt conventions, it inherits them. These four are the base layer:

- [Qt Coding Style](https://wiki.qt.io/Qt_Coding_Style) — indentation, braces, naming
- [Qt C++ Conventions](https://wiki.qt.io/Coding_Conventions)
- [Qt API Design Principles](https://wiki.qt.io/API_Design_Principles)
- [QML Coding Conventions](https://doc.qt.io/qt-5/qml-codingconventions.html)

Everything below is a Sailfish *delta* on those. Where this file is silent, the Qt document
governs.

## QML

| Rule | Wrong | Right |
| --- | --- | --- |
| No semicolons after JavaScript statements | `x = a + b;` | `x = a + b` |
| Space after `if`, space before the brace | `if(a){` / `if ( a ) {` | `if (a) {` |
| Omit a property's default value | `property bool loading: false` | `property bool loading` |
| No unused `id` | `Image { id: background }`, never referenced | `Image { }` |
| Brace a conditional body | `if (a)`<br>`    doSomething()` | `if (a) { doSomething() }` |
| Group *several* properties of the same object | `anchors.top: …`<br>`anchors.left: …`<br>`anchors.margins: …` | `anchors { top: …; left: …; margins: … }` |
| Prefer the positive conditional | `if (!enabled) { … } else { … }` | `if (enabled) { … } else { … }` |
| Underscore marks private | `property int cachedIndex` | `property int _cachedIndex` |

Three of these are worth a sentence each.

**Omitting the default** is not just brevity. `property bool loading: false` reads as a
deliberate initial value; `property bool loading` says the default is whatever the type's
default is. Writing the redundant one buries the cases where the initial value actually matters.

**The underscore** is the only privacy QML has. There is no access control — an `_`-prefixed
property is as reachable as any other. It is a message to the next reader that this is not
part of the component's contract and may change.

**Grouping is for multiples, not for one property.** The conventions page states the grouping
rule flatly, but the platform's own code does not read it that way. In the Silica QML source at
5.1.0.11, ungrouped `anchors.*` outnumbers grouped `anchors { }` 142 to 56, and the ungrouped
ones are dominated by the single-property idioms — `anchors.fill: parent` (37),
`anchors.centerIn: parent` (19). The SDK examples run the same way, 149 to 15. `font.*` matches
it, 49 to 8.

So: one property stays flat, two or more get grouped. That is the measured first-party
practice, not a quoted rule — the page itself does not draw the line. Regenerate the counts if
you want to re-check a later release:

```bash
S=~/SailfishOS/mersdk/targets/<target>.default/usr/lib64/qt5/qml/Sailfish/Silica
grep -rn --include=*.qml -E '^\s+anchors\.[a-zA-Z]+:' $S | wc -l
grep -rn --include=*.qml -E '^\s+anchors\s*\{'       $S | wc -l
```

### Short handlers stay on one line

A handler short enough to fit on one line stays on one line, unbraced:

```qml
onClicked: if (enabled) activate()
```

This coexists with the brace rule above, which governs a body that spans lines. The page states
both and does not say where the boundary sits. Treat it as: *one line, one statement, no
braces; anything more, braces*. That is a reading, not a quoted rule.

### Declaration order

One order, top to bottom, in every component:

```qml
ListItem {
    id: root                                  // 1. id

    property string title                     // 2. property declarations
    signal activated()                        // 3. signals

    width: parent.width                       // 4. bindings and handlers
    onClicked: root.activated()

    Label {                                   // 5. child items
        text: root.title
    }

    states: [ … ]                             // 6. states
    transitions: [ … ]                        // 7. transitions
}
```

The point is that a reader looking for the component's interface — what it takes, what it emits
— finds it in the first few lines, without scrolling past layout.

## C++

**Use the Qt 5 connect syntax.** Function pointers, not the `SIGNAL`/`SLOT` macros:

```cpp
QObject::connect(&sender, &Sender::signalName, &receiver, &Receiver::slotName);   // right
QObject::connect(&sender, SIGNAL(signalName()), &receiver, SLOT(slotName()));     // wrong
```

The macros resolve at runtime, so a parameter mismatch is a silent no-op you find by noticing
the slot never fired. The function-pointer form makes the compiler catch it, and is marginally
faster.

**Use ranged-for.** The platform compiler supports C++11:

```cpp
for (const auto &str : someStringList) { … }                    // right

for (int i = 0; i < someStringList.size(); ++i) {               // wrong
    const QString &str(someStringList[i]);
    …
}
```

**CamelCase namespaces**, matching Qt's class naming:

```cpp
namespace AppNamespace { … }      // right
namespace appnamespace { … }      // wrong
```

## Where this leaves Harbour

The source page's project hierarchy is written for Jolla's own repositories: `sailfish-appname`
directories, data under `/usr/share/sailfish-appname`, QML modules installed into
`/usr/lib/qt5/qml/Sailfish/ModuleName/`.

**A Harbour app cannot use any of those paths.** The package name must match
`^harbour-[-a-z0-9_.]+$`, data lives at `/usr/share/$NAME/`, and `/usr/lib/qt5/qml/Sailfish/`
belongs to the platform. See the `jolla-store-rules-check` skill.

What does carry over is the naming style, and it is already what the SDK template produces:
lowercase, dash-separated, the `.pro` filename matching the project folder, and the root QML
and entry-point source both named for the app. See `project-layout.md`.
