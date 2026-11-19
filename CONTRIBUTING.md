# Contributing to SCANCORE

Thanks for helping. Bug reports go in [Issues](https://github.com/medium-gh/scancore/issues/new/choose), questions and ideas in [Discussions](https://github.com/medium-gh/scancore/discussions), and quick chat on [Discord](https://scancore.onl/discord). Issues labelled [`good first issue`](https://github.com/medium-gh/scancore/labels/good%20first%20issue) are small and well scoped.

## Prerequisites

- CMake 3.28+, Ninja and a C++20 compiler (GCC 13+, Clang 17+ or MSVC 2022)
- Qt 6.9+ with Qt Declarative, Qt SVG, Qt Image Formats and Qt WebEngine (Qt WebEngine is optional; without it the anime player and browser checks are disabled), plus Qt WebView on Windows and macOS, where the anime player uses the system's web view

- [lexbor](https://github.com/lexbor/lexbor), the HTML parser, is downloaded by CMake. A JDK 17 or newer, if installed, also builds the extension host (`host/`) and its tests.

| System | Packages |
| --- | --- |
| Arch Linux | `sudo pacman -S cmake ninja qt6-base qt6-declarative qt6-svg qt6-imageformats qt6-webengine` |
| Fedora | `sudo dnf install cmake ninja-build qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel qt6-qtimageformats qt6-qtwebengine-devel` |
| Ubuntu, Debian | Needs Qt 6.9 or newer (else the [Qt Online Installer](https://www.qt.io/download-qt-installer)): `sudo apt install cmake ninja-build qt6-base-dev qt6-declarative-dev qt6-svg-dev qt6-image-formats-plugins qt6-webengine-dev` |
| macOS | `brew install cmake ninja qt` |
| Windows | Qt 6.9+ for MSVC 2022 from the [Qt Online Installer](https://www.qt.io/download-qt-installer) (with Qt WebEngine, Qt WebView and Qt Image Formats); build from a *Developer PowerShell for VS 2022* with `-DCMAKE_PREFIX_PATH=C:\Qt\6.x.y\msvc2022_64` |

Build options: `-DSCANCORE_WITH_WEBENGINE=OFF` leaves out the embedded browser (no anime player or browser checks), `-DSCANCORE_WITH_SYSTEM_WEBVIEW=OFF` the system web view player on Windows and macOS, `-DSCANCORE_WITH_BUNDLED_SOURCES=OFF` the bundled sources.

Anime episodes are H.264. Distribution and Flathub builds of Qt WebEngine play it; the one in the Qt Online Installer does not, which is why Windows and macOS use the system's web view. On Linux, build against your distribution's Qt WebEngine, or one configured with `-DFEATURE_webengine_proprietary_codecs=ON` (see `.github/workflows/qtwebengine.yml`).

## Build and run

```bash
cmake --preset debug
cmake --build --preset debug
./build/debug/bin/scancore
```

`SCANCORE_START` opens the app in a given state, which helps with reproducing bugs and taking screenshots:

```bash
SCANCORE_START='{"section":"anime","screenshot":"/tmp/anime.png","delay":8000}' ./build/debug/bin/scancore
```

### Android

Needs the desktop Qt above (as the host), Qt for Android (same version, `android_arm64_v8a`, with Qt WebView and Qt Image Formats), the Android SDK, NDK r27c (`27.2.12479018`) and JDK 17.

```bash
export ANDROID_SDK_ROOT=~/Android/Sdk ANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/27.2.12479018
~/Qt/6.11.2/android_arm64_v8a/bin/qt-cmake -S . -B build/android -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DQT_HOST_PATH=/usr -DANDROID_SDK_ROOT=$ANDROID_SDK_ROOT -DANDROID_NDK_ROOT=$ANDROID_NDK_ROOT
cmake --build build/android --target apk
```

`QT_HOST_PATH` is your desktop Qt prefix (`/usr` on Arch, or something like `~/Qt/6.11.2/gcc_64`). A Debug APK installs without signing; OpenSSL for Android is fetched automatically.

### How it fits together

```
src/core      HTTP (per-site limits, cancellation), HTML parsing, SQLite, image cache, background tasks
src/sources   built-in sources, files on this device, the source manifest
src/app       services the interface uses: Comics, Novels, Anime, Library, Downloads, Extensions, App…
src/qml       the interface: theme, components, pages, phone layout
host/         the extension host (Kotlin) for Mihon and Aniyomi extensions
tests/        offline tests and the live source probe
```

Network requests run on worker pools and are cancelled when you leave a page; chapter pages decode off the interface thread. Everything you keep lives in one SQLite database in the app data folder. Releases are cut with `tools/release.sh` (see [Releasing](#releasing)).

## Test

```bash
ctest --preset debug
```

Parser tests are offline (`tests/tst_parsing.cpp`). To check every built-in source against the live sites:

```bash
./build/debug/tests/scancore_probe comics "solo leveling"
./build/debug/tests/scancore_probe novels "lord of the mysteries"
```

## Where changes belong

| Change | Where |
| --- | --- |
| A site moved domain, broke, or should be disabled | [`sources/manifest.json`](sources/manifest.json): the domain, `enabled` or a template option. Bump the manifest `version` so installed apps pick it up without a new release. |
| A new site on a known template (WordPress MangaReader, Madara, Iken, NovelFull family) | A new entry in `sources/manifest.json` with the matching `engine` |
| A new kind of comic or novel site | An engine in `src/sources/bundled/comics` or `src/sources/bundled/novels`, registered in `createSource`, plus an offline parser test with its fixture in `tests/fixtures/`. The bundled sources are one separable pack ([its README](src/sources/bundled/README.md)): `-DSCANCORE_WITH_BUNDLED_SOURCES=OFF` builds without them |
| Extensions (repositories, the Kotlin host) | `src/extensions`, `host/` ([host/NOTICE.md](host/NOTICE.md) for third-party code) |
| Anime metadata, episodes, skip times, player links | `src/app/Anime.cpp` |
| Interface | `src/qml` (pages in `pages/`, shared pieces in `components/`, colours and sizes in `theme/`) |
| Networking, database, image cache | `src/core` |

Optional manifest fields for comic and novel entries:

| Field | Meaning |
| --- | --- |
| `language` | The content's language as a short code (`"es"`, `"pt"`); default `"en"`. Shown in Settings → Sources and on the comic source chips, and used by the "All sources" language setting. |
| `urlTag` | For a source that shares a host with another (MangaDex in English and in Spanish): its links carry `#<urlTag>`, so a link opens on the source that made it. Leave it out otherwise; adding it to an existing source changes its ids. |
| `minVersion` | The oldest app version that may load the entry, for entries that need new code. Older apps that know this field skip the entry; 1.4.0 and older do not know it. |
| `options` | Engine options, such as `"languages": ["es", "es-la"]` for `mangadex` or `"edition": "es"` for `webtoon`. |

`SCANCORE_PROBE_MANIFEST=/path/to/trial.json scancore_probe comics "query" <key>` checks entries before they go into the manifest. Sources are added only for public pages: nothing behind a login, a paywall or image encryption.

## Translations

Every string in the interface is wrapped in `qsTr()` and translated in each `i18n/scancore_*.ts`. After changing strings, fill in the new entries in every language; a new string should not ship untranslated. A new licence goes into `resources/licenses/` and `components.json` there.

For a source change, include the URL of a series that shows the problem and what you expected to see.

## Releasing

Everything lands on `main`. A release goes out in two steps:

1. Write the `CHANGELOG.md` entry (`## 2.18.0 — 2026-11-19`, a one-line summary under it, then the changes) and the Android stores' notes, `fastlane/metadata/android/en-US/changelogs/<version code>.txt` (major × 10000 + minor × 100 + patch + 105). Then `tools/release.sh prepare 2.18.0 "One line for Linux software centres"` sets the version in `CMakeLists.txt`, the Flatpak manifest's tag and the AppStream notes, and checks the rest, the extension host included. Commit it as `Release SCANCORE 2.18.0: …`.
2. `tools/release.sh publish 2.18.0` pushes that commit to `pre-release`, where [release.yml](.github/workflows/release.yml) builds and starts every package as a test (CI runs the tests beside it). Once it passed, it pushes `main` and the tag `v2.18.0`, and the tag publishes those same packages, signed. Nothing is built twice: CI skips the commit on `main`, and the Flatpak comes from the test build too.

A test build that fails stops it there: fix it on `main`, commit, publish again (a newer push to `pre-release` cancels the test build still running). Every night, the newest passing test build on `pre-release` that is not a release becomes the `nightly` pre-release ([nightly.yml](.github/workflows/nightly.yml)). Each release keeps its Windows and Android symbols as artifacts for 90 days, for reading crash and hang reports.

The extension host ships on its own: raise its version (`Runtime.h`, `Main.kt`, `build.gradle.kts`) and push the tag `extensions-runtime-<version>` before the app release that needs it.

## Pull requests

- Keep each pull request to one change, and match the style of the surrounding code.
- Run `ctest` (and the live probe for source changes) before opening it.
- For interface changes, attach a screenshot. Check the phone layout too: a window narrower than 720 px uses it.
- By contributing you agree that your work is released under the [MIT licence](LICENSE).

## Contributors

Thank you to everyone who has helped build SCANCORE. The picture follows GitHub's list of contributors by itself, so a merged pull request puts your avatar here.

<a href="https://github.com/medium-gh/scancore/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=medium-gh/scancore" alt="SCANCORE's contributors" />
</a>
