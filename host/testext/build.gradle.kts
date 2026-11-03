// SCANCORE's test extensions, packaged as real signed APKs with the Android
// SDK's tools (d8, aapt2, zipalign, apksigner) by `./gradlew :testext:apk`:
// a Mihon one (src/main, build/apk/testext.apk) and an Aniyomi one
// (src/anime, build/animeapk/testanime.apk), and a CloudStream plugin
// (src/cloudstream, build/cs3/testcs.cs3, by `./gradlew :testext:cs3`).
// Without an SDK
// (ANDROID_SDK_ROOT, ANDROID_HOME or ~/Android/Sdk) no APK is made and the
// tests that need one skip.
import java.security.KeyStore
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

plugins {
    alias(libs.plugins.kotlin.jvm)
}

val anime by sourceSets.creating
val cloudstream by sourceSets.creating
val echo by sourceSets.creating

dependencies {
    // As in real extensions: the API comes from the app, not the APK.
    compileOnly(project(":api"))
    "animeCompileOnly"(project(":api"))
    "cloudstreamCompileOnly"(project(":api"))
    "cloudstreamCompileOnly"(project(":cloudstream"))
    "echoCompileOnly"(project(":echo"))
}

fun sdkDir(): File? =
    listOfNotNull(System.getenv("ANDROID_SDK_ROOT"), System.getenv("ANDROID_HOME"), System.getProperty("user.home") + "/Android/Sdk")
        .map(::File)
        .firstOrNull { File(it, "build-tools").isDirectory && File(it, "platforms").isDirectory }

fun newest(dir: File) = dir.listFiles()?.filter { it.isDirectory }?.maxByOrNull { it.name.replace(Regex("\\D+"), ".").split('.').filter(String::isNotEmpty).map(String::toInt).fold(0L) { a, b -> a * 1000 + b } }

// An APK of one source set's classes, with its manifest and resources from
// src/<set>/android, signed with a new test key whose fingerprint goes into
// signer.txt beside it.
fun apkTask(
    taskName: String,
    set: SourceSet,
    folder: String,
    fileName: String,
) = tasks.register(taskName) {
    val compile = tasks.named<org.jetbrains.kotlin.gradle.tasks.KotlinCompile>(set.getCompileTaskName("kotlin"))
    val classes = compile.flatMap { it.destinationDirectory }
    val manifest = layout.projectDirectory.file("src/${set.name}/android/AndroidManifest.xml")
    val resources = layout.projectDirectory.dir("src/${set.name}/android/res")
    val outDir = layout.buildDirectory.dir(folder)
    val classpath = configurations.named(set.compileClasspathConfigurationName)
    dependsOn(compile)
    inputs.dir(classes)
    inputs.file(manifest)
    inputs.dir(resources)
    outputs.dir(outDir)
    doLast {
        val sdk = sdkDir() ?: run {
            logger.warn("No Android SDK: the test extension APK is not built.")
            return@doLast
        }
        val tools = newest(File(sdk, "build-tools"))!!
        val platform = File(newest(File(sdk, "platforms"))!!, "android.jar")
        val exe = if (System.getProperty("os.name").startsWith("Windows")) ".bat" else ""
        val work = outDir.get().asFile.apply { deleteRecursively(); mkdirs() }
        fun run(vararg command: String) {
            val process = ProcessBuilder(*command).redirectErrorStream(true).start()
            val output = process.inputStream.bufferedReader().readText()
            check(process.waitFor() == 0) { "${command.first()} failed:\n$output" }
        }
        val classFiles = classes.get().asFile.walkTopDown().filter { it.extension == "class" }.map { it.path }.toList()
        val dexDir = File(work, "dex").apply { mkdirs() }
        run(File(tools, "d8$exe").path, "--release", "--min-api", "26", "--lib", platform.path,
            *classpath.get().files.filter { it.exists() }.flatMap { listOf("--classpath", it.path) }.toTypedArray(),
            "--output", dexDir.path, *classFiles.toTypedArray())
        val unaligned = File(work, "unaligned.apk")
        val aapt2 = File(tools, "aapt2" + if (exe.isEmpty()) "" else ".exe").path
        val compiled = File(work, "res.zip")
        run(aapt2, "compile", "--dir", resources.asFile.path, "-o", compiled.path)
        run(aapt2, "link", "--manifest", manifest.asFile.path, "-I", platform.path, "-o", unaligned.path, compiled.path)
        // classes.dex beside the linked manifest.
        val zip = ZipFile(unaligned)
        val withDex = File(work, "withdex.apk")
        ZipOutputStream(withDex.outputStream()).use { out ->
            for (entry in zip.entries()) {
                out.putNextEntry(ZipEntry(entry.name))
                zip.getInputStream(entry).use { it.copyTo(out) }
                out.closeEntry()
            }
            out.putNextEntry(ZipEntry("classes.dex"))
            File(dexDir, "classes.dex").inputStream().use { it.copyTo(out) }
            out.closeEntry()
        }
        zip.close()
        val aligned = File(work, "aligned.apk")
        run(File(tools, "zipalign" + if (exe.isEmpty()) "" else ".exe").path, "-f", "4", withDex.path, aligned.path)
        val keystore = File(work, "test.jks")
        run(File(System.getProperty("java.home"), "bin/keytool").path, "-genkeypair", "-keystore", keystore.path, "-storepass", "testpass",
            "-keypass", "testpass", "-alias", "test", "-keyalg", "RSA", "-keysize", "2048", "-validity", "36500", "-dname", "CN=SCANCORE test extension")
        val signed = File(work, fileName)
        run(File(tools, "apksigner$exe").path, "sign", "--ks", keystore.path, "--ks-pass", "pass:testpass", "--out", signed.path, aligned.path)
        // The signer's fingerprint, for the tests' trust check.
        val certificate = KeyStore.getInstance("PKCS12").apply { keystore.inputStream().use { load(it, "testpass".toCharArray()) } }.getCertificate("test")
        File(work, "signer.txt").writeText(MessageDigest.getInstance("SHA-256").digest(certificate.encoded).joinToString("") { "%02x".format(it) })
        // The same extension as Keiyoushi's jars are: the Java classes, the
        // manifest as plain XML and the resources, jar-signed with that key.
        val unsignedJar = File(work, "unsigned.jar")
        ZipOutputStream(unsignedJar.outputStream()).use { out ->
            out.putNextEntry(ZipEntry("AndroidManifest.xml"))
            manifest.asFile.inputStream().use { it.copyTo(out) }
            out.closeEntry()
            val root = classes.get().asFile
            for (file in root.walkTopDown().filter { it.extension == "class" }) {
                out.putNextEntry(ZipEntry(file.relativeTo(root).invariantSeparatorsPath))
                file.inputStream().use { it.copyTo(out) }
                out.closeEntry()
            }
        }
        run(File(System.getProperty("java.home"), "bin/jarsigner").path, "-keystore", keystore.path, "-storepass", "testpass",
            "-signedjar", File(work, fileName.substringBeforeLast('.') + ".jar").path, unsignedJar.path, "test")
    }
}

