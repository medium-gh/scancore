import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

// The desktop helper process: AndroidCompat + dex2jar around the host core.
// installDist puts it with its libraries in build/install/scancore-host.
plugins {
    alias(libs.plugins.kotlin.jvm)
    application
}

// The plugin runtimes' tasks are used below.
evaluationDependsOn(":cloudstream")
evaluationDependsOn(":echo")

application {
    applicationName = "scancore-host"
    mainClass.set("com.mediumdev.scancore.host.desktop.MainKt")
    applicationDefaultJvmArgs = listOf("-Xss2m", "-XX:+UseSerialGC", "-XX:TieredStopAtLevel=1", "-Dfile.encoding=UTF-8")
}

dependencies {
    implementation(project(":core"))
    implementation(libs.dex2jar.translator)
    implementation(libs.dex2jar.tools)
    implementation(libs.apk.parser)
    implementation(libs.apksig)
    runtimeOnly(libs.slf4j.nop)
    testImplementation(libs.junit)
}

// CloudStream's runtime beside lib/ (development builds run the host from
// here; the Extensions runtime puts it in lib/cloudstream instead).
distributions {
    main {
        contents {
            from(project(":cloudstream").tasks.named("runtimeJars")) { into("cloudstream") }
            from(project(":echo").tasks.named("runtimeJars")) { into("echo") }
        }
    }
}

// The end-to-end test needs the test extension APK and the fixture site.
tasks.test {
    dependsOn(":testext:apk")
    systemProperty("scancore.testext.apk", project(":testext").layout.buildDirectory.file("apk/testext.apk").get().asFile.path)
    systemProperty("scancore.testanime.apk", project(":testext").layout.buildDirectory.file("animeapk/testanime.apk").get().asFile.path)
    systemProperty("scancore.fixtures", rootProject.layout.projectDirectory.dir("../tests/fixtures/host").asFile.path)
    // CloudStream's runtime jars, and (by hand) a folder of real plugins.
    dependsOn(":cloudstream:runtimeJars", ":echo:runtimeJars", ":testext:cs3", ":testext:eapk")
    systemProperty("scancore.testcs.cs3", project(":testext").layout.buildDirectory.file("cs3/testcs.cs3").get().asFile.path)
    systemProperty("scancore.testecho.eapk", project(":testext").layout.buildDirectory.file("eapk/testecho.eapk").get().asFile.path)
    systemProperty("scancore.echo.lib", project(":echo").layout.buildDirectory.dir("runtime-jars").get().asFile.path)
    systemProperty("scancore.cloudstream.lib", project(":cloudstream").layout.buildDirectory.dir("runtime-jars").get().asFile.path)
    providers.gradleProperty("cloudstream.live").orNull?.let { systemProperty("scancore.cloudstream.live", it) }
    testLogging {
        events("passed", "skipped", "failed")
        showStandardStreams = true
        exceptionFormat = org.gradle.api.tasks.testing.logging.TestExceptionFormat.FULL
    }
}

// ---------------------------------------------------------------------------
// The Extensions runtime SCANCORE downloads: a Java trimmed with jlink to the
// modules extensions use, and the host's jars (with sqlite's native library
// for this system only). `./gradlew :desktop:runtimeZip` builds it for the
// system Gradle runs on, with that Java (21 in the release workflow, which
// also gives class-data sharing archives the app turns on).
val runtimeModules =
    listOf(
        "java.base", "java.desktop", "java.logging", "java.management", "java.prefs", "java.scripting",
        "java.sql", "java.xml", "jdk.dynalink", "jdk.unsupported",
        // Not visible to jdeps, needed by extensions: TLS with EC keys,
        // non-UTF-8 sites, dates in every language, and the jar edits.
        "jdk.crypto.ec", "jdk.charsets", "jdk.localedata", "jdk.zipfs",
    )

fun platformName(): String {
    val os = System.getProperty("os.name").lowercase()
    val arch = System.getProperty("os.arch").lowercase().let { if (it == "amd64" || it == "x86_64") "x64" else if (it == "aarch64") "arm64" else it }
    return when {
        os.startsWith("windows") -> "windows-$arch"
        os.startsWith("mac") -> "macos-$arch"
        else -> "linux-$arch"
    }
}

