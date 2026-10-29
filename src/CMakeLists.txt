# Core: networking, HTML, storage and every source adapter. Kept free of QML
# so tests link it directly.
add_library(scancore_core STATIC
    core/Cookies.cpp
    core/Database.cpp
    core/Html.cpp
    core/Migration.cpp
    core/Http.cpp
    core/ImageCache.cpp
    core/Parallel.cpp
    core/QuietSites.cpp
    core/Text.cpp
    core/Epub.cpp
    core/Aes.cpp
    core/translation/Layout.cpp
    core/translation/Cloud.cpp
    core/translation/Onnx.cpp
    core/translation/LocalMt.cpp
    core/StreamRelay.cpp
    core/VideoSave.cpp
    core/UpdateRules.cpp
    core/ThemeLook.cpp
    core/Backups.cpp
    extensions/AnimeSource.cpp
    extensions/Apk.cpp
    extensions/ExtensionSource.cpp
    extensions/Host.cpp
    extensions/Index.cpp
    extensions/LineHost.cpp
    extensions/Manager.cpp
    extensions/Package.cpp
    extensions/ProcessHost.cpp
    extensions/Runtime.cpp
    plugins/Engine.h
    plugins/PluginSource.cpp
    plugins/Runtime.cpp
    sources/Manifest.cpp
    sources/anime/Kitsu.cpp
    sources/anime/Playback.cpp
    sources/anime/Registry.cpp
    sources/catalogue/Catalogue.cpp
    sources/comics/ComicSource.cpp
    sources/comics/Registry.cpp
    sources/local/AudioTags.cpp
    sources/local/Ebooks.cpp
    sources/local/Local.cpp
    sources/local/LocalComics.cpp
    sources/local/LocalNovels.cpp
    sources/local/MihonLayout.cpp
    sources/servers/Servers.cpp
    sources/servers/Opds.cpp
    sources/servers/Jellyfin.cpp
    sources/novels/LnCrawl.cpp
    sources/novels/Registry.cpp
    sources/novels/TextBlocks.cpp
)
if(ANDROID)
    target_sources(scancore_core PRIVATE extensions/JniHost.cpp)
endif()
# The bundled sources pack: every site engine, removable in one go.
if(SCANCORE_WITH_BUNDLED_SOURCES)
    target_sources(scancore_core PRIVATE
        sources/bundled/comics/ApiEngines.cpp
        sources/bundled/comics/PageEngines.cpp
        sources/bundled/comics/EsEngines.cpp
        sources/bundled/novels/Engines.cpp
        sources/bundled/novels/WtrLab.cpp)
    target_compile_definitions(scancore_core PUBLIC SCANCORE_BUNDLED_SOURCES)
