package com.mediumdev.scancore.host.desktop

import com.mediumdev.scancore.host.ExtensionHost
import com.mediumdev.scancore.host.HostError
import com.sun.net.httpserver.HttpServer
import eu.kanade.tachiyomi.network.HostNetwork
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonArray
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.long
import kotlinx.serialization.json.put
import org.junit.AfterClass
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.BeforeClass
import org.junit.Test
import java.io.File
import java.net.InetSocketAddress
import java.nio.file.Files
import java.util.Collections

// The test extensions (host/testext), loaded the way SCANCORE's will be:
// verified, converted from Dalvik bytecode, run against a fixture site on
// fixture.test (tests/fixtures/host). The anime one (Aniyomi) is tested
// below the comics one.
class HostTest {
    companion object {
        private val apk = File(System.getProperty("scancore.testext.apk", ""))
        private val signer = File(apk.parentFile, "signer.txt").takeIf { it.exists() }?.readText()?.trim()
        private val animeApk = File(System.getProperty("scancore.testanime.apk", ""))
        private val animeSigner = File(animeApk.parentFile, "signer.txt").takeIf { it.exists() }?.readText()?.trim()
        private val fixtures = File(System.getProperty("scancore.fixtures", ""))
        private val cs3 = File(System.getProperty("scancore.testcs.cs3", ""))
        private val eapk = File(System.getProperty("scancore.testecho.eapk", ""))
        private lateinit var server: HttpServer
        private lateinit var host: ExtensionHost
        private val requests = Collections.synchronizedList(mutableListOf<String>())

        @JvmStatic
        @BeforeClass
        fun start() {
            assumeTrue("No test extension APK (no Android SDK)", apk.isFile && signer != null)
            server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
            server.createContext("/") { exchange ->
                val path = exchange.requestURI.path
                requests.add(exchange.requestURI.toString())
                val (status, file) =
                    when {
                        path == "/popular" -> 200 to "popular.json"
                        path == "/search" -> 200 to "search.json"
                        path == "/series/1" -> 200 to "series-1.json"
                        path == "/series/1/chapters" -> 200 to "chapters.json"
                        path == "/chapter/2/pages" -> 200 to "pages.json"
                        path == "/page/2" -> 200 to "page-2.json"
                        path == "/anime/popular" -> 200 to "anime/popular.json"
                        path == "/anime/search" -> 200 to "anime/search.json"
                        path == "/anime/1" -> 200 to "anime/anime-1.json"
                        path == "/anime/1/episodes" -> 200 to "anime/episodes.json"
                        path == "/anime/2/seasons" -> 200 to "anime/seasons.json"
                        path == "/episode/1/hosters" -> 200 to "anime/hosters.json"
                        path == "/episode/2/hosters" -> 200 to "anime/hosters-local.json"
                        path == "/hoster/alpha" -> 200 to "anime/videos-alpha.json"
                        path == "/hoster/beta" -> 200 to "anime/videos-beta.json"
                        path == "/resolve" -> 200 to "anime/resolve.json"
                        path == "/episode/1/videos" -> 200 to "anime/videos-classic.json"
                        path == "/videopage/1" -> 200 to "anime/videopage-1.json"
                        // The CloudStream test plugin (host/testext/src/cloudstream).
                        path == "/cs/poster.png" -> 200 to "image.png"
                        path.startsWith("/cs/") && File(fixtures, path.drop(1)).isFile -> 200 to path.drop(1)
                        // Only the extension's own client sends this header.
                        path.startsWith("/img/") && exchange.requestHeaders.getFirst("X-Test-Source") == "1" -> 200 to "image.png"
                        else -> 404 to null
                    }
                val body = file?.let { File(fixtures, it).readBytes() } ?: ByteArray(0)
                exchange.responseHeaders.add("Content-Type", if (file?.endsWith(".png") == true) "image/png" else "application/json")
                exchange.sendResponseHeaders(status, if (body.isEmpty()) -1 else body.size.toLong())
                exchange.responseBody.use { it.write(body) }
            }
            server.start()
            System.setProperty("scancore.testext.base", "http://fixture.test:${server.address.port}")
            val root = Files.createTempDirectory("scancore-host-test").toFile()
            host = setUp(root, null, "test").first
            HostNetwork.dns = HostNetwork.loopbackForTestDomains
        }

        @JvmStatic
        @AfterClass
        fun stop() {
            if (::server.isInitialized) server.stop(0)
        }
    }

