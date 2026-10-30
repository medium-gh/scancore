package com.mediumdev.scancore.host

import android.content.SharedPreferences
import androidx.preference.PreferenceScreen
import eu.kanade.tachiyomi.animesource.AnimeSource
import eu.kanade.tachiyomi.animesource.ConfigurableAnimeSource
import eu.kanade.tachiyomi.animesource.online.AnimeHttpSource
import eu.kanade.tachiyomi.network.HostNetwork
import eu.kanade.tachiyomi.network.MemoryCookieJar
import eu.kanade.tachiyomi.source.CatalogueSource
import eu.kanade.tachiyomi.source.ConfigurableSource
import eu.kanade.tachiyomi.source.Source
import eu.kanade.tachiyomi.source.model.SChapter
import eu.kanade.tachiyomi.source.model.SManga
import eu.kanade.tachiyomi.source.model.SMangaUpdate
import eu.kanade.tachiyomi.source.online.HttpSource
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonArray
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.put
import okhttp3.Cookie
import uy.kohesive.injekt.api.get
import okhttp3.HttpUrl.Companion.toHttpUrlOrNull
import okhttp3.Response
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import kotlin.coroutines.coroutineContext
import kotlinx.coroutines.withContext

// What one extension holds: Mihon sources (comics), Aniyomi sources
// (anime, CloudStream's too) or music sources (Echo).
class LoadedExtension(
    val sources: List<Source> = emptyList(),
    val anime: List<AnimeSource> = emptyList(),
    val music: List<MusicSource> = emptyList(),
)

// Loads extensions on first use (desktop: converted APKs; Android: the
// package manager) and reports what they contain.
interface ExtensionLoader {
    // The extension's sources. `signer` is the SHA-256 of the certificate
    // SCANCORE trusted; the loader refuses a file signed otherwise.
    fun load(
        pkg: String,
        path: String,
        signer: String?,
    ): LoadedExtension

    // Lib versions this host runs: Mihon's (e.g. "1.4", "1.5", "1.6") and
    // Aniyomi's ("14", "16", "17").
    val libs: List<String>
    val animeLibs: List<String>
}

