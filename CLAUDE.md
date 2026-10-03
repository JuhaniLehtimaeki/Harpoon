# Working on Harpoon

- Work on `main` only: commit and push to `main`. Do not create other branches or pull
  requests unless asked.
- Releases are made by pushing a version tag (see docs/packaging.md); the maintainer pushes
  tags.
- The phone runs Qt 5.6: use only Qt 5.6 APIs, ES5 QML and `onFoo:` handlers in Connections.
- Before pushing: build with `-Wall -Wextra -Werror` and run `ctest`.