    private fun call(
        method: String,
        extra: Map<String, Any?> = emptyMap(),
    ) = runBlocking {
        host.handle(
            method,
            buildJsonObject {
                put("pkg", "com.mediumdev.scancore.testext")
                put("apk", apk.path)
                put("signer", signer)
                put("source", sourceId)
                for ((k, v) in extra) {
                    when (v) {
                        is String -> put(k, v)
                        is Int -> put(k, v)
                        is JsonElement -> put(k, v)
                    }
                }
            },
        )
    }

    private val sourceId: String by lazy {
        runBlocking {
            host
                .handle(
                    "load",
                    buildJsonObject {
                        put("pkg", "com.mediumdev.scancore.testext")
                        put("apk", apk.path)
                        put("signer", signer)
                    },
                ).jsonArray
                .single()
                .jsonObject["id"]!!
                .jsonPrimitive.content
        }
    }

    @Test
    fun loadsAndBrowses() {
        val loaded = runBlocking { host.handle("load", buildJsonObject { put("pkg", "com.mediumdev.scancore.testext"); put("apk", apk.path); put("signer", signer) }) }
        val source = loaded.jsonArray.single().jsonObject
        assertEquals("Test Source", source["name"]!!.jsonPrimitive.content)
        assertEquals("en", source["lang"]!!.jsonPrimitive.content)
        assertEquals("true", source["configurable"]!!.jsonPrimitive.content)
        val popular = call("popular", mapOf("page" to 1)).jsonObject
        assertEquals(2, popular["items"]!!.jsonArray.size)
        assertEquals("true", popular["hasNextPage"]!!.jsonPrimitive.content)
        assertEquals("/series/1", popular["items"]!!.jsonArray[0].jsonObject["url"]!!.jsonPrimitive.content)
    }

    // The same extension as a signed Java jar (Keiyoushi publishes these):
    // loaded without dex2jar, dates still Android's; a changed jar is not.
    @Test
    fun loadsSignedJar() {
        val jar = File(apk.parentFile, "testext.jar")
        assumeTrue("No test extension jar", jar.isFile)
        val load = { pkg: String, path: String ->
            runBlocking { host.handle("load", buildJsonObject { put("pkg", pkg); put("apk", path); put("signer", signer) }) }
        }
        val source = load("com.mediumdev.scancore.testext.jar", jar.path).jsonArray.single().jsonObject
        assertEquals("Test Source", source["name"]!!.jsonPrimitive.content)
        val chapters =
            runBlocking {
                host.handle(
                    "chapters",
                    buildJsonObject {
                        put("pkg", "com.mediumdev.scancore.testext.jar")
                        put("source", source["id"]!!.jsonPrimitive.content)
                        put("url", "/series/1")
                    },
                )
            }.jsonArray
        val expected = java.time.LocalDate.of(2026, 1, 15).atStartOfDay(java.time.ZoneId.systemDefault()).toInstant().toEpochMilli()
        assertEquals(expected, chapters[0].jsonObject["uploaded"]!!.jsonPrimitive.long)

        // One class swapped for another's bytes after signing.
        val tampered = Files.createTempFile("tampered", ".jar").toFile()
        java.util.zip.ZipFile(jar).use { zip ->
            java.util.zip.ZipOutputStream(tampered.outputStream()).use { out ->
                val classes = zip.entries().toList().filter { it.name.endsWith(".class") }
                for (entry in zip.entries()) {
                    out.putNextEntry(java.util.zip.ZipEntry(entry.name))
                    val from = if (entry.name == classes[0].name) classes[1] else entry
                    zip.getInputStream(from).use { it.copyTo(out) }
                    out.closeEntry()
                }
            }
        }
        val refused = runCatching { load("com.mediumdev.scancore.testext.tampered", tampered.path) }.exceptionOrNull()
        assertTrue("$refused", refused is com.mediumdev.scancore.host.HostError && refused.status == 403)
    }

