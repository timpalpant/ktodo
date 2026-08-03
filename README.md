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

Each release attaches a prebuilt `*.pkg.tar.zst`, if you would rather not
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

## Licence

GPL-3.0-or-later.

KTodo is an unofficial client and is not affiliated with, endorsed by, or
supported by Doist, the makers of Todoist.