// Answers SCANCORE's calls for Mihon and Aniyomi extensions. Every call
// names its extension (pkg, apk, signer) so the host can load it on demand,
// and its source by id. Anime sources' calls are in AnimeCalls.
class ExtensionHost(
    private val loader: ExtensionLoader,
    private val memos: MemoStore,
    private val version: String,
    private val settings: SettingsBridge,
) {
    // By package: each source id's Source or AnimeSource.
    private val loaded = ConcurrentHashMap<String, Map<Long, Any>>()
    private val locks = ConcurrentHashMap<String, Mutex>()
    private val anime = AnimeCalls(memos, settings)

    suspend fun handle(
        method: String,
        params: JsonObject,
    ): JsonElement =
        when (method) {
            "hello" ->
                buildJsonObject {
                    put("version", version)
                    put("libs", buildJsonArray { loader.libs.forEach { add(JsonPrimitive(it)) } })
                    put("animeLibs", buildJsonArray { loader.animeLibs.forEach { add(JsonPrimitive(it)) } })
                }
            "load" -> describe(sources(params))
            "unload" -> {
                loaded.remove(params.string("pkg"))
                JsonNull
            }
            "setUserAgent" -> {
                HostNetwork.userAgent = params.string("userAgent")
                JsonNull
            }
            "setCookies" -> setCookies(params)
            // Extensions → Diagnostics: the sites each extension contacted.
            "networkUse" ->
                buildJsonObject {
                    for ((pkg, hosts) in eu.kanade.tachiyomi.network.NetworkUse.report()) {
                        put(
                            pkg,
                            buildJsonArray {
                                for ((host, use) in hosts) {
                                    add(
                                        buildJsonObject {
                                            put("host", host)
                                            put("requests", use.first)
                                            put("last", use.second)
                                        },
                                    )
                                }
                            },
                        )
                    }
                }
            else -> {
                // Everything the call does over the network counts for its
                // extension, whichever thread it runs on.
                val pkg = params.optString("pkg") ?: packages[params.optString("source")?.toLongOrNull()]
                if (pkg == null) call(method, params) else withContext(eu.kanade.tachiyomi.network.NetworkUse.For(pkg)) { call(method, params) }
            }
        }

    // Which extension each source came in.
    private val packages = ConcurrentHashMap<Long, String>()

    private suspend fun sources(params: JsonObject): Map<Long, Any> {
        val pkg = params.string("pkg")
        loaded[pkg]?.let { return it }
        val lock = locks.getOrPut(pkg) { Mutex() }
        return lock.withLock {
            loaded[pkg] ?: eu.kanade.tachiyomi.network.NetworkUse
                .during(pkg) { loader.load(pkg, params.string("apk"), params.optString("signer")) }
                .let { it.sources.associateBy { s -> s.id } + it.anime.associateBy { s -> s.id } + it.music.associateBy { s -> s.id } }
                .also {
                    loaded[pkg] = it
                    for (id in it.keys) packages[id] = pkg
                }
        }
    }

    private fun describe(sources: Map<Long, Any>) =
        buildJsonArray {
            for (source in sources.values) {
                add(
                    when (source) {
                        is MusicSource ->
                            buildJsonObject {
                                put("id", source.id.toString())
                                put("name", source.name)
                                put("lang", source.lang)
                                put("baseUrl", source.baseUrl)
                                put("supportsLatest", false)
                                put("configurable", false)
                                put("music", true)
                            }
                        is AnimeSource ->
                            buildJsonObject {
                                put("id", source.id.toString())
                                put("name", source.name)
                                put("lang", source.lang)
                                put("baseUrl", (source as? AnimeHttpSource)?.baseUrl ?: (source as? PluginAnimeSource)?.baseUrl)
                                put("supportsLatest", source.supportsLatest)
                                put("configurable", source is ConfigurableAnimeSource)
                                put("anime", true)
                            }
                        else ->
                            buildJsonObject {
                                source as Source
                                put("id", source.id.toString())
                                put("name", source.name)
                                put("lang", source.lang)
                                put("baseUrl", (source as? HttpSource)?.baseUrl)
                                put("supportsLatest", (source as? CatalogueSource)?.supportsLatest ?: false)
                                put("configurable", source is ConfigurableSource)
                            }
                    },
                )
            }
        }

    private suspend fun source(params: JsonObject): Any {
        val id = params.string("source").toLongOrNull() ?: throw HostError("That is not a source id.", 400)
        return sources(params)[id] ?: throw HostError("The extension has no source $id. It may need updating.", 404)
    }

    private fun base(source: Source) = (source as? HttpSource)?.baseUrl

    // Calls that start with the site itself, rather than images.
    private val browsing = setOf("popular", "latest", "search", "details", "chapters", "pages", "episodes", "seasons", "hosters")

    // Sites whose home page went through the Cloudflare check this session.
    private val warmed = java.util.concurrent.ConcurrentHashMap.newKeySet<String>()

    // Some extensions (Comix) open the site in a web view of their own, where
    // a Cloudflare check is never seen by CloudflareInterceptor: they wait
    // minutes and fail. So the first call to a site in a session loads its
    // home page through the default client first, which passes a check
    // there (hidden, else with the reader's tap) and leaves the clearance
    // in the cookies those web views share. One request per site per
    // session; any other failure is the extension's own business.
    private suspend fun warmUp(source: Any) {
        val (base, headers) =
            when (source) {
                is HttpSource -> source.baseUrl to source.headers
                is AnimeHttpSource -> source.baseUrl to source.headers
                else -> return
            }
        val url = base.toHttpUrlOrNull() ?: return
        if (url.host in warmed) return
        kotlinx.coroutines.runInterruptible(kotlinx.coroutines.Dispatchers.IO) {
            try {
                val request = okhttp3.Request.Builder().url(url).headers(headers).build()
                uy.kohesive.injekt.Injekt.get<eu.kanade.tachiyomi.network.NetworkHelper>().client.newCall(request).execute().close()
                warmed += url.host
            } catch (e: eu.kanade.tachiyomi.network.BrowserCheckException) {
                throw e
            } catch (_: Exception) {
                warmed += url.host
            }
        }
    }

    // An extension that loads the site in its own hidden web view (Keiyoushi's
    // runWebView) cannot show the site's human check there, and asks the
    // reader to "Open in WebView" instead, as Mihon's button does. That
    // becomes a browser check of the page that failed (a series' page holds
    // a check of its own that the home page does not), with the extension's
    // user agent, so the clearance the reader earns is the one its web view
    // sends.
    private suspend fun <T> checkInWebView(
        source: Source,
        page: (Source) -> String,
        work: suspend () -> T,
    ): T =
        try {
            work()
        } catch (e: Exception) {
            val asks =
                generateSequence<Throwable>(e) { it.cause }.any { cause ->
                    cause !is eu.kanade.tachiyomi.network.BrowserCheckException &&
                        cause.message?.contains("in WebView", ignoreCase = true) == true
                }
            val http = source as? HttpSource
            val url = if (asks && http != null) runCatching { page(source) }.getOrNull() else null
            if (url == null || !url.startsWith("http")) throw e
            throw eu.kanade.tachiyomi.network.BrowserCheckException(
                url,
                http!!.headers["User-Agent"] ?: eu.kanade.tachiyomi.network.HostNetwork.userAgent,
            )
        }

    private fun catalogue(source: Source) = source as? CatalogueSource ?: throw HostError("${source.name} cannot be browsed.", 501)

    // A series' details and chapters come from one getMangaUpdate, and
    // Keiyoushi's sources refuse a second one for the same series while one
    // runs ("must not be called concurrently for same manga"): the series
    // page asks for details and chapters at once, so every such source
    // failed there. Calls for a series that arrive within a moment of each
    // other are made into one asking for all they want (as Mihon does);
    // later ones wait their turn. The fetch runs on its own, so one caller
    // giving up does not leave the others without an answer.
    private class MangaUpdateJob(
        var details: Boolean,
        var chapters: Boolean,
    ) {
        val result = CompletableDeferred<SMangaUpdate>()
        var started = false
    }

    private val updateJobs = HashMap<String, MangaUpdateJob>()
    private val updateLocks = ConcurrentHashMap<String, Mutex>()
    private val updateScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private suspend fun mangaUpdate(
        source: Source,
        url: String,
        details: Boolean,
        chapters: Boolean,
    ): SMangaUpdate {
        val key = "${source.id}|$url"
        val (job, first) =
            synchronized(updateJobs) {
                val waiting = updateJobs[key]?.takeIf { !it.started }
                if (waiting != null) {
                    waiting.details = waiting.details || details
                    waiting.chapters = waiting.chapters || chapters
                    waiting to false
                } else {
                    MangaUpdateJob(details, chapters).also { updateJobs[key] = it } to true
                }
            }
        if (first) {
            updateScope.launch(coroutineContext[eu.kanade.tachiyomi.network.NetworkUse.For] ?: kotlin.coroutines.EmptyCoroutineContext) {
                delay(UPDATE_GATHER_MS)
                updateLocks.getOrPut(key) { Mutex() }.withLock {
                    synchronized(updateJobs) {
                        job.started = true
                        if (updateJobs[key] === job) updateJobs.remove(key)
                    }
                    try {
                        job.result.complete(source.getMangaUpdate(manga(source, url), emptyList(), job.details, job.chapters))
                    } catch (e: Throwable) {
                        job.result.completeExceptionally(e)
                    }
                }
            }
        }
        return job.result.await()
    }

    // A series' title as its listing gave it. Mihon always knows it (from
    // its library) and some extensions read it while fetching details or
    // chapters (MangaDex's did: "lateinit property title has not been
    // initialized"), so it is never left unset.
    private val titles = ConcurrentHashMap<String, String>()

    private fun manga(
        source: Source,
        url: String,
        title: String? = null,
    ): SManga =
        SManga.create().also {
            it.url = url
            it.title = title?.takeIf { t -> t.isNotEmpty() } ?: titles["${source.id}|$url"].orEmpty()
            memos.manga(source.id, url)?.let { memo -> it.memo = memo }
        }

    private fun chapter(
        source: Source,
        url: String,
    ): SChapter =
        SChapter.create().also {
            it.url = url
            memos.chapter(source.id, url)?.let { memo -> it.memo = memo }
        }

    private suspend fun call(
        method: String,
        params: JsonObject,
    ): JsonElement {
        val found = source(params)
        if (method in browsing) warmUp(found)
        val source =
            when (found) {
                is AnimeSource -> return anime.call(method, found, params)
                is MusicSource -> return music(method, found, params)
                else -> found as Source
            }
        val page = params.int("page", 1)
        return when (method) {
            "popular" -> Codec.mangas(catalogue(source).getPopularManga(page).also { remember(source, it.mangas) }, base(source))
            "latest" -> Codec.mangas(catalogue(source).getLatestUpdates(page).also { remember(source, it.mangas) }, base(source))
            "search" -> {
                val filters = Codec.applyFilters(source.getFilterList(), params["filters"] as? JsonArray ?: JsonArray(emptyList()))
                Codec.mangas(
                    catalogue(source).getSearchManga(page, params.optString("query").orEmpty(), filters).also { remember(source, it.mangas) },
                    base(source),
                )
            }
            "details" ->
                checkInWebView(source, { (it as HttpSource).getMangaUrl(manga(source, params.string("url"))) }) {
                    val update = mangaUpdate(source, params.string("url"), details = true, chapters = false)
                    remember(source, listOf(update.manga))
                    Codec.details(update.manga, base(source))
                }
            "chapters" ->
                checkInWebView(source, { (it as HttpSource).getMangaUrl(manga(source, params.string("url"))) }) {
                    val update = mangaUpdate(source, params.string("url"), details = false, chapters = true)
                    for (c in update.chapters) if (c.memo.isNotEmpty()) memos.putChapter(source.id, c.url, c.memo)
                    Codec.chapters(update.chapters)
                }
            "pages" ->
                checkInWebView(source, { (it as HttpSource).getChapterUrl(chapter(source, params.string("url"))) }) {
                    Codec.pages(source.getPageList(chapter(source, params.string("url"))))
                }
            "imageUrl" -> {
                val http = source as? HttpSource ?: throw HostError("${source.name} has no image addresses.", 501)
                JsonPrimitive(http.getImageUrl(Codec.page(params["page"]?.jsonObject ?: JsonObject(emptyMap()))))
            }
            "fetchImage" -> fetchImage(source, params)
            "filters" -> Codec.filters(source.getFilterList())
            "preferences" -> (source as? ConfigurableSource)?.let { settings.describe(Configurable.of(it)) } ?: JsonArray(emptyList())
            "setPreference" -> {
                settings.set(configurable(source), params.string("key"), params["value"] ?: JsonNull)
                JsonNull
            }
            else -> throw HostError("The host does not know \"$method\".", 400)
        }
    }

    // Echo's music sources: the music plugin contract's calls.
    private suspend fun music(
        method: String,
        source: MusicSource,
        params: JsonObject,
    ): JsonElement =
        when (method) {
            "searchSongs" -> source.search(params.optString("query").orEmpty(), params.int("page", 1))
            "popularSongs" -> source.popular(params.int("page", 1))
            "streamOf" -> source.stream(params["song"]?.jsonObject ?: throw HostError("The request has no song.", 400))
            "lyricsOf" -> source.lyrics(params["song"]?.jsonObject ?: throw HostError("The request has no song.", 400))
            else -> throw HostError("The host does not know \"$method\" for music.", 400)
        }

    // The image through the source's own client (its rate limits, headers
    // and any interceptor that unscrambles pages), written straight to the
    // file SCANCORE's image cache names; the bytes never cross the pipe.
    private suspend fun fetchImage(
        source: Source,
        params: JsonObject,
    ): JsonElement {
        val http = source as? HttpSource ?: throw HostError("${source.name} has no images.", 501)
        val target = File(params.string("target"))
        var page = Codec.page(params["page"]?.jsonObject ?: JsonObject(emptyMap()))
        if (page.imageUrl.isNullOrEmpty()) page.imageUrl = http.getImageUrl(page)
        return http.getImage(page).use { writeImage(it, target) }
    }

    private fun configurable(source: Source) =
        (source as? ConfigurableSource)?.let(Configurable::of) ?: throw HostError("${source.name} has no settings.", 501)

    private fun remember(
        source: Source,
        mangas: List<SManga>,
    ) {
        for (m in mangas) {
            if (m.memo.isNotEmpty()) memos.putManga(source.id, m.url, m.memo)
            // Either may be unset (details give no url back).
            val url = runCatching { m.url }.getOrNull() ?: continue
            runCatching { m.title }.getOrNull()?.takeIf { it.isNotEmpty() }?.let { titles["${source.id}|$url"] = it }
        }
    }

    // Cookies from SCANCORE's browser-check window: {url, cookies: ["a=b; Path=/", ...]}.
    private fun setCookies(params: JsonObject): JsonElement {
        val url = params.string("url").toHttpUrlOrNull() ?: throw HostError("That is not a link.", 400)
        val cookies = (params["cookies"] as? JsonArray).orEmpty().mapNotNull { Cookie.parse(url, it.jsonPrimitive.content) }
        val jar = HostNetwork.cookieJar
        if (jar is MemoryCookieJar) jar.add(cookies) else jar.saveFromResponse(url, cookies)
        return JsonPrimitive(cookies.size)
    }
}