    @Test
    fun searchSendsFilters() {
        val filters = call("filters").jsonArray
        assertEquals(listOf("header", "select", "checkbox"), filters.map { it.jsonObject["type"]!!.jsonPrimitive.content })
        val chosen =
            buildJsonArray {
                add(filters[0])
                add(buildJsonObject { put("type", "select"); put("state", 2) })
                add(buildJsonObject { put("type", "checkbox"); put("state", true) })
            }
        call("search", mapOf("query" to "one", "filters" to chosen))
        val sent = requests.last { it.startsWith("/search") }
        assertTrue(sent, sent.contains("q=one") && sent.contains("genre=Drama") && sent.contains("adult=1"))
    }

    @Test
    fun detailsChaptersAndPages() {
        val details = call("details", mapOf("url" to "/series/1")).jsonObject
        assertEquals("A. Writer", details["author"]!!.jsonPrimitive.content)
        // JavaScript ran (Rhino standing in for QuickJS).
        assertEquals("A test series. js-42", details["description"]!!.jsonPrimitive.content)
        assertEquals(listOf("Action", "Drama"), details["genres"]!!.jsonArray.map { it.jsonPrimitive.content })

        val chapters = call("chapters", mapOf("url" to "/series/1")).jsonArray
        assertEquals(2, chapters.size)
        // Dates went through the Android-behaving SimpleDateFormat, in the
        // device's time zone as on a phone.
        val expected = java.time.LocalDate.of(2026, 1, 15).atStartOfDay(java.time.ZoneId.systemDefault()).toInstant().toEpochMilli()
        assertEquals(expected, chapters[0].jsonObject["uploaded"]!!.jsonPrimitive.long)

        val pages = call("pages", mapOf("url" to "/chapter/2")).jsonArray
        assertEquals(2, pages.size)
        assertTrue(pages[0].jsonObject["imageUrl"]!!.jsonPrimitive.content.endsWith("/img/1.png"))
        assertEquals("", pages[1].jsonObject["imageUrl"]!!.jsonPrimitive.content)

        // The second page needs a request for its image; the image comes
        // through the extension's client straight into the given file.
        val target = Files.createTempFile("page", ".img").toFile()
        val fetched = call("fetchImage", mapOf("page" to pages[1].jsonObject, "target" to target.path)).jsonObject
        assertEquals("image/png", fetched["contentType"]!!.jsonPrimitive.content)
        assertEquals(File(fixtures, "image.png").readBytes().toList(), target.readBytes().toList())
    }

    // The series page asks for details and chapters at once; a source that
    // refuses two updates of one series at a time (Keiyoushi's) still
    // answers both, from one update.
    @Test
    fun detailsAndChaptersTogether() {
        val (details, chapters) =
            runBlocking(Dispatchers.IO) {
                val d = async { call("details", mapOf("url" to "/series/1")) }
                val c = async { call("chapters", mapOf("url" to "/series/1")) }
                d.await() to c.await()
            }
        assertEquals("A. Writer", details.jsonObject["author"]!!.jsonPrimitive.content)
        assertEquals(2, chapters.jsonArray.size)
        // And one after the other, as before.
        assertEquals(2, call("chapters", mapOf("url" to "/series/1")).jsonArray.size)
    }

    private fun setting(key: String) = call("preferences").jsonArray.map { it.jsonObject }.single { it["key"]!!.jsonPrimitive.content == key }

    @Test
    fun settings() {
        val quality = setting("quality")
        assertEquals("list", quality["type"]!!.jsonPrimitive.content)
        assertEquals("high", quality["value"]!!.jsonPrimitive.content)
        call("setPreference", mapOf("key" to "quality", "value" to JsonPrimitive("low").content))
        assertEquals("low", setting("quality")["value"]!!.jsonPrimitive.content)
        call("popular", mapOf("page" to 1))
        assertTrue(requests.last { it.startsWith("/popular") }.contains("quality=low"))
        call("setPreference", mapOf("key" to "quality", "value" to "high"))
    }

    // #21: a multiselect with nothing stored yet, ticked, then cleared, and
    // a null (nothing chosen) taken as an empty set.
    @Test
    fun multiselectSettings() {
        val blocked = setting("blocked")
        assertEquals("multiselect", blocked["type"]!!.jsonPrimitive.content)
        call("setPreference", mapOf("key" to "blocked", "value" to buildJsonArray { add(JsonPrimitive("drama")) }))
        assertEquals(listOf("drama"), setting("blocked")["value"]!!.jsonArray.map { it.jsonPrimitive.content })
        call("setPreference", mapOf("key" to "blocked", "value" to JsonNull))
        assertEquals(emptyList<String>(), setting("blocked")["value"]!!.jsonArray.map { it.jsonPrimitive.content })
    }