val mangaApk = apkTask("mangaApk", sourceSets["main"], "apk", "testext.apk")
val animeApk = apkTask("animeApk", anime, "animeapk", "testanime.apk")

// An Echo extension: an APK like any other, with Echo's meta-data.
val echoApk = apkTask("echoApk", echo, "eapk", "testecho.eapk")
val eapk by tasks.registering { dependsOn(echoApk) }

val apk by tasks.registering {
    dependsOn(mangaApk, animeApk)
}

// The CloudStream plugin: its classes as classes.dex beside a manifest.json,
// zipped, as CloudStream's gradle plugin makes them.
val cs3 by tasks.registering {
    val compile = tasks.named<org.jetbrains.kotlin.gradle.tasks.KotlinCompile>(cloudstream.getCompileTaskName("kotlin"))
    val classes = compile.flatMap { it.destinationDirectory }
    val outDir = layout.buildDirectory.dir("cs3")
    val classpath = configurations.named(cloudstream.compileClasspathConfigurationName)
    dependsOn(compile)
    inputs.dir(classes)
    outputs.dir(outDir)
    doLast {
        val sdk = sdkDir() ?: run {
            logger.warn("No Android SDK: the test CloudStream plugin is not built.")
            return@doLast
        }
        val tools = newest(File(sdk, "build-tools"))!!
        val platform = File(newest(File(sdk, "platforms"))!!, "android.jar")
        val exe = if (System.getProperty("os.name").startsWith("Windows")) ".bat" else ""
        val work = outDir.get().asFile.apply { deleteRecursively(); mkdirs() }
        val classFiles = classes.get().asFile.walkTopDown().filter { it.extension == "class" }.map { it.path }.toList()
        val dexDir = File(work, "dex").apply { mkdirs() }
        // The arguments in a file: on Windows the class path alone is longer
        // than a command line may be.
        val args = File(work, "d8.args")
        args.writeText(
            (listOf("--release", "--min-api", "26", "--lib", platform.path) +
                classpath.get().files.filter { it.exists() }.flatMap { listOf("--classpath", it.path) } +
                listOf("--output", dexDir.path) + classFiles).joinToString("\n"),
        )
        val process = ProcessBuilder(File(tools, "d8$exe").path, "@" + args.path).redirectErrorStream(true).start()
        val output = process.inputStream.bufferedReader().readText()
        check(process.waitFor() == 0) { "d8 failed:\n$output" }
        ZipOutputStream(File(work, "testcs.cs3").outputStream()).use { out ->
            out.putNextEntry(ZipEntry("manifest.json"))
            out.write("""{"name":"Test CloudStream","pluginClassName":"com.mediumdev.scancore.testcs.TestPlugin","version":1,"requiresResources":false}""".toByteArray())
            out.closeEntry()
            out.putNextEntry(ZipEntry("classes.dex"))
            File(dexDir, "classes.dex").inputStream().use { it.copyTo(out) }
            out.closeEntry()
        }
    }
}