// sqlite-jdbc's folder for this system's native library.
fun sqliteNative(): String {
    val os = System.getProperty("os.name").lowercase()
    val arch = System.getProperty("os.arch").lowercase().let { if (it == "amd64") "x86_64" else it }
    return "org/sqlite/native/" + when {
        os.startsWith("windows") -> "Windows/${if (arch == "x86_64") "x86_64" else arch}/"
        os.startsWith("mac") -> "Mac/${if (arch == "aarch64") "aarch64" else "x86_64"}/"
        else -> "Linux/${if (arch == "aarch64") "aarch64" else "x86_64"}/"
    }
}

val hostVersion = "10"

val runtimeImage by tasks.registering {
    dependsOn("installDist", ":cloudstream:runtimeJars", ":echo:runtimeJars")
    val out = layout.buildDirectory.dir("runtime/scancore-extensions-runtime")
    // The host's jars: without them as inputs, a rebuilt host never reached
    // the image.
    inputs.dir(layout.buildDirectory.dir("install/scancore-host/lib"))
    inputs.dir(project(":cloudstream").layout.buildDirectory.dir("runtime-jars"))
    inputs.dir(project(":echo").layout.buildDirectory.dir("runtime-jars"))
    inputs.property("modules", runtimeModules)
    inputs.property("hostVersion", hostVersion)
    outputs.dir(out)
    doLast {
        val dir = out.get().asFile
        dir.deleteRecursively()
        val javaHome = File(System.getProperty("java.home"))
        val jlink = File(javaHome, "bin/jlink" + if (System.getProperty("os.name").startsWith("Windows")) ".exe" else "")
        val major = JavaVersion.current().majorVersion.toInt()
        val process =
            ProcessBuilder(
                jlink.path, "--add-modules", runtimeModules.joinToString(","), "--strip-debug", "--no-header-files",
                "--no-man-pages", "--compress", if (major >= 21) "zip-6" else "2", "--output", dir.path,
            ).redirectErrorStream(true).start()
        val log = process.inputStream.bufferedReader().readText()
        check(process.waitFor() == 0) { "jlink failed:\n$log" }
        val lib = File(dir, "lib/host").apply { mkdirs() }
        val keep = sqliteNative()
        for (jar in layout.buildDirectory.dir("install/scancore-host/lib").get().asFile.listFiles()!!) {
            val target = File(lib, jar.name)
            if (!jar.name.startsWith("sqlite-jdbc")) {
                jar.copyTo(target)
                continue
            }
            // Only this system's native library (the jar has all of them).
            ZipFile(jar).use { zip ->
                ZipOutputStream(target.outputStream().buffered()).use { zipOut ->
                    for (entry in zip.entries()) {
                        if (entry.name.startsWith("org/sqlite/native/") && !entry.isDirectory && !entry.name.startsWith(keep)) continue
                        zipOut.putNextEntry(ZipEntry(entry.name))
                        zip.getInputStream(entry).use { it.copyTo(zipOut) }
                        zipOut.closeEntry()
                    }
                }
            }
        }
        // CloudStream's runtime, in a class loader of its own (core's
        // CloudStream.kt): its jars beside the host's.
        project(":cloudstream").layout.buildDirectory.dir("runtime-jars").get().asFile.copyRecursively(File(dir, "lib/cloudstream"))
        project(":echo").layout.buildDirectory.dir("runtime-jars").get().asFile.copyRecursively(File(dir, "lib/echo"))
        File(dir, "scancore-runtime.json").writeText("""{"host": "$hostVersion", "java": "$major", "platform": "${platformName()}"}""" + "\n")
    }
}

val runtimeZip by tasks.registering(Zip::class) {
    dependsOn(runtimeImage)
    archiveFileName.set("SCANCORE-extensions-runtime-$hostVersion-${platformName()}.zip")
    destinationDirectory.set(layout.buildDirectory.dir("distributions"))
    from(layout.buildDirectory.dir("runtime"))
    // bin/java and friends stay executable.
    eachFile { if (path.contains("/bin/") || path.endsWith("/lib/jspawnhelper")) permissions { unix("rwxr-xr-x") } }
    doLast {
        val zip = archiveFile.get().asFile
        val sum = MessageDigest.getInstance("SHA-256").digest(zip.readBytes()).joinToString("") { "%02x".format(it) }
        File(zip.path + ".sha256").writeText("$sum  ${zip.name}\n")
    }
}