    // #20: extensions that make a Handler on the main looper (Komga, Comix,
    // keiyoushi's WebView helper) and post work to it.
    @Test
    fun mainThreadRunsPostedWork() {
        val latest = call("latest", mapOf("page" to 1)).jsonObject
        assertTrue(latest["items"]!!.jsonArray.isNotEmpty())
    }

    @Test
    fun refusesAnotherSigner() {
        try {
            runBlocking {
                // A fresh pkg name so the cached, verified jar is not reused.
                val copy = File(apk.parentFile, "copy-${System.nanoTime()}.apk")
                apk.copyTo(copy)
                host.handle("load", buildJsonObject { put("pkg", "com.mediumdev.scancore.other"); put("apk", copy.path); put("signer", "00".repeat(32)) })
            }
            throw AssertionError("loaded an extension signed with an untrusted key")
        } catch (e: HostError) {
            assertEquals(403, e.status)
        }
    }

    @Test
    fun loadIsCachedAndQuick() {
        sourceId
        val started = System.nanoTime()
        repeat(100) { call("filters") }
        val perCall = (System.nanoTime() - started) / 100 / 1_000_000.0
        println("filters call: %.2f ms".format(perCall))
        assertTrue("calls take $perCall ms", perCall < 20)
    }

    // ------------------------------------------------------------- anime

    private fun animeCall(
        source: String,
        method: String,
        extra: Map<String, Any?> = emptyMap(),
    ) = runBlocking {
        assumeTrue("No test anime extension APK (no Android SDK)", animeApk.isFile && animeSigner != null)
        host.handle(
            method,
            buildJsonObject {
                put("pkg", "com.mediumdev.scancore.testanime")
                put("apk", animeApk.path)
                put("signer", animeSigner)
                put("source", animeSources.getValue(source))
                for ((k, v) in extra) {
                    when (v) {
                        is String -> put(k, v)
                        is Int -> put(k, v)
                        is Boolean -> put(k, v)
                        is JsonArray -> put(k, v)
                    }
                }
            },
        )
    }

    // By name: the hoster-style source and the lib 14 style one.
    private val animeSources: Map<String, String> by lazy {
        assumeTrue("No test anime extension APK (no Android SDK)", animeApk.isFile && animeSigner != null)
        runBlocking {
            host
                .handle(
                    "load",
                    buildJsonObject {
                        put("pkg", "com.mediumdev.scancore.testanime")
                        put("apk", animeApk.path)
                        put("signer", animeSigner)
                    },
                ).jsonArray
                .associate { it.jsonObject["name"]!!.jsonPrimitive.content to it.jsonObject["id"]!!.jsonPrimitive.content }
        }
    }

    private fun JsonObject.text(key: String) = this[key]!!.jsonPrimitive.content

    @Test
    fun animeLoadsAndBrowses() {
        assertEquals(setOf("Test Anime", "Test Anime Classic"), animeSources.keys)
        val loaded = runBlocking { host.handle("load", buildJsonObject { put("pkg", "com.mediumdev.scancore.testanime"); put("apk", animeApk.path); put("signer", animeSigner) }) }
        val source = loaded.jsonArray.first { it.jsonObject.text("name") == "Test Anime" }.jsonObject
        assertEquals("true", source.text("anime"))
        assertEquals("true", source.text("configurable"))
        val popular = animeCall("Test Anime", "popular", mapOf("page" to 1)).jsonObject
        val items = popular["items"]!!.jsonArray.map { it.jsonObject }
        assertEquals(listOf("/anime/1", "/anime/2"), items.map { it.text("url") })
        assertEquals(listOf("episodes", "seasons"), items.map { it.text("fetchType") })
        // Covers relative to the site come back as full links.
        assertTrue(items[0].text("cover").endsWith("/img/anime-1.png") && items[0].text("cover").startsWith("http://fixture.test"))

        val filters = animeCall("Test Anime", "filters").jsonArray
        assertEquals(listOf("header", "select"), filters.map { it.jsonObject.text("type") })
        val chosen = buildJsonArray { add(filters[0]); add(buildJsonObject { put("type", "select"); put("state", 2) }) }
        animeCall("Test Anime", "search", mapOf("query" to "fix", "filters" to chosen))
        val sent = requests.last { it.startsWith("/anime/search") }
        assertTrue(sent, sent.contains("q=fix") && sent.contains("genre=Comedy"))
    }

