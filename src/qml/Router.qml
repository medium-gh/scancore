pragma Singleton
import QtQuick
import Scancore

// Navigation entry points used by every page. Main.qml installs itself as
// the shell on startup.
QtObject {
    property var shell: null
    property string section: "home"

    function go(name) { if (shell) shell.goSection(name) }
    // Comics, Novels and Anime show in the tabs and sidebar unless the
    // reader left them out (welcome, Settings → Interface); a link or key
    // still opens them.
    function sectionShown(section) {
        // Music is a preview, turned on in Settings → Interface.
        if (section === "music") return App.settings.musicSection === true
        const media = { comics: "comic", novels: "novel", anime: "anime" }[section]
        if (!media) return true
        const shown = Array.from(App.settings.shownMedia || [])
        return shown.length === 0 || shown.indexOf(media) >= 0
    }
    // Shows only these media (["comic", "novel", "anime"], at least one):
    // the tabs and sidebar, Home's sections and Retro's hub follow.
    function setShownMedia(media) {
        const list = ["comic", "novel", "anime"].filter(m => media.indexOf(m) >= 0)
        if (list.length === 0) return
        App.setSetting("shownMedia", list)
        const sections = { comic: "comics", novel: "novels", anime: "anime" }
        for (const kind of ["comic", "novel", "anime"])
            PageLayout.setRemoved("home", sections[kind], list.indexOf(kind) < 0)
        const kinds = { comics: "comic", novels: "novel", anime: "anime" }
        const current = Retro.sections().map(s => s.key)
        const wanted = current.filter(k => !kinds[k] || list.indexOf(kinds[k]) >= 0)
        for (const k of ["comics", "novels", "anime"])
            if (list.indexOf(kinds[k]) >= 0 && wanted.indexOf(k) < 0) wanted.push(k)
        Retro.setSections(wanted)
    }
    // Music on or off (welcome, Settings → Interface); Retro's hub gets its
    // section too.
    function setMusicShown(on) {
        if ((App.settings.musicSection === true) === on) return
        App.setSetting("musicSection", on)
        const hub = Retro.sections().map(s => s.key)
        if (on && hub.indexOf("music") < 0) Retro.setSections(hub.concat(["music"]))
    }
    // A page on top of the current section's stack.
    function push(path, props) { if (shell) shell.push(path, props || {}) }
    function back() { if (shell) shell.back() }
    // A page done: its own window closes (openWindow), else the shell
    // goes back.
    function leave(page) {
        const w = page && page.Window ? page.Window.window : null
        if (w && w.isPageWindow === true) w.close()
        else if (shell && page && page === shell.splitItem) shell.closeSplit()
        else back()
    }
    // The next reader opened goes beside the one that is open (a reader's
    // Side by side), not over it.
    property bool toSplit: false
    // Computers: `path` (a reader, the player, a page) in a window of its own.
    readonly property bool windows: App.platform !== "android"
    function openWindow(path, props) {
        const component = Qt.createComponent(Qt.resolvedUrl("components/PageWindow.qml"))
        if (component.status !== Component.Ready) {
            console.warn("PageWindow:", component.errorString())
            return null
        }
        return component.createObject(null, { page: path, props: props || {} })
    }
    // A comic or book file: an archive, folder or PDF of pages, or a book
    // (EPUB, FB2, MOBI).
    function openFile(url) {
        // A plain path (C:\… or /…) as a file:// link.
        let address = String(url)
        if (/^(\/|[A-Za-z]:[\\/])/.test(address)) address = "file:///" + address.replace(/\\/g, "/").replace(/^\/+/, "")
        const name = decodeURIComponent(address.replace(/[?#].*$/, "").replace(/\/$/, "").replace(/^.*\//, ""))
        const media = /\.(epub|fb2|fb2\.zip|mobi|azw|azw3|prc)$/i.test(name) ? "novel" : "comic"
        openSeries({ media: media, sourceUrl: address, source: "local", title: name.replace(/(\.fb2)?\.[^.]+$/i, "").replace(/_/g, " ") }, null)
    }
    // Settings, at one category.
    function openSettings(category) {
        App.setSetting("settingsCategory", category)
        // Phones list the categories first; the page opens that one.
        settingsJump = category
        go("settings")
    }
    property string settingsJump: ""
    // The "Add repository" sheet, for a link or (empty) to paste one.
    function addRepository(link) { if (shell) shell.addRepository(link || "") }
    // A repository link pasted into a search box opens the "Add repository"
    // sheet instead of searching for it. Returns whether it did.
    function takeRepositoryLink(text) {
        const t = String(text).trim()
        if (!/^(scancore|tachiyomi|mihon|aniyomi|lnreader|cloudstreamrepo):\/\//i.test(t)
                && !/^https?:\/\/\S+\/(index\.min\.json|repo\.json|plugins\.min\.json|plugins\.json)$/i.test(t))
            return false
        addRepository(t)
        return true
    }
    // The library's categories, as anime or reading media call them.
    // The built-in lists, then the user's own (custom: true).
    // The library's lists in the user's order: [{value, label, custom,
    // hidden, renamed}]. Hidden ones only with includeHidden (Settings) or
    // when `keep` is one of them (the list a series is on).
    function libraryCategories(media, includeHidden, keep) {
        App.dependOn(Library.listsRevision)
        const anime = media === "anime"
        const layout = Library.listLayout()
        const names = layout.names || {}
        const hidden = Array.from(layout.hidden || [])
        const builtin = { reading: anime ? qsTr("Watching") : qsTr("Reading"),
                          planned: anime ? qsTr("Plan to watch") : qsTr("Plan to read"),
                          paused: qsTr("On hold"), completed: qsTr("Completed"), dropped: qsTr("Dropped") }
        const own = {}
        for (const c of Array.from(Library.customCategories())) own[c.id] = c.name
        const out = []
        for (const id of Array.from(layout.order || [])) {
            const isBuiltin = builtin[id] !== undefined
            if (!isBuiltin && own[id] === undefined) continue
            const isHidden = hidden.indexOf(id) >= 0
            if (isHidden && !includeHidden && id !== keep) continue
            out.push({ value: id, label: isBuiltin ? (names[id] || builtin[id]) : own[id], custom: !isBuiltin,
                       hidden: isHidden, renamed: isBuiltin && !!names[id], defaultLabel: isBuiltin ? builtin[id] : "" })
        }
        return out
    }
    // One source's own popular, latest and filtered lists.
    function openSource(key) { push("pages/SourcePage.qml", { sourceKey: key }) }
    function openExtensions() { push("pages/ExtensionsPage.qml", {}) }
    function openExtensionsDiagnostics() { push("pages/ExtensionsDiagnostics.qml", {}) }
    function toast(message, kind, action) { if (shell) shell.toast(message, kind || "info", action) }

    // An anime from an extension's source (Aniyomi, CloudStream, a
    // Jellyfin server) rather than AniList's.
    function extensionAnime(source) { return /^(aniyomi|cloudstream):/.test(String(source || "")) }

    // An AniList anime's id from any shape of it: a catalogue card, a saved
    // series (extra.anilistId), or a brief library row that has only its id
    // ("anime:anilist:<id>") and address. 0 when it has none.
    function anilistIdOf(item) {
        if (!item) return 0
        const direct = Number(item.anilistId || (item.extra && item.extra.anilistId) || 0)
        if (direct > 0) return direct
        const m = /^anime:anilist:(\d+)$/.exec(String(item.id || "")) || /anilist\.co\/anime\/(\d+)/.exec(String(item.sourceUrl || ""))
        return m ? Number(m[1]) : 0
    }

    // item: a search/library map with id, media, sourceUrl, title, cover.
    function openSeries(item, fromCover) {
        if (!shell || !item) return
        if (item.media === "anime" && Router.extensionAnime(item.source))
            shell.push("pages/AnimeDetails.qml", { sourceUrl: item.sourceUrl || item.url, summary: item }, fromCover, item.cover)
        else if (item.media === "anime")
            shell.push("pages/AnimeDetails.qml", { anilistId: anilistIdOf(item), summary: item }, fromCover, item.cover)
        else if (item.media === "novel")
            shell.push("pages/NovelDetails.qml", { url: item.sourceUrl || item.url, summary: item }, fromCover, item.cover)
        else
            shell.push("pages/ComicDetails.qml", { url: item.sourceUrl || item.url, summary: item }, fromCover, item.cover)
    }

    function openComicReader(series, chapters, index) { openReader("pages/ComicReader.qml", series, chapters, index) }
    function openNovelReader(series, chapters, index) { openReader("pages/NovelReader.qml", series, chapters, index) }
    function openReader(path, series, chapters, index) {
        if (!shell) return
        const props = { series: series, chapters: chapters, index: index }
        if (toSplit && shell.canSplit) {
            toSplit = false
            shell.openSplit(path, props)
        } else {
            toSplit = false
            shell.openImmersive(path, props)
        }
    }

    // SCANCORE Wrapped for a year, as a story over the whole window.
    function openWrapped(year) {
        if (shell) shell.openImmersive("pages/Wrapped.qml", { year: year })
    }
    // The year Wrapped is in season for: December and the first week of
    // January show the year just ending; any other time -1.
    function wrappedYear(now) {
        // Development hook: SCANCORE_START {"wrappedSeason": year}.
        if (!now && App.startAction.wrappedSeason) return App.startAction.wrappedSeason
        const d = now || new Date()
        if (d.getMonth() === 11) return d.getFullYear()
        if (d.getMonth() === 0 && d.getDate() <= 7) return d.getFullYear() - 1
        return -1
    }

    // A watch party's anime (Party.state), for someone who joined from
    // elsewhere: its page, which starts the host's episode.
    function openPartyAnime(state) {
        if (!shell || !state || !state.anime) return
        shell.push("pages/AnimeDetails.qml", { anilistId: state.anime.anilistId,
                                               summary: { title: state.anime.title, cover: state.anime.cover || "" },
                                               partyEpisode: Number(state.episode) || 1 })
    }
    // The song playing, full screen; with a song, that song (and, with
    // `video`, its video).
    function openNowPlaying(song, video) {
        if (song && (!MusicQueue.current || MusicQueue.current.id !== song.id)) MusicQueue.playSong(song)
        if (shell) shell.openImmersive("pages/NowPlayingPage.qml", { startWithVideo: !!video })
    }
    function openWatch(media, episodes, index) {
        if (shell) shell.openImmersive("pages/WatchPage.qml", { media: media, episodes: episodes, index: index })
    }

    // AniList's sign-in inside SCANCORE (see AniListSignIn.qml).
    function openAniListSignIn() {
        if (shell) shell.openImmersive("pages/AniListSignIn.qml", {})
    }

    // Lets the reader pass a site's browser check (see SiteCheck.qml).
    function openSiteCheck(url) {
        // Never SCANCORE's own site or the sync server (the account's session).
        if (App.protectedSite(url)) {
            toast(qsTr("SCANCORE does not open browser checks for its own site or your sync server."), "error")
            return
        }
        if (App.platform === "android") {
            // Android's own web view; its cookies come back to SCANCORE.
            const host = url.replace(/^https?:\/\//, "").replace(/\/.*$/, "")
            if (!App.openSiteCheck(url, qsTr("Browser check for %1. Complete any check the site shows, then press Done.").arg(host), qsTr("Done")))
                App.openUrl(url)
            return
        }
        if (shell && url) shell.openImmersive("pages/SiteCheck.qml", { url: url })
    }

    function openAnimeBrowse(filters) {
        if (shell) shell.push("pages/AnimeBrowse.qml", { initialFilters: filters || {} })
    }

    function openUpcoming() {
        if (shell) shell.push("pages/Upcoming.qml", {})
    }
    function openLibraryCare() {
        if (shell) shell.push("pages/LibraryCare.qml", {})
    }
    function openAnimeSchedule() {
        if (shell) shell.push("pages/AnimeSchedule.qml", {})
    }

    // For pages that refresh a list in the background: the old list when
    // the new one holds the same, so views keep their items (and scroll
    // position) instead of rebuilding them.
    // Each list remembers its own text, so a refresh writes only the new one
    // (a 2000-series library took a noticeable moment on phones). Long
    // lists come with their task's digest, made off the interface's thread,
    // which stands in for the text.
    // A task's rows, or `old` when its digest says nothing changed: then
    // the rows are never converted to JavaScript (2000+ library rows took
    // about 18 ms each load). Read in an onFinished without parameters.
    function keepTask(old, task) {
        if (old && typeof old === "object" && task.digest && old.__keepText === task.digest) return old
        return keep(old, task.result, task.digest)
    }
    function keep(old, fresh, digest) {
        if (!fresh || typeof fresh !== "object") return fresh
        const text = digest || JSON.stringify(fresh)
        if (old && typeof old === "object" && (old.__keepText || JSON.stringify(old)) === text) return old
        try { Object.defineProperty(fresh, "__keepText", { value: text, enumerable: false, writable: true }) } catch (e) {}
        return fresh
    }

    // A catalogue shelf's title by its key (Comics.catalogFeeds).
    function shelfTitle(key) {
        switch (key) {
        case "trending": return qsTr("Trending this week")
        case "popular": return qsTr("Most popular")
        case "top": return qsTr("Top rated")
        case "newest": return qsTr("New in the catalogue")
        case "completed": return qsTr("Recently completed")
        case "manhwa": return qsTr("Top manhwa")
        case "manhua": return qsTr("Top manhua")
        case "lightnovels": return qsTr("Top light novels")
        }
        return key
    }

    // Incognito: nothing read or watched is recorded (Library, Activity,
    // Presence check the setting); what is showing on Discord goes now.
    function setIncognito(on) {
        App.setSetting("incognito", on)
        if (on) Presence.clear()
        toast(on ? qsTr("Incognito is on: nothing you read or watch is recorded") : qsTr("Incognito is off"), "info")
    }

    // Everything a theme file carries beyond its colours and background
    // (Customize.h): light or dark, accent, layout, arranged pages and the
    // readers' colours.
    readonly property var lookSettings: ["uiMode", "retroTitle", "retroTitleColor", "retroBackground", "retroCase",
        "retroLiveTiles", "retroSections", "navStyle", "navSide", "dockSide", "pageSpacing", "roundness", "coverSize",
        "motionSpeed", "textScale", "readerProgressWidth", "readerRailUnread", "readerRailRead", "readerRailCurrent",
        "readerProgressLook", "readerProgressEdge", "readerProgressOpacity",
        "novelTheme", "novelCustomBg", "novelCustomText"]
    readonly property var arrangedPages: ["home", "comics", "comics-top", "novels", "novels-top", "anime"]
    // What the ones with no stored value look like, so a file resets them too.
    readonly property var lookDefaults: ({ uiMode: "standard", navStyle: "sidebar", navSide: "left", dockSide: "bottom", pageSpacing: "default",
        roundness: "default", coverSize: "default", motionSpeed: "default", textScale: 1 })
    function currentLook() {
        const settings = {}
        for (const k of lookSettings) {
            const v = App.settings[k] !== undefined && App.settings[k] !== "" ? App.settings[k] : lookDefaults[k]
            if (v !== undefined) settings[k] = v
        }
        const pages = {}
        for (const p of arrangedPages) {
            const stored = PageLayout.stored(p)
            if (Object.keys(stored).length) pages[p] = stored
        }
        return { mode: App.settings.theme, accentName: App.settings.accent, settings: settings, pages: pages }
    }

    // Switches to a theme read from a .sct file (Customize.importTheme).
    function applyTheme(theme) {
        const t = Object.assign({}, theme)
        const accent = t.accent, preset = t.preset
        // Files from 2.12.0 call the Retro look "Metro".
        const look = t.look ? Object.assign({}, t.look) : t.look
        if (look) {
            if (String(look.accentName || "").startsWith("metro-")) look.accentName = "retro-" + look.accentName.slice(6)
            const settings = {}
            for (const k in (look.settings || {}))
                settings[k.startsWith("metro") ? "retro" + k.slice(5) : k] = k === "uiMode" && look.settings[k] === "metro" ? "retro" : look.settings[k]
            look.settings = settings
        }
        delete t.accent
        delete t.look
        delete t.preset
        if (!preset) {
            App.setSetting("customTheme", t)
            App.setSetting("theme", "custom")
        } else if (look && look.mode && look.mode !== "custom") {
            App.setSetting("theme", look.mode)
        }
        if (accent) {
            App.setSetting("customAccent", accent)
            App.setSetting("accent", "custom")
        } else if (look && look.accentName && (Theme.accents[look.accentName] || Theme.retroAccents[look.accentName])) {
            App.setSetting("accent", look.accentName)
        }
        // A version 2 file is the whole look: pages it does not arrange go
        // back to their defaults.
        if (look) {
            const settings = look.settings || {}
            for (const k of lookSettings) if (settings[k] !== undefined) App.setSetting(k, settings[k])
            const pages = look.pages || {}
            for (const p of arrangedPages) {
                if (pages[p]) PageLayout.save(p, pages[p])
                else PageLayout.reset(p)
            }
        }
        const name = t.name ? "“" + t.name + "”" : qsTr("The theme")
        if (!preset && t.windowOpacity < 1 && !Customize.translucentWindow && Customize.canRestart)
            toast(name + qsTr(" is on. Restart SCANCORE to make the window see-through."), "success")
        else
            toast(name + qsTr(" is on"), "success")
    }

    // Resume an item from history or the library at its last unit.
    function resume(entry) {
        if (!shell || !entry) return
        shell.resume(entry)
    }
}
