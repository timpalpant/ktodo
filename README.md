# KTodo

[![CI](https://img.shields.io/github/actions/workflow/status/timpalpant/ktodo/ci.yml?branch=master&label=CI&logo=github)](https://github.com/timpalpant/ktodo/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/timpalpant/ktodo?include_prereleases&label=release&logo=github)](https://github.com/timpalpant/ktodo/releases)
[![Flathub](https://img.shields.io/flathub/v/io.github.timpalpant.ktodo?logo=flathub)](https://flathub.org/apps/io.github.timpalpant.ktodo)
[![AUR](https://img.shields.io/aur/version/ktodo?logo=archlinux)](https://aur.archlinux.org/packages/ktodo)
[![Licence](https://img.shields.io/badge/licence-GPL--3.0--or--later-blue)](LICENSE)

A native Todoist client for KDE Plasma, built with Kirigami and Qt Quick.

**Website:** <https://timpalpant.github.io/ktodo/>

> KTodo is an unofficial client. It is not affiliated with, endorsed by, or
> supported by Doist, the makers of Todoist. A Todoist account is required.

> [!NOTE]
> The Flathub and AUR badges will read "not found" until the first release is
> submitted to those registries. Building from source works today.

<!-- Screenshot goes here: docs/screenshots/today.png -->

## Features

- **Offline-first.** Tasks are cached in SQLite and edits are queued locally,
  so the app stays usable without a connection and replays changes on
  reconnect.
- **Projects, sections and sub-tasks**, including sub-projects, with drag and
  drop to reorder tasks and move them between sections.
- **Today, Upcoming, labels and saved filters**, with live counts. Filters are
  evaluated locally and support boolean queries (`&`, `|`, `,`, `!`,
  parentheses), `#project`, `@label`, `/section`, `p1`–`p4`, date terms and
  `assigned to:`.
- **Team projects** with assignees, comment threads and per-role permissions.
- **Scheduling**: due dates, recurring schedules, deadlines and priorities.
  Natural language ("every Monday", "tomorrow at 9am") is parsed by Todoist.
- **Clickable links** in descriptions, from Markdown links and bare URLs.
- **Plasma integration**: KWallet for token storage, KNotifications for
  reminders, Breeze styling and full translation support.

## Installing

### Flatpak

```sh
flatpak install flathub io.github.timpalpant.ktodo
```

### Arch Linux

```sh
yay -S ktodo        # or ktodo-git for the development branch
```

Each release also attaches a prebuilt `*.pkg.tar.zst`, if you would rather not
build locally:

```sh
sudo pacman -U ktodo-*.pkg.tar.zst
```

To build a package from a checkout, before any release exists:

```sh
./packaging/build-arch-package.sh      # -> dist/*.pkg.tar.zst
sudo pacman -U dist/ktodo-*.pkg.tar.zst
```

That script uses the real AUR `PKGBUILD` with its source repointed at the
current commit, so it exercises the same dependency list, build flags and
`check()` step that AUR users get. It is what CI and the release workflow run.

### From source

Requires Qt 6.6+, KDE Frameworks 6 and `kirigami-addons`.

```sh
cmake -B build -G Ninja
cmake --build build
./build/ktodo
```

On Arch:

```sh
pacman -S --needed qt6-base qt6-declarative qt6-networkauth kirigami \
    kirigami-addons ki18n kconfig knotifications kwallet kiconthemes \
    kcoreaddons kwindowsystem cmake ninja
```

`extra-cmake-modules` is not required; each framework is located individually
so the build works on distributions without the KF6 umbrella package.

## Setup

Install the KTodo integration into your Todoist account, then sign in from the
app:

**[Add KTodo to your Todoist account](https://app.todoist.com/app/install/153685_ddc8fa128ea8fc273e0d9c2d)**

Access can be revoked at any time from Todoist's *Settings → Integrations →
Connected apps*.

## Development

```sh
cmake -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The test suite covers the parts with real logic and no GUI or network
dependency: the saved-filter evaluator, the due-date helpers, the Todoist
colour palette and the rich-text renderer. It is headless and touches neither
the network nor a Todoist account, so it is safe to run anywhere.

```sh
cmake --build build --target all_qmllint   # expected to be silent
clang-format -i $(git ls-files '*.cpp' '*.h')
```

A local build picks up `KTODO_CLIENT_ID` and `KTODO_CLIENT_SECRET` from a `.env`
in the source tree at configure time, so a development build needs no `-D`
flags. This happens only during the build; the application itself never reads
`.env`, and credentials always come from the environment, `~/.config/ktodorc`,
or the values compiled in.

`.qmllint.ini` disables only `UnqualifiedAccess`, because the `i18n*` functions
are injected into the QML context at runtime and cannot be resolved statically.
Every other check stays on, and CI treats warnings as failures.

## Packaging and CI

| Path | What it is |
| --- | --- |
| `.github/workflows/ci.yml` | Build and test on Arch, qmllint, AppStream and desktop-entry validation, clang-format, PKGBUILD lint, Flatpak build |
| `.github/workflows/release.yml` | On a `v*` tag: verifies the tag against the project and AppStream versions, then builds the source tarball, the Arch package and the Flatpak bundle and drafts a release |
| `.github/workflows/pages.yml` | Deploys `docs/` to GitHub Pages on the same tag |
| `packaging/flatpak/io.github.timpalpant.ktodo.yml` | Flathub manifest, builds a tagged release |
| `packaging/flatpak/io.github.timpalpant.ktodo.ci.yml` | Same, but builds the working tree; used by CI and the release |
| `packaging/aur/ktodo/PKGBUILD` | AUR release package |
| `packaging/aur/ktodo-git/PKGBUILD` | AUR development package |
| `packaging/build-arch-package.sh` | Builds a `*.pkg.tar.zst` from a checkout using the AUR PKGBUILD |
| `packaging/fill-credentials.sh` | Stamps the OAuth client id and secret into the packaging files |
| `docs/` | The website |

The app id is `io.github.timpalpant.ktodo`, matching the project's GitHub Pages
domain as Flathub requires. Submit the Flatpak manifest to
[flathub/flathub](https://github.com/flathub/flathub) as a pull request against
the `new-pr` branch. KDE Discover lists Flathub applications, so no separate
submission is needed. Flathub requires at least one screenshot; the URLs in the
metainfo point at `docs/screenshots/`.

### Cutting a release

1. Bump `project(ktodo VERSION ...)` in `CMakeLists.txt` and `pkgver` in
   `packaging/aur/ktodo/PKGBUILD`.
2. Add a `<release version="X.Y.Z">` entry to the AppStream metainfo, and bump
   the `tag:` in the Flathub manifest.
3. Push a `vX.Y.Z` tag.

The release workflow refuses the tag unless it matches the version in
`CMakeLists.txt` and has a matching `<release>` entry, then builds three
artifacts — source tarball, Arch package, Flatpak bundle — each with a
`.sha256`, and drafts a GitHub release. It also prints the `sha256sums=(...)`
line for the AUR PKGBUILD; paste that in before publishing the draft.

The website deploys from the same tag.

### OAuth credentials in a package

A desktop application is a *public client* under
[RFC 8252](https://datatracker.ietf.org/doc/html/rfc8252) and cannot keep a
secret: anything compiled into the binary or written into a manifest is
recoverable. The client id and secret identify the build, not the user, and
grant no access on their own — a token is issued only after the user approves
it in a browser, and only against the registered redirect URI.

Two consequences are worth understanding. Anyone holding the pair can publish
an app that shows your registered name on the consent screen, and rate limits
and revocation are per-client, so abuse affects every user of the package.

Credentials live in `.env`, which is gitignored, and are stamped into the
packaging files when cutting a release:

```sh
packaging/fill-credentials.sh           # from .env, or from the environment
packaging/fill-credentials.sh --check   # report which files are filled
packaging/fill-credentials.sh --clear   # restore the placeholders
```

CI fails if a filled-in credential is ever committed. Leaving the placeholders
empty is also valid: the app then asks the user to supply their own
registration.

The sign-in flow uses PKCE (S256). Whether Todoist enforces the verifier at
token exchange is not observable from a client, so this is protection only if
the server honours it.

### Why the release PKGBUILD never uses `SKIP`

`sha256sums=('SKIP')` is correct for `ktodo-git`, whose source is a git
checkout that git verifies itself. For the release package it would mean
makepkg builds whatever the download happened to return, so the tarball is
pinned by digest instead, and CI fails the build if that is ever weakened back
to `SKIP`.

The digest covers the tarball uploaded by the release workflow, not GitHub's
auto-generated "Source code" links: those are produced on demand and are not
byte-stable, so they cannot be pinned.

## Licence

GPL-3.0-or-later.

KTodo is an unofficial client and is not affiliated with, endorsed by, or
supported by Doist, the makers of Todoist.