    @Test
    fun animeDetailsEpisodesAndSeasons() {
        val details = animeCall("Test Anime", "details", mapOf("url" to "/anime/1", "title" to "Fixture Anime")).jsonObject
        assertEquals("A. Studio", details.text("author"))
        assertEquals(listOf("Action", "Comedy"), details["genres"]!!.jsonArray.map { it.jsonPrimitive.content })
        assertTrue(details.text("background").endsWith("/img/background.png"))
        assertEquals("episodes", details.text("fetchType"))

        val episodes = animeCall("Test Anime", "episodes", mapOf("url" to "/anime/1")).jsonArray.map { it.jsonObject }
        assertEquals(listOf("/episode/2", "/episode/1"), episodes.map { it.text("url") })
        assertEquals(listOf(false, true), episodes.map { it["filler"]!!.jsonPrimitive.content.toBoolean() })
        assertEquals(1.0, episodes[1]["number"]!!.jsonPrimitive.content.toDouble(), 0.0)

        val seasons = animeCall("Test Anime", "seasons", mapOf("url" to "/anime/2")).jsonArray.map { it.jsonObject }
        assertEquals(listOf("Season 1", "Season 2"), seasons.map { it.text("title") })
        assertEquals(listOf(1.0, 2.0), seasons.map { it["season"]!!.jsonPrimitive.content.toDouble() })
    }

    @Test
    fun hostersVideosAndResolving() {
        val hosters = animeCall("Test Anime", "hosters", mapOf("url" to "/episode/1", "name" to "Episode 1")).jsonArray.map { it.jsonObject }
        assertEquals(listOf("Alpha", "Beta", "Inline"), hosters.map { it.text("name") })
        assertEquals(listOf(false, true, false), hosters.map { it["lazy"]!!.jsonPrimitive.content.toBoolean() })
        // Videos the hoster listed itself come with it.
        assertEquals(null, hosters[0]["videos"])
        assertEquals("Inline 480p", hosters[2]["videos"]!!.jsonArray.single().jsonObject.text("title"))

        val alpha = animeCall("Test Anime", "hosterVideos", mapOf("hoster" to hosters[0].text("id"))).jsonArray.map { it.jsonObject }
        // The source's own order: higher resolutions first.
        assertEquals(listOf("Alpha 1080p", "Alpha 720p"), alpha.map { it.text("title") })
        val best = alpha[0]
        assertEquals("true", best.text("preferred"))
        assertTrue(best.text("url").endsWith("/video/alpha-1080.m3u8"))
        // The headers mpv must send: the source's, as the video has none.
        val headers = best["headers"]!!.jsonObject
        assertTrue(headers.toString(), headers.text("Referer").startsWith("http://fixture.test") && headers.containsKey("User-Agent"))
        assertEquals("English", best["subtitles"]!!.jsonArray.single().jsonObject.text("lang"))

        // A video listed without its address gets it when it is to play.
        assertEquals("false", alpha[1].text("initialized"))
        val resolved = animeCall("Test Anime", "resolveVideo", mapOf("video" to alpha[1].text("id"))).jsonObject
        assertTrue(resolved.text("url").endsWith("/video/alpha-720.m3u8"))
        assertEquals("true", resolved.text("initialized"))

        // A lazy hoster's videos only load when asked.
        assertTrue(requests.none { it.startsWith("/hoster/beta") })
        val beta = animeCall("Test Anime", "hosterVideos", mapOf("hoster" to hosters[1].text("id"))).jsonArray
        assertEquals("Beta 360p", beta.single().jsonObject.text("title"))

        // An id from before a restart, or from another source, is refused.
        try {
            animeCall("Test Anime", "hosterVideos", mapOf("hoster" to "${animeSources.getValue("Test Anime")}.999999"))
            throw AssertionError("an unknown hoster id was accepted")
        } catch (e: HostError) {
            assertEquals(410, e.status)
        }
    }

