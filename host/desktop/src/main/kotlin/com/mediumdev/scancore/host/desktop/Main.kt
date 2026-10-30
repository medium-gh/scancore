// SCANCORE's extension host for Windows, macOS and Linux: started by the app
// on first use, it runs Mihon extensions and answers over stdin/stdout (one
// JSON object per line, see core/Rpc.kt).
//
//   scancore-host --root <folder> [--user-agent <ua>] [--app-version <v>]
package com.mediumdev.scancore.host.desktop

import android.app.Application
import android.content.Context
import android.os.Looper
import com.mediumdev.scancore.host.CloudStream
import com.mediumdev.scancore.host.Echo
import com.mediumdev.scancore.host.ExtensionHost
import com.mediumdev.scancore.host.MemoStore
import com.mediumdev.scancore.host.RpcServer
import eu.kanade.tachiyomi.AppInfo
import eu.kanade.tachiyomi.network.HostNetwork
import eu.kanade.tachiyomi.network.JavaScriptEngine
import eu.kanade.tachiyomi.network.NetworkHelper
import eu.kanade.tachiyomi.network.PersistentCookieJar
import kotlinx.serialization.json.Json
import kotlinx.serialization.protobuf.ProtoBuf
import uy.kohesive.injekt.Injekt
import uy.kohesive.injekt.api.addSingleton
import uy.kohesive.injekt.api.addSingletonFactory
import xyz.nulldev.androidcompat.AndroidCompat
import xyz.nulldev.androidcompat.AndroidCompatInitializer
import java.io.File
import java.io.OutputStreamWriter
import java.io.PrintStream
import java.util.concurrent.CountDownLatch
import kotlin.system.exitProcess

const val HOST_VERSION = "10"

fun main(args: Array<String>) {
    val options = args.toList().zipWithNext().filter { it.first.startsWith("--") }.associate { it.first.removePrefix("--") to it.second }
    val root = File(options["root"] ?: run { System.err.println("--root is required"); exitProcess(2) })
    root.mkdirs()

    // The protocol owns stdout; anything an extension prints goes to stderr.
    val protocol = OutputStreamWriter(System.out, Charsets.UTF_8)
    System.setOut(PrintStream(System.err, true, "UTF-8"))

    val (host, memos) = setUp(root, options["user-agent"], options["app-version"])
    val server = RpcServer(host::handle)
    HostNetwork.browserCheckSolver = AppBrowserCheck(server)
    HostNetwork.readerBrowserCheck = com.mediumdev.scancore.host.ReaderBrowserCheck(server)
    server.serve(System.`in`.bufferedReader(Charsets.UTF_8), protocol)
    memos.save()
    exitProcess(0)
}

// Everything an extension expects of the app: Android stand-ins, Injekt's
// objects, the network with SCANCORE's identity and cookies. Once per
// process.
fun setUp(
    root: File,
    userAgent: String?,
    appVersion: String?,
): Pair<ExtensionHost, MemoStore> {
    System.setProperty("scancore.host.root", root.absolutePath)
    userAgent?.let { HostNetwork.userAgent = it }
    HostNetwork.cookieJar = PersistentCookieJar(File(root, "cookies.json"))
    HostNetwork.cacheDir = File(root, "cache")
    // Offline tests: fixture sites on *.test are served from this machine.
    if (System.getenv("SCANCORE_HOST_TEST_LOOPBACK") == "1") HostNetwork.dns = HostNetwork.loopbackForTestDomains
    AppInfo.set(versionCode = 1, versionName = appVersion ?: "SCANCORE")
    startMainThread()

    AndroidCompatInitializer().init(HostNetwork.userAgent)
    // Web views run in Chromium (downloaded on first use).
    android.webkit.WebView.setProviderFactory { view -> CefRuntime.providerFor(view) }
    val app = Application()
    AndroidCompat().startApp(app)

    Injekt.addSingleton<Application>(app)
    Injekt.addSingleton<Context>(app)
    Injekt.addSingletonFactory { NetworkHelper(app) }
    Injekt.addSingletonFactory { JavaScriptEngine(app) }
    Injekt.addSingletonFactory {
        Json {
            ignoreUnknownKeys = true
            explicitNulls = false
        }
    }
    Injekt.addSingletonFactory<ProtoBuf> { ProtoBuf }

    // CloudStream's runtime: lib/cloudstream beside the host's own jars
    // (lib/host) in the Extensions runtime.
    CloudStream.runtimeClasses = pluginClasses("cloudstream")
    Echo.runtimeClasses = pluginClasses("echo")

    val memos = MemoStore(File(root, "memos.json"))
    val host = ExtensionHost(ApkLoader(File(root, "jars")), memos, HOST_VERSION, CompatSettings(app))
    Runtime.getRuntime().addShutdownHook(Thread { memos.save() })
    return host to memos
}

// Android's main thread: extensions make Handlers on Looper.getMainLooper()
// (Komga at construction) and post toasts or web-view work to it (#20).
// Calls still run on the RPC workers, so myLooper() stays null there, as on
// a background thread on Android.
private fun startMainThread() {
    if (Looper.getMainLooper() != null) return
    val ready = CountDownLatch(1)
    Thread({
        Looper.prepareMainLooper()
        ready.countDown()
        Looper.loop()
    }, "main").apply {
        isDaemon = true
        start()
    }
    ready.await()
}

// The class loader of a plugin runtime's jars (lib/cloudstream, lib/echo
// beside lib/host), made once; null when they are not there.
// scancore.<name>.lib names the folder in tests.
private fun pluginClasses(name: String): () -> ClassLoader? {
    val loader by lazy {
        val dir =
            System.getProperty("scancore.$name.lib")?.let(::File)
                ?: File(ExtensionHost::class.java.protectionDomain.codeSource.location.toURI()).parentFile.resolveSibling(name)
        val jars = dir.listFiles { f -> f.name.endsWith(".jar") }?.sortedBy { it.name }.orEmpty()
        if (jars.isEmpty()) null else java.net.URLClassLoader(jars.map { it.toURI().toURL() }.toTypedArray(), ExtensionHost::class.java.classLoader)
    }
    return { loader }
}
