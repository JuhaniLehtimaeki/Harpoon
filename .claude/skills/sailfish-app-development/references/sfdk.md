# sfdk

`sfdk` is the command-line frontend to the Sailfish SDK. Everything the IDE does, it does.
Verified against SDK 3.13.5.

Its own manual is the best reference and is installed locally:

```bash
sfdk --help-all          # everything, one page
sfdk --help-building     # building domain
sfdk --help-testing      # testing domain
sfdk <command> --help    # one command, as a man page
```

## The shape of the system

- **Build engine** — a VM (VirtualBox) or container (Docker) holding the cross toolchains.
  Docker builds run roughly 30–40% faster and, on Linux, close to native.
- **Build targets** — one per OS version and architecture, e.g.
  `SailfishOS-5.1.0.11-aarch64`. `sfdk tools list` shows them.
- **Emulator** — an x86 VM. Needs VirtualBox even when the build engine is Docker.
- **Devices** — real hardware, registered once through the IDE.

Targets are unpacked on the host under
`~/SailfishOS/mersdk/targets/<target>.default/`, which is a readable sysroot — the Silica QML
source, headers and libraries are all there without entering the engine.

## Configuration has three scopes, and one of them is a trap

Configuration is a stack. From outermost to innermost:

| Scope | Set with | Lifetime |
| --- | --- | --- |
| global | `sfdk config --global --push <name> <value>` | persists indefinitely |
| session | `sfdk config --push <name> <value>` (the default) | **the current shell session only** |
| command | `sfdk -c <name>=<value> …` | that one invocation |

Inner scopes mask outer ones. `sfdk config --show` prints all three.

**Session scope does not survive into a new shell.** Verified: setting `target` at session
scope and then running `sfdk config --show` from a separate shell shows it cleared. Anything
that runs each command in a fresh shell — a script, a CI step, an agent making one tool call
per command — loses it every time.

So there are exactly two reliable ways to fix a target:

```bash
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build      # per command, always correct
sfdk config --global --push target SailfishOS-5.1.0.11-aarch64   # once, persists
```

Prefer `-c` when the target is a property of the task, and `--global` when the machine only
ever builds for one target. Do not rely on a bare `sfdk config target=…` from an earlier
command having stuck.

Shorthand: `<name>=<value>` means `--push`, `<name>=` means `--push-mask` (mask without
setting), `--drop` removes.

## Commands

**Project**

- `sfdk init -t <type> [-b <builder>] [<name>]` — new project. `--list-types` gives
  `qtquick2app` (builders `qmake`, `cmake`) and `qtquick2app-qmlonly` (`qmake`).
- `sfdk build-init` — create the build directory. `build`, `qmake` and `cmake` do this
  implicitly; `build-shell` does not.

**Building**

- `sfdk build [<dir>]` — every step at once: deps, configure, compile, package. RPMs land in
  `RPMS/`.
- `sfdk qmake` / `sfdk cmake` / `sfdk make` / `sfdk make-install` / `sfdk package` — the same
  steps individually, for iterating without repackaging.
- `sfdk build-shell <command>` — run an arbitrary command inside the target's build
  environment. This is how you cross-compile something the build steps do not cover.
- `sfdk build-requires` — update build-time dependencies.
- `sfdk prepare` / `sfdk apply` / `sfdk scrape` — source and patch handling driven by the spec.
- `sfdk compiledb` — emit `compile_commands.json`, which is what makes clangd work on a
  cross-compiled project.
- `sfdk qmltypes` — regenerate `.qmltypes` for QML modules.
- `sfdk changelog` — extract a changelog from the git commit log.

**Deploying**

```
sfdk deploy {--pkcon|--rsync|--sdk|--zypper|--zypper-dup|--manual} [--all] [--debug] [-n]
```

- `--pkcon` — install through PackageKit on the device. The normal choice.
- `--zypper` / `--zypper-dup` — zypper install or distribution-upgrade semantics.
- `--rsync` — copy files without packaging; fastest for iterating on QML.
- `--sdk` — installs under `/opt/sdk` rather than `/usr`, so the app does not collide with an
  installed copy. Binaries then need an rpath that finds their libraries from there.
- `--manual` — just transfer the RPMs; you install them yourself.
- `-n` / `--dry-run` shows what would happen. `sfdk undeploy` reverses it.

**Devices and emulators**

- `sfdk device list` — registered devices, including emulators.
- `sfdk device exec [<name>] [--] <command>` — run a command on the device over SSH.
- `sfdk emulator …` and `sfdk engine …` — start, stop and `exec` into the emulator and build
  engine. `sfdk engine exec` gets you a shell inside the build engine.

**Checking and debugging**

- `sfdk check [<rpm>…]` — quality checks. `--list-suites` names them; at 5.1.0.11 there are
  three, all Essential:

  | Suite | Level | What it does |
  | --- | --- | --- |
  | `harbour` | package | The Jolla Harbour intake validator |
  | `rpmlint` | package | Common packaging problems |
  | `rpmspec` | unit | Runs the `%check` section of the RPM spec |

  `-s/--suites` and `-l/--levels` select among them. Note that `rpmspec` means the spec's
  `%check` section is a real, supported place to run unit tests during a build.

- `sfdk debug [<device>] --args <remote-binary>` — run under gdb on the device.

**Targets and tooling**

- `sfdk tools list` — installed targets and toolings.
- `sfdk tools update <target>`, `sfdk tools exec <target> <command>`.
- `sfdk maintain` — the interactive SDK maintenance tool.

## Behaviours worth knowing before they bite

**The workspace restriction.** `sfdk` refuses to work outside its configured workspace,
which defaults to `$HOME`:

```
The command needs to be used under Sailfish SDK workspace, which is currently
configured as "/home/<user>"
```

Either keep projects under the workspace or move it: `sfdk config workspace=<dir>`.

**Almost everything needs a target.** Even `sfdk check --list-suites` fails with *"The
required configuration option 'target' is not set"*. When a command errors in a way that
mentions configuration, the target is the first thing to check.

**One build at a time.** The SDK's own Known Issues state that simultaneous builds fail,
because package creation collides. Serialise them.

**`sfdk tools list --available` hangs** when SDK updates are pending. Plain `sfdk tools list`
is fine.

**Build engine connection timeouts** are usually a missing hosts entry. Add `SailfishSDK` to
`/etc/hosts` on the loopback line.

**Docker deployment to the emulator failing** usually means the `iptable_nat` kernel module is
not loaded, or a firewall is blocking Docker's bridge interface
(`br-sfdk-<username>`).

**Docker images accumulate** — each engine start/stop adds a layer. Reinstalling the SDK is
the supported cleanup.

**Whitespace in paths breaks things.** Both the installer and projects. Keep paths plain.

**Don't pick the "Desktop" kit** in the IDE for a Sailfish project; it will look usable and is
not.

To debug the engine itself:

```bash
QT_LOGGING_RULES=sfdk.queue.debug=true sfdk engine start
sfdk engine exec cat /var/log/systemboot.log
```