    @Test
    fun classicVideosGetTheirAddress() {
        val hosters = animeCall("Test Anime Classic", "hosters", mapOf("url" to "/episode/1")).jsonArray.map { it.jsonObject }
        // Before hosters: one unnamed hoster with the episode's videos.
        assertEquals("", hosters.single().text("name"))
        val video = hosters.single()["videos"]!!.jsonArray.single().jsonObject
        assertEquals("Classic 720p", video.text("title"))
        assertTrue(video.text("url"), video.text("url").endsWith("/video/classic.mp4"))
    }

    // Extensions → Diagnostics: the sites an extension's calls reached.
    @Test
    fun networkUseIsCounted() {
        animeCall("Test Anime", "popular", mapOf("page" to 1))
        val use = runBlocking { host.handle("networkUse", JsonObject(emptyMap())) }.jsonObject
        val hosts = use["com.mediumdev.scancore.testanime"]!!.jsonArray.map { it.jsonObject }
        val fixture = hosts.single { it.text("host") == "fixture.test" }
        assertTrue(fixture.text("requests").toLong() >= 1)
    }

    // Aniyomi's local video servers: the placeholder address becomes the
    // source's own server on this device, which answers.
    @Test
    fun localVideoServersRun() {
        val hosters = animeCall("Test Anime", "hosters", mapOf("url" to "/episode/2")).jsonArray.map { it.jsonObject }
        val video = hosters.single()["videos"]!!.jsonArray.single().jsonObject
        val url = video.text("url")
        assertTrue(url, url.startsWith("http://localhost:") && !url.startsWith("http://localhost:1/"))
        assertEquals("false", video.text("localServer"))
        assertEquals("served by the extension at /served.mp4", java.net.URI(url.replace("localhost", "127.0.0.1")).toURL().readText())
        // The same server for the next video.
        val again = animeCall("Test Anime", "hosters", mapOf("url" to "/episode/2")).jsonArray.single().jsonObject["videos"]!!.jsonArray.single().jsonObject
        assertEquals(url, again.text("url"))
    }

    @Test
    fun animeSettings() {
        val dub = animeCall("Test Anime", "preferences").jsonArray.single().jsonObject
        assertEquals("switch", dub.text("type"))
        assertEquals("false", dub.text("value"))
        animeCall("Test Anime", "setPreference", mapOf("key" to "dub", "value" to true))
        assertEquals("true", animeCall("Test Anime", "preferences").jsonArray.single().jsonObject.text("value"))
        animeCall("Test Anime", "hosters", mapOf("url" to "/episode/1"))
        assertTrue(requests.last { it.startsWith("/episode/1/hosters") }.contains("dub=true"))
        animeCall("Test Anime", "setPreference", mapOf("key" to "dub", "value" to false))
    }

    // ------------------------------------------------------------ CloudStream

    private fun csCall(
        method: String,
        extra: Map<String, String> = emptyMap(),
        page: String? = null,
    ) = runBlocking {
        host.handle(
            method,
            buildJsonObject {
                put("pkg", "TestCloudStream")
                put("apk", cs3.path)
                put("source", com.mediumdev.scancore.host.CloudStream.sourceId("TestCloudStream").toString())
                for ((k, v) in extra) put(k, v)
                page?.let { put("page", buildJsonObject { put("imageUrl", it) }) }
            },
        )
    }

