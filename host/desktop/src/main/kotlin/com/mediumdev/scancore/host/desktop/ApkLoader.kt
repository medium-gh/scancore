package com.mediumdev.scancore.host.desktop

import com.android.apksig.ApkVerifier
import com.googlecode.d2j.dex.Dex2jar
import com.googlecode.d2j.reader.MultiDexFileReader
import com.googlecode.dex2jar.tools.BaksmaliBaseDexExceptionHandler
import com.mediumdev.scancore.host.CloudStream
import com.mediumdev.scancore.host.Echo
import uy.kohesive.injekt.api.get
import com.mediumdev.scancore.host.ExtensionLoader
import com.mediumdev.scancore.host.HostError
import com.mediumdev.scancore.host.LoadedExtension
import eu.kanade.tachiyomi.animesource.AnimeSource
import eu.kanade.tachiyomi.animesource.AnimeSourceFactory
import eu.kanade.tachiyomi.source.Source
import eu.kanade.tachiyomi.source.SourceFactory
import net.dongliu.apk.parser.ApkFile
import org.w3c.dom.Element
import java.io.File
import java.net.URLClassLoader
import java.security.MessageDigest
import javax.xml.parsers.DocumentBuilderFactory

// Runs Mihon and Aniyomi extension APKs on the JVM: checks the signature (apksig) is the
// one SCANCORE trusted, converts the Dalvik bytecode to JVM bytecode
// (dex2jar, as Suwayomi does), points date parsing at Android's behaviour,
// and loads the sources in their own class loader. The check and the
// conversion happen once per APK version; later starts load the cached jar.
class ApkLoader(
    private val cacheDir: File,
) : ExtensionLoader {
    override val libs = listOf("1.4", "1.5", "1.6")
    override val animeLibs = listOf("14", "16", "17")

    companion object {
        // Raised whenever conversion changes (BytecodeEditor included).
        const val CONVERTER = 4
    }

    override fun load(
        pkg: String,
        path: String,
        signer: String?,
    ): LoadedExtension {
        val apk = File(path)
        if (!apk.isFile) throw HostError("The extension's file is missing. Install it again.", 404)
        val bytes = apk.readBytes()
        if (CloudStream.isPlugin(path)) return loadCloudStream(pkg, apk, bytes)
        if (Echo.isPlugin(path)) return loadEcho(pkg, apk, bytes, signer)
        // A new converter makes older conversions stale.
        val stamp = "${sha256(bytes).take(16)}-c$CONVERTER"
        val jar = File(cacheDir, "$pkg-$stamp.jar")
        val manifest = File(cacheDir, "$pkg-$stamp.meta")
        if (!jar.isFile || !manifest.isFile) {
            // Keiyoushi also publishes each extension as a signed Java jar:
            // their builds are minified since lib 1.6, and dex2jar turns the
            // minified code into classes Java refuses ("Bad type on operand
            // stack"). Suwayomi installs these jars too.
            if (path.endsWith(".jar")) {
                val meta = verifyJar(apk, signer)
                copyJar(apk, jar)
                manifest.writeText(meta.entries.joinToString("\n") { "${it.key}=${it.value}" })
            } else {
                verify(apk, signer)
                convert(bytes, jar)
                manifest.writeText(metadata(apk).entries.joinToString("\n") { "${it.key}=${it.value}" })
            }
            // Older conversions of this extension are no longer needed.
            cacheDir.listFiles { f -> f.name.startsWith("$pkg-") && !f.name.startsWith("$pkg-$stamp") }?.forEach { it.delete() }
        }
        val meta = manifest.readLines().associate { it.substringBefore('=') to typedValue(it.substringAfter('=')) }
        // Aniyomi's extensions name their classes under their own key.
        val anime = meta.containsKey("tachiyomi.animeextension.class")
        val (libKey, supported) = if (anime) "aniyomix.extensionLib" to animeLibs else "tachiyomix.extensionLib" to libs
        val lib = meta[libKey]?.takeIf { it.isNotEmpty() && it.toDoubleOrNull() != 0.0 } ?: meta["versionName"].orEmpty().substringBeforeLast('.')
        if (lib.toDoubleOrNull() !in supported.map { it.toDouble() }) {
            throw HostError("This extension uses lib $lib, which this SCANCORE does not run.", 426)
        }
        val classes =
            meta[if (anime) "tachiyomi.animeextension.class" else "tachiyomi.extension.class"]
                ?.split(";")
                ?.map { it.trim() }
                ?.filter { it.isNotEmpty() }
                ?: throw HostError("The extension does not say which sources it has.", 422)
        val loader = ChildFirstClassLoader(arrayOf(jar.toURI().toURL()), javaClass.classLoader)
        val instances =
            classes.map { name ->
                val className = if (name.startsWith(".")) (meta["package"]?.takeIf { it.isNotEmpty() } ?: pkg) + name else name
                Class.forName(className, false, loader).getDeclaredConstructor().newInstance()
            }
        return if (anime) {
            LoadedExtension(
                anime =
                    instances.flatMap {
                        when (it) {
                            is AnimeSource -> listOf(it)
                            is AnimeSourceFactory -> it.createSources()
                            else -> throw HostError("The extension's class ${it.javaClass.name} is not a source.", 422)
                        }
                    },
            )
        } else {
            LoadedExtension(
                sources =
                    instances.flatMap {
                        when (it) {
                            is Source -> listOf(it)
                            is SourceFactory -> it.createSources()
                            else -> throw HostError("The extension's class ${it.javaClass.name} is not a source.", 422)
                        }
                    },
            )
        }
    }

    // A CloudStream plugin: a dex and its manifest.json in a zip, converted
    // like an APK (it has no signature: SCANCORE trusts the repository the
    // reader added) and loaded over CloudStream's runtime.
    private fun loadCloudStream(
        pkg: String,
        file: File,
        bytes: ByteArray,
    ): LoadedExtension {
        val jar = File(cacheDir, "cs-$pkg-${sha256(bytes).take(16)}-c$CONVERTER.jar")
        val app = uy.kohesive.injekt.Injekt.get<android.app.Application>()
        val runtime = CloudStream.runtime(app) { uy.kohesive.injekt.Injekt.get<eu.kanade.tachiyomi.network.NetworkHelper>().client }
        if (!jar.isFile) {
            val temp = File(jar.path + ".frames")
            convert(bytes, temp, runtime.javaClass.classLoader)
            // dex2jar works out stack frames without CloudStream's classes
            // in sight and writes "java/util/Object" where it cannot: the
            // frames are worked out again with them.
            Frames.recompute(temp, jar, runtime.javaClass.classLoader)
            temp.delete()
            cacheDir.listFiles { f -> f.name.startsWith("cs-$pkg-") && f.name != jar.name }?.forEach { it.delete() }
        }
        val classes = ChildFirstClassLoader(arrayOf(jar.toURI().toURL()), runtime.javaClass.classLoader)
        return LoadedExtension(anime = runtime.load(pkg, file, classes))
    }

    // An Echo extension: an APK (signed by its author, whose key SCANCORE
    // keeps from the first install), converted like any other and loaded
    // over Echo's runtime; its manifest's meta-data names its class.
    private fun loadEcho(
        pkg: String,
        file: File,
        bytes: ByteArray,
        signer: String?,
    ): LoadedExtension {
        val stamp = "${sha256(bytes).take(16)}-c$CONVERTER"
        val jar = File(cacheDir, "echo-$pkg-$stamp.jar")
        val manifest = File(cacheDir, "echo-$pkg-$stamp.meta")
        val app = uy.kohesive.injekt.Injekt.get<android.app.Application>()
        val root = File(System.getProperty("scancore.host.root") ?: cacheDir.parent)
        val runtime = Echo.runtime(app, { uy.kohesive.injekt.Injekt.get<eu.kanade.tachiyomi.network.NetworkHelper>().client }, File(root, "echo"))
        if (!jar.isFile || !manifest.isFile) {
            verify(file, signer)
            val temp = File(jar.path + ".frames")
            convert(bytes, temp, runtime.javaClass.classLoader)
            Frames.recompute(temp, jar, runtime.javaClass.classLoader)
            temp.delete()
            manifest.writeText(metadata(file).entries.joinToString("\n") { "${it.key}=${it.value}" })
            cacheDir.listFiles { f -> f.name.startsWith("echo-$pkg-") && !f.name.startsWith("echo-$pkg-$stamp") }?.forEach { it.delete() }
        }
        val meta = manifest.readLines().filter { '=' in it }.associate { it.substringBefore('=') to it.substringAfter('=') }
        // Parent first, as Echo's app loads extensions: Echo's classes are
        // the runtime's even when an extension carries a copy.
        val classes = java.net.URLClassLoader(arrayOf(jar.toURI().toURL()), runtime.javaClass.classLoader)
        return LoadedExtension(music = runtime.load(pkg, file, classes, meta))
    }

    private fun verify(
        apk: File,
        signer: String?,
    ) {
        val result = ApkVerifier.Builder(apk).build().verify()
        if (!result.isVerified) {
            val reason = result.errors.firstOrNull()?.toString() ?: "unknown"
            throw HostError("The extension's signature does not check out ($reason).", 403)
        }
        if (signer != null) {
            val signers = result.signerCertificates.map { sha256(it.encoded) }
            if (signer.lowercase() !in signers) throw HostError("The extension is signed with a key SCANCORE did not trust.", 403)
        }
    }

    // A jar extension: every file but the signature's own must be signed by
    // the trusted key (Java checks each entry's digest as it is read).
    // Returns the meta-data of its AndroidManifest.xml, which is plain XML.
    private fun verifyJar(
        file: File,
        signer: String?,
    ): Map<String, String> {
        var manifest: ByteArray? = null
        try {
            java.util.jar.JarFile(file, true).use { jar ->
                var signed = 0
                for (entry in jar.entries()) {
                    if (entry.isDirectory) continue
                    val bytes = jar.getInputStream(entry).use { it.readBytes() }
                    val name = entry.name
                    if (name.startsWith("META-INF/") && name.count { it == '/' } == 1) continue
                    val signers = entry.codeSigners.orEmpty().map { sha256(it.signerCertPath.certificates.first().encoded) }
                    if (signers.isEmpty()) throw HostError("The extension has a file that is not signed ($name).", 403)
                    if (signer != null && signer.lowercase() !in signers) {
                        throw HostError("The extension is signed with a key SCANCORE did not trust.", 403)
                    }
                    if (name == "AndroidManifest.xml") manifest = bytes
                    signed++
                }
                if (signed == 0) throw HostError("The extension is not signed.", 403)
            }
        } catch (e: SecurityException) {
            throw HostError("The extension's signature does not check out (${e.message}).", 403)
        }
        val xml = manifest ?: throw HostError("The extension has no manifest.", 422)
        val doc = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(xml.inputStream())
        val root = doc.documentElement
        val out = mutableMapOf("versionName" to root.getAttribute("android:versionName"), "package" to root.getAttribute("package"))
        val nodes = doc.getElementsByTagName("meta-data")
        for (i in 0 until nodes.length) {
            val e = nodes.item(i) as? Element ?: continue
            out[e.getAttribute("android:name")] = e.getAttribute("android:value").replace('\n', ' ')
        }
        return out
    }

    // The checked jar without its signature (the date fix below edits the
    // classes, which a signed jar would then refuse to load).
    private fun copyJar(
        source: File,
        jar: File,
    ) {
        cacheDir.mkdirs()
        val temp = File(jar.path + ".part")
        temp.delete()
        java.util.zip.ZipInputStream(source.inputStream().buffered()).use { input ->
            java.util.zip.ZipOutputStream(temp.outputStream().buffered()).use { output ->
                while (true) {
                    val entry = input.nextEntry ?: break
                    val name = entry.name
                    if (entry.isDirectory || name.startsWith("META-INF/") || name.contains("..")) continue
                    output.putNextEntry(java.util.zip.ZipEntry(name))
                    input.copyTo(output)
                    output.closeEntry()
                }
            }
        }
        BytecodeEditor.fixAndroidClasses(temp.toPath())
        if (!temp.renameTo(jar)) throw HostError("The extension could not be saved.", 500)
    }

    private fun convert(
        apk: ByteArray,
        jar: File,
        runtime: ClassLoader? = null,
    ) {
        cacheDir.mkdirs()
        val temp = File(jar.path + ".part")
        temp.delete()
        val handler = BaksmaliBaseDexExceptionHandler()
        Dex2jar
            .from(InlinedConstructors.fix(MultiDexFileReader.open(apk)))
            .withExceptionHandler(handler)
            .reUseReg(false)
            .topoLogicalSort()
            .skipDebug(true)
            .optimizeSynchronized(false)
            .printIR(false)
            .noCode(false)
            .skipExceptions(false)
            .dontSanitizeNames(true)
            .computeFrames(true)
            .to(temp.toPath())
        if (handler.hasException()) {
            temp.delete()
            throw HostError("The extension could not be converted to run on this computer.", 422)
        }
        BytecodeEditor.fixAndroidClasses(temp.toPath(), runtime)
        copyResources(apk, temp)
        if (!temp.renameTo(jar)) throw HostError("The converted extension could not be saved.", 500)
    }

    // The APK's other files (assets/, res/ and any at its root) go into the
    // jar too: on Android an extension's class loader reads them from the
    // APK, so getResourceAsStream("assets/i18n/messages_en.properties")
    // works there. Keiyoushi's builds load their texts that way when the
    // source is made, and without them every one failed with a
    // NullPointerException. Not META-INF: its signatures are the APK's and
    // would make Java reject the converted classes.
    private fun copyResources(
        apk: ByteArray,
        jar: File,
    ) {
        java.nio.file.FileSystems.newFileSystem(jar.toPath()).use { zip ->
            java.util.zip.ZipInputStream(apk.inputStream()).use { input ->
                while (true) {
                    val entry = input.nextEntry ?: break
                    val name = entry.name
                    if (entry.isDirectory || name.startsWith("META-INF/") || name.endsWith(".dex") || name.contains("..")) continue
                    if (name == "AndroidManifest.xml" || name == "resources.arsc") continue
                    val target = zip.getPath(name)
                    if (java.nio.file.Files.exists(target)) continue
                    target.parent?.let { java.nio.file.Files.createDirectories(it) }
                    java.nio.file.Files.copy(input, target)
                }
            }
        }
    }

    // The APK parser writes typed values it does not know as {type:data}:
    // Keiyoushi's builds declare tachiyomix.extensionLib as a float (1.6 is
    // {4:1070386381}), which read as text made every one of them "lib
    // {4:…}". Android's binary XML types: 4 float, 16 and 17 int, 18 boolean.
    private fun typedValue(value: String): String {
        val match = Regex("""^\{(\d+):(-?\d+)\}$""").matchEntire(value) ?: return value
        val data = match.groupValues[2].toLong().toInt()
        return when (match.groupValues[1].toInt()) {
            4 -> java.lang.Float.intBitsToFloat(data).toString()
            16, 17 -> data.toString()
            18 -> (data != 0).toString()
            else -> value
        }
    }

    // The meta-data of the manifest's <application>, and the version name.
    private fun metadata(apk: File): Map<String, String> =
        ApkFile(apk).use { parsed ->
            val doc = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(parsed.manifestXml.byteInputStream())
            val out = mutableMapOf("versionName" to (parsed.apkMeta.versionName ?: ""))
            val nodes = doc.getElementsByTagName("meta-data")
            for (i in 0 until nodes.length) {
                val e = nodes.item(i) as? Element ?: continue
                out[e.getAttribute("android:name")] = e.getAttribute("android:value").replace('\n', ' ')
            }
            out
        }

    private fun sha256(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
}

// Classes come from the extension first, so its own copies of libraries win;
// the host's API classes are not in the extension, so they come from the
// host, which is what makes it run.
class ChildFirstClassLoader(
    urls: Array<java.net.URL>,
    parent: ClassLoader,
) : URLClassLoader(urls, parent) {
    override fun loadClass(
        name: String,
        resolve: Boolean,
    ): Class<*> =
        synchronized(getClassLoadingLock(name)) {
            val c =
                findLoadedClass(name)
                    ?: if (name.startsWith("java.") || name.startsWith("javax.") || name.startsWith("kotlin.")) {
                        super.loadClass(name, false)
                    } else {
                        try {
                            findClass(name)
                        } catch (_: ClassNotFoundException) {
                            super.loadClass(name, false)
                        }
                    }
            if (resolve) resolveClass(c)
            c
        }
}