endif()
target_include_directories(scancore_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# ONNX Runtime's C API, for the optional on-device translation pack.
target_include_directories(scancore_core SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime)
# LNReader plugins: SCANCORE's shims, and the bundled libraries when present.
set(SCANCORE_PLUGIN_FILES ${PROJECT_SOURCE_DIR}/resources/plugins/prelude.js ${PROJECT_SOURCE_DIR}/resources/plugins/runtime.js)
if(EXISTS ${PROJECT_SOURCE_DIR}/resources/plugins/vendor.js)
    list(APPEND SCANCORE_PLUGIN_FILES ${PROJECT_SOURCE_DIR}/resources/plugins/vendor.js)
endif()
qt_add_resources(scancore_core plugin_runtime
    PREFIX /
    BASE ${PROJECT_SOURCE_DIR}/resources
    FILES ${SCANCORE_PLUGIN_FILES})
# Manifest entries can ask for a minimum app version.
target_compile_definitions(scancore_core PRIVATE SCANCORE_VERSION="${SCANCORE_VERSION_FULL}")
target_link_libraries(scancore_core
    PUBLIC Qt6::Core Qt6::Network Qt6::Sql Qt6::Concurrent Qt6::Qml
    # QZipReader, for CBZ and EPUB files.
    PRIVATE scancore_lexbor Qt6::CorePrivate)

set(SCANCORE_PLATFORM_SOURCES)
if(WIN32)
    list(APPEND SCANCORE_PLATFORM_SOURCES ${PROJECT_SOURCE_DIR}/packaging/windows/scancore.rc)
elseif(APPLE)
    set(SCANCORE_ICNS ${PROJECT_SOURCE_DIR}/packaging/macos/scancore.icns)
    set_source_files_properties(${SCANCORE_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND SCANCORE_PLATFORM_SOURCES ${SCANCORE_ICNS})
endif()

qt_add_executable(scancore WIN32 MACOSX_BUNDLE main.cpp ${SCANCORE_PLATFORM_SOURCES})
set_target_properties(scancore PROPERTIES
    OUTPUT_NAME scancore
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    MACOSX_BUNDLE_GUI_IDENTIFIER com.mediumdev.scancore
    MACOSX_BUNDLE_BUNDLE_NAME SCANCORE
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE scancore.icns
    MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in)
target_compile_definitions(scancore PRIVATE SCANCORE_VERSION="${SCANCORE_VERSION_FULL}"
    SCANCORE_DISCORD_APP_ID="${SCANCORE_DISCORD_APP_ID}"
    SCANCORE_ANILIST_CLIENT_ID="${SCANCORE_ANILIST_CLIENT_ID}"
    SCANCORE_MAL_CLIENT_ID="${SCANCORE_MAL_CLIENT_ID}"
    SCANCORE_COMICK_CLIENT_ID="${SCANCORE_COMICK_CLIENT_ID}"
    SCANCORE_SHIKIMORI_CLIENT_ID="${SCANCORE_SHIKIMORI_CLIENT_ID}"
    SCANCORE_SHIKIMORI_CLIENT_SECRET="${SCANCORE_SHIKIMORI_CLIENT_SECRET}"
    SCANCORE_SIMKL_CLIENT_ID="${SCANCORE_SIMKL_CLIENT_ID}")
if(NOT SCANCORE_WITH_NOVEL_PACK_INSTALLER)
    target_compile_definitions(scancore PRIVATE SCANCORE_DISABLE_NOVEL_PACK)
    target_compile_definitions(scancore_core PRIVATE SCANCORE_DISABLE_NOVEL_PACK)
endif()

file(GLOB_RECURSE SCANCORE_QML RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/*.qml)
if(ANDROID)
    list(REMOVE_ITEM SCANCORE_QML qml/components/WebPlayer.qml qml/components/WebCheckView.qml)
endif()
if(NOT SCANCORE_WITH_SYSTEM_WEBVIEW)
    list(REMOVE_ITEM SCANCORE_QML qml/components/WebPlayerSystem.qml)
endif()
set_source_files_properties(qml/theme/Theme.qml qml/theme/Motion.qml qml/retro/Retro.qml qml/Router.qml qml/Keymap.qml qml/PageLayout.qml qml/VideoPrefs.qml qml/PlayerPage.qml qml/MusicQueue.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(scancore
    URI Scancore
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${SCANCORE_QML}
    SOURCES
        app/Account.cpp app/Account.h
        app/Activity.cpp app/Activity.h
        app/Anime.cpp app/Anime.h
        app/App.cpp app/App.h
        app/Comics.cpp app/Comics.h
        app/Customize.cpp app/Customize.h
        app/Downloads.cpp app/Downloads.h
        app/Extensions.cpp app/Extensions.h
        app/Images.cpp app/Images.h
        app/Importer.cpp app/Importer.h
        app/SingleInstance.cpp app/SingleInstance.h
        app/Announcements.cpp app/Announcements.h
        app/SiteNotifications.cpp app/SiteNotifications.h
        app/Notifications.cpp app/Notifications.h
        app/Music.cpp app/Music.h
        app/AudioPlayer.cpp app/AudioPlayer.h
        app/Platform.cpp app/Platform.h
        app/KeepAwake.cpp app/KeepAwake.h
        app/Profiler.cpp app/Profiler.h
        app/Speech.cpp app/Speech.h
        app/ReadAloud.cpp app/ReadAloud.h
        app/MediaControls.cpp app/MediaControls.h
        app/HomeWidget.cpp app/HomeWidget.h
        app/Translator.cpp app/Translator.h
        app/Trackers.cpp app/Trackers.h app/MoreTrackers.cpp app/TrackerCommon.h
        app/Story.cpp app/Story.h app/Upkeep.cpp app/Upkeep.h
        app/Sync.cpp app/Sync.h
        app/Library.cpp app/Library.h
        app/Live.cpp app/Live.h app/EventStream.h
        app/Party.cpp app/Party.h
        app/Cast.cpp app/Cast.h
        app/CrashLog.cpp app/CrashLog.h
        app/CastProxy.cpp app/CastProxy.h
        app/Lock.cpp app/Lock.h
        app/MpvPlayer.cpp app/MpvPlayer.h
        app/Piper.cpp app/Piper.h
        app/LocalOcr.cpp app/LocalOcr.h
        app/LocalUpscale.cpp app/LocalUpscale.h
        app/OnDevice.cpp app/OnDevice.h
        app/Archive.cpp app/Archive.h
        app/Novels.cpp app/Novels.h
        app/Presence.cpp app/Presence.h
        app/Updater.cpp app/Updater.h
        app/WebBridge.cpp app/WebBridge.h
        core/Task.cpp core/Task.h
)

# The interface in other languages. `cmake --build <dir> --target
# update_translations` refreshes the .ts files from the code.
if(TARGET Qt6::lupdate)
    qt_add_translations(scancore
        TS_FILES
            ${PROJECT_SOURCE_DIR}/i18n/scancore_es.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_fr.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_id.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_pt_BR.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_ko.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_ja.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_zh_CN.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_zh_TW.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_pt_PT.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_de.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_it.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_ru.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_tr.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_vi.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_pl.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_th.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_fil.ts
            ${PROJECT_SOURCE_DIR}/i18n/scancore_hi.ts
        RESOURCE_PREFIX /i18n
        LUPDATE_OPTIONS -no-obsolete -locations none)
else()
    message(STATUS "Qt Linguist tools not found; building without translations")
endif()

file(GLOB SCANCORE_ICONS RELATIVE ${PROJECT_SOURCE_DIR} CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/icons/*.svg)
qt_add_resources(scancore assets
    PREFIX /
    BASE ${PROJECT_SOURCE_DIR}/resources
    FILES
        ${PROJECT_SOURCE_DIR}/resources/fonts/DMSans.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/DMSans-Italic.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/SpaceGrotesk.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/Literata.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/Literata-Italic.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/Selawik.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/Selawik-Light.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/Selawik-Semilight.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/Selawik-Semibold.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/Selawik-Bold.ttf
        ${PROJECT_SOURCE_DIR}/resources/branding/scancore.svg
        ${PROJECT_SOURCE_DIR}/resources/branding/shadow.png
        ${PROJECT_SOURCE_DIR}/resources/shaders/cover.frag.qsb
        ${PROJECT_SOURCE_DIR}/resources/shaders/title.frag.qsb
        ${PROJECT_SOURCE_DIR}/resources/shaders/panel.vert.qsb
        ${PROJECT_SOURCE_DIR}/resources/shaders/panel.frag.qsb)
foreach(icon ${SCANCORE_ICONS})
    list(APPEND SCANCORE_ICON_PATHS ${PROJECT_SOURCE_DIR}/${icon})
endforeach()
qt_add_resources(scancore icons
    PREFIX /
    BASE ${PROJECT_SOURCE_DIR}/resources
    FILES ${SCANCORE_ICON_PATHS})
# Anime4K's shaders for the player (Settings → Anime → Sharpen).
file(GLOB SCANCORE_SHADERS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/shaders/anime4k/*.glsl)
qt_add_resources(scancore anime4k
    PREFIX /
    BASE ${PROJECT_SOURCE_DIR}/resources
    FILES ${SCANCORE_SHADERS})
qt_add_resources(scancore lncrawl
    PREFIX /
    BASE ${PROJECT_SOURCE_DIR}/resources
    FILES
        ${PROJECT_SOURCE_DIR}/resources/lncrawl/sources.json
        ${PROJECT_SOURCE_DIR}/resources/lncrawl/bridge.py)
# Licences shown in Settings → About, and the video player libraries' own
# notices when the libmpv build carries them (tools/libmpv/build.sh).
file(GLOB SCANCORE_LICENCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/licenses/*)
qt_add_resources(scancore licences
    PREFIX /
    BASE ${PROJECT_SOURCE_DIR}/resources
    FILES ${SCANCORE_LICENCES})
if(SCANCORE_WITH_MPV AND MPV_DIR AND EXISTS ${MPV_DIR}/licenses)
    file(GLOB_RECURSE SCANCORE_MPV_LICENCES ${MPV_DIR}/licenses/*)
    qt_add_resources(scancore mpv_licences
        PREFIX /licenses/mpv
        BASE ${MPV_DIR}/licenses
        FILES ${SCANCORE_MPV_LICENCES})
endif()
qt_add_resources(scancore manifest
    PREFIX /
    BASE ${PROJECT_SOURCE_DIR}
    FILES ${PROJECT_SOURCE_DIR}/sources/manifest.json)

target_include_directories(scancore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app ${CMAKE_CURRENT_SOURCE_DIR}/core)

target_link_libraries(scancore PRIVATE
    scancore_core Qt6::Gui Qt6::OpenGL Qt6::Quick Qt6::QuickControls2 Qt6::Svg Qt6::CorePrivate)
if(ANDROID)
    # Qt's network stack on Android needs OpenSSL shipped inside the APK.
    include(FetchContent)
    FetchContent_Declare(android_openssl
        GIT_REPOSITORY https://github.com/KDAB/android_openssl.git
        GIT_TAG b71f1470962019bd89534a2919f5925f93bc5779)
    FetchContent_MakeAvailable(android_openssl)
    include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)
    add_android_openssl_libraries(scancore)
    set_target_properties(scancore PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR ${PROJECT_SOURCE_DIR}/packaging/android
        QT_ANDROID_VERSION_NAME ${SCANCORE_VERSION_FULL}
        QT_ANDROID_VERSION_CODE ${SCANCORE_ANDROID_VERSION_CODE}
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 36
        # The extension host's OkHttp needs 37. Named here because Qt
        # otherwise picks the longest platform name (android-36-ext19).
        QT_ANDROID_COMPILE_SDK_VERSION 37)
else()
    target_link_libraries(scancore PRIVATE Qt6::Widgets)
endif()
# Android: zlib (the NDK's) unpacks ONNX Runtime out of its package
# (app/OnDevice.cpp).
if(ANDROID)
    target_link_libraries(scancore PRIVATE z)
endif()
# What plays, on the system's own now playing (app/MediaControls.cpp):
# Windows' media controls and macOS' Now Playing; Linux has MPRIS (D-Bus,
# below) and Android its media session.
if(WIN32)
    target_sources(scancore PRIVATE app/MediaControls_win.cpp)
    target_link_libraries(scancore PRIVATE windowsapp)
elseif(APPLE AND NOT IOS)
    enable_language(OBJCXX)
    target_sources(scancore PRIVATE app/MediaControls_mac.mm)
    set_source_files_properties(app/MediaControls_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(scancore PRIVATE "-framework MediaPlayer" "-framework AppKit")
endif()
# Keeping the screen on while a video plays (app/KeepAwake.cpp).
if(APPLE)
    target_link_libraries(scancore PRIVATE "-framework IOKit" "-framework CoreFoundation")
elseif(UNIX AND NOT ANDROID)
    find_package(Qt6 6.9 QUIET COMPONENTS DBus)
    if(Qt6DBus_FOUND)
        target_link_libraries(scancore PRIVATE Qt6::DBus)
        target_compile_definitions(scancore PRIVATE SCANCORE_HAVE_DBUS)
    endif()
endif()

if(SCANCORE_WITH_SYSTEM_WEBVIEW)
    target_link_libraries(scancore PRIVATE Qt6::WebView)
    target_compile_definitions(scancore PRIVATE SCANCORE_HAVE_WEBVIEW)
endif()
if(SCANCORE_WITH_WEBENGINE)
    target_link_libraries(scancore PRIVATE Qt6::WebEngineQuick Qt6::WebEngineCore)
    target_compile_definitions(scancore PRIVATE SCANCORE_HAVE_WEBENGINE)
endif()
if(TARGET Qt6::TextToSpeech)
    target_link_libraries(scancore PRIVATE Qt6::TextToSpeech)
    target_compile_definitions(scancore PRIVATE SCANCORE_HAVE_TTS)
endif()
# Neural voices (Piper) come as a .tar.gz on Linux and macOS.
if(NOT ANDROID AND NOT WIN32)
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        target_link_libraries(scancore PRIVATE ZLIB::ZLIB)
        target_compile_definitions(scancore PRIVATE SCANCORE_HAVE_ZLIB)
    endif()
endif()
if(TARGET Qt6::Pdf)
    target_link_libraries(scancore PRIVATE Qt6::Pdf)
    target_compile_definitions(scancore PRIVATE SCANCORE_HAVE_PDF)
endif()
if(SCANCORE_HAVE_EXTENSION_HOST)
    # A development build runs extensions with the host built from host/.
    add_dependencies(scancore scancore_host)
    target_compile_definitions(scancore PRIVATE SCANCORE_HOST_DEV_LIB="${SCANCORE_HOST_LIB_DIR}")
endif()
if(SCANCORE_HAVE_ANDROID_HOST)
    # The APK's Gradle build takes the host from libs/ (build.gradle there).
    target_compile_definitions(scancore PRIVATE SCANCORE_HAVE_ANDROID_HOST)
    set(SCANCORE_APK_LIBS ${CMAKE_CURRENT_BINARY_DIR}/android-build/libs)
    add_custom_command(
        OUTPUT ${SCANCORE_APK_LIBS}/scancore-host.aar ${SCANCORE_APK_LIBS}/scancore-host.deps
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SCANCORE_APK_LIBS}
        COMMAND ${CMAKE_COMMAND} -E copy ${SCANCORE_HOST_APP_DIR}/scancore-host.aar
            ${SCANCORE_HOST_APP_DIR}/scancore-host.deps ${SCANCORE_APK_LIBS}/
        DEPENDS ${CMAKE_BINARY_DIR}/scancore_host_android.stamp
        VERBATIM)
    add_custom_target(scancore_host_libs DEPENDS ${SCANCORE_APK_LIBS}/scancore-host.aar)
    add_dependencies(scancore_host_libs scancore_host)
    add_dependencies(scancore scancore_host_libs)
endif()
# AVIF images (Images.cpp): libavif when there is one; Android decodes them
# with the system (Avif.java), and Qt may also have an image plugin for them.
if(SCANCORE_WITH_AVIF AND NOT ANDROID)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(AVIF QUIET IMPORTED_TARGET libavif)
    endif()
    if(AVIF_FOUND)
        target_link_libraries(scancore PRIVATE PkgConfig::AVIF)
        target_compile_definitions(scancore PRIVATE SCANCORE_HAVE_AVIF)
        message(STATUS "AVIF images: libavif ${AVIF_VERSION}")
    else()
        message(STATUS "AVIF images: libavif not found (only through a Qt image plugin)")
    endif()
endif()
if(SCANCORE_HAVE_MPV)
    target_link_libraries(scancore PRIVATE scancore_mpv)
    target_compile_definitions(scancore PRIVATE SCANCORE_HAVE_MPV)
    if(ANDROID)
        # libmpv and FFmpeg's libraries, packaged beside the app's.
        set_property(TARGET scancore APPEND PROPERTY QT_ANDROID_EXTRA_LIBS ${SCANCORE_MPV_ANDROID_LIBS})
    endif()
endif()

if(NOT ANDROID)
    install(TARGETS scancore
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