    @Test
    fun cloudStreamPlugin() {
        assumeTrue("No test CloudStream plugin (no Android SDK)", cs3.isFile)
        val source = csCall("load").jsonArray.single().jsonObject
        assertEquals(com.mediumdev.scancore.host.CloudStream.sourceId("TestCloudStream").toString(), source.text("id"))
        assertEquals("Test CloudStream", source.text("name"))
        assertEquals(true, source["anime"]!!.jsonPrimitive.content.toBoolean())

        // The main page's first section is the popular list.
        val popular = csCall("popular", mapOf("page" to "1")).jsonObject
        assertEquals(listOf("First Show", "Second Show"), popular["items"]!!.jsonArray.map { it.jsonObject.text("title") })
        assertEquals(true, popular["hasNextPage"]!!.jsonPrimitive.content.toBoolean())
        val first = popular["items"]!!.jsonArray[0].jsonObject
        // Its sections are a filter; an empty search shows the one chosen.
        val filters = csCall("filters").jsonArray
        assertTrue(filters.toString().contains("New"))
        val search = csCall("search", mapOf("query" to "found")).jsonObject
        assertEquals("Found Show", search["items"]!!.jsonArray.single().jsonObject.text("title"))

        val details = csCall("details", mapOf("url" to first.text("url"))).jsonObject
        assertEquals("First Show", details.text("title"))
        assertEquals("A test show.", details.text("description"))
        // Two seasons: numbered on, newest first, "S2 E1" in the name.
        val episodes = csCall("episodes", mapOf("url" to first.text("url"))).jsonArray.map { it.jsonObject }
        assertEquals(listOf("S2 E1 · Return", "S1 E2", "S1 E1 · Pilot"), episodes.map { it.text("name") })
        assertTrue(episodes.last().text("url").endsWith("/s1e1"))

        // One hoster per extractor, best first; torrents left out.
        val hosters = csCall("hosters", mapOf("url" to "s1e1", "name" to "Pilot")).jsonArray.map { it.jsonObject }
        assertEquals(listOf("Alpha"), hosters.map { it.text("name") })
        val videos = hosters.single()["videos"]!!.jsonArray.map { it.jsonObject }
        assertEquals(listOf(1080, 720), videos.map { it["resolution"]!!.jsonPrimitive.int })
        assertTrue(videos[0].text("url").endsWith("/cs/s1e1-1080.m3u8"))
        assertTrue(videos[0]["headers"].toString().contains("referer"))
        assertTrue(videos[0]["subtitles"].toString().contains("s1e1.vtt"))

        // Covers through the plugin's headers.
        val target = File.createTempFile("cover", ".png")
        val cover = csCall("fetchImage", mapOf("target" to target.path), page = first.text("cover")).toString()
        target.delete()
        assertTrue(cover, cover.isNotEmpty())
    }

    // ------------------------------------------------------------ Echo

    private fun echoCall(
        method: String,
        extra: Map<String, JsonElement> = emptyMap(),
    ) = runBlocking {
        host.handle(
            method,
            buildJsonObject {
                put("pkg", "testecho")
                put("apk", eapk.path)
                put("source", com.mediumdev.scancore.host.Echo.sourceId("testecho").toString())
                for ((k, v) in extra) put(k, v)
            },
        )
    }

    @Test
    fun echoExtension() {
        assumeTrue("No test Echo extension (no Android SDK)", eapk.isFile)
        val source = echoCall("load").jsonArray.single().jsonObject
        assertEquals("Test Echo", source.text("name"))
        assertEquals("true", source["music"].toString())

        // A search's shelves, lists and single items alike, as songs.
        val found = echoCall("searchSongs", mapOf("query" to JsonPrimitive("test"), "page" to JsonPrimitive(1))).jsonArray.map { it.jsonObject }
        assertEquals(listOf("Song 1", "Song 2", "Song 3"), found.map { it.text("title") })
        assertEquals("The Testers", found[0]["artists"]!!.jsonArray.single().jsonPrimitive.content)
        assertEquals(180.001, found[0]["duration"]!!.jsonPrimitive.content.toDouble(), 0.0001)
        assertTrue(found[0].text("cover").endsWith("/img/cover.png"))

        // The home feed is the popular list, page by page.
        assertEquals("Song 11", echoCall("popularSongs", mapOf("page" to JsonPrimitive(1))).jsonArray.single().jsonObject.text("title"))
        assertEquals("Song 12", echoCall("popularSongs", mapOf("page" to JsonPrimitive(2))).jsonArray.single().jsonObject.text("title"))
        assertEquals(0, echoCall("popularSongs", mapOf("page" to JsonPrimitive(3))).jsonArray.size)

        // The best server's stream, with the extension's headers (its
        // settings kept: it counted one start).
        val stream = echoCall("streamOf", mapOf("song" to found[0])).jsonObject
        assertTrue(stream.text("url"), stream.text("url").endsWith("/audio/high-1.mp3"))
        assertEquals("1", stream["headers"]!!.jsonObject["X-Test-Echo"]!!.jsonPrimitive.content)

        // Timed lyrics as LRC.
        val lyrics = echoCall("lyricsOf", mapOf("song" to found[0])).jsonObject
        assertEquals("[00:01.50]First line\n[01:02.25]Second line", lyrics.text("synced"))
    }
}
