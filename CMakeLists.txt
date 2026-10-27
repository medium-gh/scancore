cmake_minimum_required(VERSION 3.28)

project(SCANCORE
    VERSION 2.16.3
    DESCRIPTION "Read comics and novels and watch anime, entirely on your own machine"
    HOMEPAGE_URL "https://github.com/medium-gh/scancore"
    LANGUAGES C CXX)

# A pre-release label ("beta.1") makes the app 2.0.0-beta.1, tagged
# v2.0.0-beta.1 and published as a GitHub pre-release, which installed copies
# never take as an update; empty for a release.
set(SCANCORE_PRERELEASE "")
set(SCANCORE_VERSION_FULL ${PROJECT_VERSION})
if(SCANCORE_PRERELEASE)
    string(APPEND SCANCORE_VERSION_FULL "-${SCANCORE_PRERELEASE}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Sources are UTF-8 (titles and language names in many scripts); MSVC would
# otherwise read them in the system code page.
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SCANCORE_WITH_WEBENGINE "Embedded web players (ReAnime) and site verification windows" ON)
option(SCANCORE_WITH_SYSTEM_WEBVIEW "Play anime in the system web view (WebView2 on Windows, WKWebView on macOS)" ON)
option(SCANCORE_WITH_MPV "Native video playback through libmpv (anime extensions; off: web players only)" ON)
option(SCANCORE_WITH_AVIF "AVIF covers and pages through libavif when it is found (Android uses the system's decoder)" ON)
option(SCANCORE_WITH_NOVEL_PACK_INSTALLER "Allow downloading the optional Python novel-source pack" ON)
option(SCANCORE_WITH_BUNDLED_SOURCES "Built-in site sources (src/sources/bundled, listed in sources/manifest.json); off: extensions and files only" ON)
option(SCANCORE_BUILD_TESTS "Build unit tests" ON)
option(SCANCORE_WITH_EXTENSION_HOST "Build the Kotlin extension host: for development and tests on desktop, inside the APK on Android (needs JDK 17+)" ON)
# Public ids of SCANCORE's registered apps (not secrets: the sign-ins use
# PKCE or show a token, as every desktop and phone app must); features that
# need one stay hidden without it.
set(SCANCORE_DISCORD_APP_ID "" CACHE STRING "Discord application id, for Rich Presence")
set(SCANCORE_ANILIST_CLIENT_ID "54204" CACHE STRING "AniList API client id, for tracking")
set(SCANCORE_MAL_CLIENT_ID "d85f703db50aa4add685b1f65eb8ad94" CACHE STRING "MyAnimeList API client id, for tracking")
set(SCANCORE_COMICK_CLIENT_ID "DKZVIvYfXDqpWlEEBNfXDMqFKlAPsfaz" CACHE STRING "Comick public app client id, for tracking and imports")
# Shikimori needs a confidential app (id and secret, both shipped like
# Mihon's); Simkl only the id (PIN sign-in). Empty hides that tracker.
set(SCANCORE_SHIKIMORI_CLIENT_ID "" CACHE STRING "Shikimori OAuth application id, for tracking")
set(SCANCORE_SHIKIMORI_CLIENT_SECRET "" CACHE STRING "Shikimori OAuth application secret, for tracking")
set(SCANCORE_SIMKL_CLIENT_ID "" CACHE STRING "Simkl API client id, for tracking")

find_package(Qt6 6.9 REQUIRED COMPONENTS
    Core Gui Network Sql Qml Quick QuickControls2 Svg Concurrent OpenGL)
# QZipReader (CBZ and EPUB files) lives in Qt Core's private API. Every
# package ships the Qt it was built with, so the version tie is no risk.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 REQUIRED COMPONENTS CorePrivate)
qt_standard_project_setup(REQUIRES 6.8)

if(ANDROID)
    # Phones: the built-in anime servers use the system WebView instead of
    # Qt WebEngine, and there is no tray icon or Widgets.
    set(SCANCORE_WITH_WEBENGINE OFF)
    set(SCANCORE_BUILD_TESTS OFF)
    find_package(Qt6 REQUIRED COMPONENTS WebView)
    set(SCANCORE_WITH_SYSTEM_WEBVIEW ON)
else()
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
endif()

# Anime episodes are H.264 video. The Qt WebEngine in Qt's own packages is
# built without that codec, so on Windows and macOS the player uses the
# system's web view (Qt WebView), which has it. Linux has no system web view;
# there the player needs a Qt WebEngine built with proprietary codecs, as
# distribution and Flathub packages are.
if(NOT ANDROID)
    if(SCANCORE_WITH_SYSTEM_WEBVIEW AND (WIN32 OR APPLE))
        find_package(Qt6 6.9 QUIET COMPONENTS WebView)
        if(NOT Qt6WebView_FOUND)
            message(WARNING "Qt WebView not found; anime will use Qt WebEngine, which may lack the H.264 codec")
            set(SCANCORE_WITH_SYSTEM_WEBVIEW OFF)
        endif()
    else()
        set(SCANCORE_WITH_SYSTEM_WEBVIEW OFF)
    endif()
endif()

# PDF files on this device (Settings → Sources), drawn by Qt PDF.
find_package(Qt6 6.9 QUIET COMPONENTS Pdf)
# Reading novels aloud with the system's voices: Qt Speech on desktop. On
# Android the app speaks through Android's own engine (Speech.java), which
# spares the APK Qt Multimedia and its FFmpeg backend.
if(NOT ANDROID)
    find_package(Qt6 6.9 QUIET COMPONENTS TextToSpeech)
endif()
# Translations (i18n/); without Qt's Linguist tools the app is English only.
find_package(Qt6 6.9 QUIET COMPONENTS LinguistTools)

if(SCANCORE_WITH_WEBENGINE)
    find_package(Qt6 6.9 QUIET COMPONENTS WebEngineQuick WebEngineCore)
    if(NOT Qt6WebEngineQuick_FOUND)
        message(WARNING "Qt WebEngine not found; building without embedded web players")
        set(SCANCORE_WITH_WEBENGINE OFF)
    endif()
endif()

# Keep Android's numeric version code above the previously shipped 1.1.4 build
# (10104) even though the public version is being reset to 1.0.0. The offset
# also keeps subsequent 1.0.x releases increasing monotonically.
math(EXPR SCANCORE_ANDROID_VERSION_CODE
    "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH} + 105")
# A pre-release sits just below its release, so the release installs over it.
if(SCANCORE_PRERELEASE)
    math(EXPR SCANCORE_ANDROID_VERSION_CODE "${SCANCORE_ANDROID_VERSION_CODE} - 1")
endif()

include(GNUInstallDirs)
include(cmake/Dependencies.cmake)
include(cmake/ExtensionHost.cmake)

add_subdirectory(src)

if(SCANCORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/Packaging.cmake)
