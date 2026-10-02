# Testing and diagnostics

This is the thinnest area of the platform. The official *Testing* page carries no commands at
all — it states that behaviour changes should come with unit tests, names `testrunnerlite` and
`tests.xml` as the CI runner, and points at the systemd journal for output. Everything below
that goes beyond that page is read from the installed target, and the gaps are marked as gaps
rather than filled in with guesses.

## Writing the assertions

Two conventions, from the platform coding-conventions page. Both are about what you learn from
a failure.

**`compare()`, not `verify()`.** `verify(value == 10)` fails with "failed". `compare(value, 10)`
fails with both values printed, and does more type checking on the way:

```qml
compare(view.count, 10)          // right
verify(view.count == 10)         // wrong
```

**`SignalSpy`, not an arbitrary `wait()`.** A fixed sleep is wrong in both directions: too short
and the test fails irregularly, which is worse than failing; too long and every run pays for it.

```qml
SignalSpy {
    id: spy
    target: downloader
    signalName: "finished"
}

// in the test function
downloader.start()
spy.wait()                       // right — returns as soon as the signal arrives
// wait(100)                     // wrong — a guess, and a flaky one
```

Style rules for the test code itself are the same as for any other QML — see
`coding-style.md`.

## What the target actually ships

Read from `SailfishOS-5.1.0.11-aarch64`:

| Present | Path |
| --- | --- |
| `QtTest` QML module | `usr/lib64/qt5/qml/QtTest/` |
| QtQuickTest headers | `usr/include/qt5/QtQuickTest/` |
| qmake modules | `usr/share/qt5/mkspecs/modules/qt_lib_qmltest.pri`, `qt_lib_testlib.pri` |
| pkgconfig | `Qt5QuickTest.pc`, `Qt5Test.pc` |
| **Absent** | any `qmltestrunner` binary |

Two consequences.

**The qmake module is `qmltest`, not `quicktest`.** The `.pri` files name the modules, and
`quicktest` is the C++ library name, not the qmake one. So:

```qmake
QT += qmltest      # QML TestCase
QT += testlib      # C++ QTest
```

`QT += quicktest` fails with *"Unknown module(s) in QT"*.

**There is no test runner on the device**, so a QML test suite needs its own executable. The
whole harness is three lines:

```cpp
#include <QtQuickTest/quicktest.h>
QUICK_TEST_MAIN(myapp)
```

Run it with an explicit input directory:

```bash
./harbour-myapp-tests -input /path/to/tests
```

`QUICK_TEST_MAIN` compiles in a fallback directory that is almost certainly not where your
tests ended up. Pass `-input` every time.

**Watch the exit code, not the summary.** `QUICK_TEST_MAIN` passes unrecognised arguments to
QTestLib as a test-function name. A name that matches nothing runs zero tests and still exits
0, and `initTestCase`/`cleanupTestCase` alone print a passing total. A green run that names no
test functions of yours ran nothing.

## Tests during the build

`sfdk check` includes an `rpmspec` suite at unit level that executes the `%check` section of
the RPM spec. That is the supported hook for running tests as part of a build, and it needs no
device:

```bash
sfdk -c target=<target> check -s rpmspec
```

```spec
%check
make check
```

`sfdk check --list-suites` confirms what is available; at 5.1.0.11 it is `harbour`, `rpmlint`
and `rpmspec`.

## Running tests on a device or emulator

The RPM built from the SDK template installs the binary, QML, desktop file and icons — nothing
else. **Test files are not packaged**, so either add them to `%files` deliberately or push them
each run:

```bash
sfdk -c target=<target> -c device="<device>" deploy --rsync
sfdk -c target=<target> device exec -- /usr/bin/harbour-myapp-tests -input /path/to/tests
```

The emulator is the cheaper of the two and enough for anything that is not sensor, camera or
performance related.

## Logs

Application output — `qDebug`, `qWarning`, `qCritical` — goes to the systemd journal, not to a
terminal.

```bash
devel-su journalctl -fa                    # follow everything
devel-su journalctl | grep -i harbour-myapp
devel-su journalctl -a > ~/saved.journal
```

Prefer a time window over a line count. `-n 40` is routinely too small a window to contain the
messages you are looking for, and an empty result then reads as "the app logged nothing"
rather than "I looked at the wrong 40 lines".

Turn up Qt's own logging with environment variables:

```bash
QT_LOGGING_RULES="*.debug=true"     # all Qt categories
GST_DEBUG=element:level             # GStreamer
LIPSTICK_COMPOSITOR_DEBUG=1         # the compositor
```

To keep the journal across reboots, set `Storage=persistent` in `/etc/systemd/journald.conf`
and clear the rate limits (`RateLimitBurst=0`, `RateLimitInterval=0`).

## Debugging

```bash
sfdk -c target=<target> -c device="<device>" debug --args /usr/bin/harbour-myapp
```

This runs the binary under gdb on the device with the SDK's sysroot configured. Two things
make the difference between a useful backtrace and a wall of `?? ()`:

- Build with debug symbols. `sfdk build -d` produces them, but as separate `-debuginfo` and
  `-debugsource` RPMs — the application RPM stays stripped, and those extra packages must not
  be shipped (the Harbour validator rejects them).
- The host and device binaries must match. A rebuilt binary on one side and not the other
  gives a build-ID mismatch and no symbols.

## Where the platform gives you nothing

Stated plainly, because working around a gap is cheaper than rediscovering it:

- **No documented on-device test harness.** `testrunnerlite` and `tests.xml` are named as the
  CI runner in the platform documentation, with no example for an application project.
- **No `qmltestrunner`** in the target, hence the hand-written `QUICK_TEST_MAIN`.
- **No headless QML testing.** Qt 5.6 here has no `offscreen` platform plugin packaged, and
  `minimal` provides no OpenGL for the scene graph. QML tests need a real display — an
  emulator window or a device with the screen on.
- **The official Testing page contains no commands.** Treat any command attributed to it with
  suspicion; the ones above come from the installed target and from `sfdk --help`.