// How long a series' first details or chapters call waits for the other.
private const val UPDATE_GATHER_MS = 150L

// An image response straight into the file SCANCORE's image cache names.
fun writeImage(
    response: Response,
    target: File,
): JsonElement {
    if (!response.isSuccessful) throw HostError("The site answered HTTP ${response.code} for an image.", response.code)
    target.parentFile?.mkdirs()
    val temp = File(target.path + ".part")
    response.body.byteStream().use { input -> temp.outputStream().use { input.copyTo(it) } }
    if (!temp.renameTo(target)) {
        target.delete()
        temp.renameTo(target)
    }
    return buildJsonObject {
        put("contentType", response.header("Content-Type"))
        put("size", target.length())
    }
}

// A source's settings screen, from either API: Mihon's ConfigurableSource or
// Aniyomi's ConfigurableAnimeSource. Both keep their values in the
// SharedPreferences named by `preferenceKey` ("source_<id>").
class Configurable(
    val name: String,
    val preferenceKey: String,
    val preferences: () -> SharedPreferences,
    val setup: (PreferenceScreen) -> Unit,
) {
    companion object {
        fun of(source: ConfigurableSource) =
            Configurable(source.name, "source_${source.id}", { source.getSourcePreferences() }, source::setupPreferenceScreen)

        // Through the host's own default: extensions are compiled against an
        // interface without getSourcePreferences.
        fun of(source: ConfigurableAnimeSource) =
            Configurable(source.name, "source_${source.id}", { source.getSourcePreferences() }, source::setupPreferenceScreen)
    }
}

// A source's settings screen as JSON, and changes to it, with the platform's
// preference classes (AndroidCompat's on desktop, androidx on Android).
//   [{key, title, summary, enabled, type: switch|list|multiselect|text|info,
//     entries, values, value}]
interface SettingsBridge {
    fun describe(source: Configurable): JsonArray

    fun set(
        source: Configurable,
        key: String,
        value: JsonElement,
    )
}
