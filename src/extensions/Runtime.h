#pragma once

#include <QString>

#include <functional>

namespace sc::ext {

// The Extensions runtime (desktop): a trimmed Java and the extension host,
// downloaded on request (about 100 MB) from the extensions-runtime-<version>
// pre-release of SCANCORE's repository, one zip per system with a SHA-256
// beside it. The app needs the host version it was built with: raise it
// (here, in host/desktop's Main.kt and build.gradle.kts) whenever host/
// changes, and publish that runtime before the app, or installed copies
// keep running the old host.
class Runtime {
public:
    static constexpr const char *HostVersion = "10";

    // Where it is installed (<app data>/extensions/runtime).
    static void setDir(const QString &dir);
    static QString dir();
    // This system as the zips name it ("linux-x64"); empty where there is
    // no runtime (Android, other CPUs).
    static QString platform();
    static bool installed();
    // A runtime for another host version is installed: it still runs, and
    // the app replaces it with its own.
    static bool outdated();
    // The asset and checksum links; the base can be changed for tests.
    static void setDownloadBase(const QString &base);
    static QString assetUrl();

    // Downloads, checks and unpacks it, replacing an older one. Blocking;
    // `status` hears what it is doing. Throws on any failure, leaving what
    // was installed in place. `beforeSwap` runs once the new one is
    // unpacked, just before it replaces the old (stop the host there).
    static void install(const std::function<void(const QString &)> &status = {}, const std::function<void()> &beforeSwap = {});
    static void remove();

    // Android: CloudStream's runtime (its library as one dex in a jar,
    // about 4 MB), which the host loads with the first CloudStream plugin;
    // asset SCANCORE-cloudstream-runtime-<HostVersion>-android.jar of the
    // same release. Downloaded and checked when a CloudStream plugin is
    // installed; nothing elsewhere (desktop runtimes have it). Echo's the
    // same way (SCANCORE-echo-runtime-<HostVersion>-android.jar).
    static QString cloudStreamPath();
    static void installCloudStream();
    static QString echoPath();
    static void installEcho();

private:
    static QString pluginRuntimePath(const QString &name);
    static void installPluginRuntime(const QString &name, const QString &label);
};

} // namespace sc::ext
