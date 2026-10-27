import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Scancore

// Anime player. Episodes from anime extensions play in SCANCORE's own player
// (NativePlayer, libmpv): an extension's own anime from its source, an
// AniList anime from the source picked in "Watch from". Otherwise ReAnime's
// streams are the provider's own player page, shown in an embedded browser.
// Either way SCANCORE follows the position for resume, auto-next and
// skipping intros (AniSkip, or the video's own chapters).
//
// Desktop with Qt WebEngine (Linux): the video fills the page and the top
// bar and episode list float over it, fading out after a few seconds without
// mouse movement.
// System web views (phones, and Windows and macOS desktops) are native views
// drawn above everything Qt renders, so nothing can overlap them.
// Desktops dock the bar above the video and the episode list beside it; in
// full screen they hide until the pointer reaches the top edge.
// Phone portrait puts the controls and episodes below a 16:9 player;
// landscape is full screen, its controls over the video (over a system web
// view, which nothing can cover, a strip above it that shows on its own:
// taps there are the web player's). Hidden controls take no taps: the first
// tap only shows them. Settings and actions are one panel
// (PlayerPanel).
Rectangle {
    id: watch
    property var media: ({})
    property var episodes: []
    property int index: 0

    readonly property var episode: episodes[index] || ({})
    readonly property bool hasPrev: index > 0
    readonly property bool hasNext: index < episodes.length - 1
    // An extension's own anime (opened from its source) plays from there;
    // an AniList anime from the source picked for it, else the built-in
    // servers ({builtIn: true}).
    readonly property bool fromSource: Router.extensionAnime(media.source)
    property var provider: fromSource ? { source: media.source, url: media.sourceUrl, sourceLabel: Anime.sourceLabel(media.source) }
                                      : Anime.provider(media.anilistId || 0)
    // A downloaded episode plays from its file, with SCANCORE's player.
    property bool offline: false
    readonly property bool nativeStream: !!provider.source || offline
    readonly property bool hasSources: { App.dependOn(Extensions.revision); return Anime.sources().some(s => s.enabled) }
    // "Watch from" is asked once, the first time an episode plays here, unless
    // tracker anime play from the built-in servers (Settings → Anime).
    property bool asked: false
    readonly property string trackerPlayer: Anime.playerEngine === "" ? "extensions" : (App.settings.animeTrackerPlayer || "builtin")
    readonly property bool finding: watchFrom.quiet && !!watchFrom.task && watchFrom.task.loading
    readonly property var series: fromSource ? media : Anime.seriesFor(media)
    property var streamsTask: null
    // Extensions: the episode's videos as its hosters answer, and the one
    // chosen to play (with its headers and tracks).
    property var videosTask: null
    property var videoState: null
    property var resolveTask: null
    property var hosterTasks: []
    property string currentId: ""
    property var currentVideo: ({})
    property var failedIds: ({})
    property string playError: ""
    property var skipTask: null
    property string currentUrl: ""
    property real position: 0
    property real duration: 0
    property real resumeAt: 0
    property bool sideOpen: false
    property bool ended: false

    readonly property bool phone: App.platform === "android"
    readonly property bool landscape: width > height
    readonly property bool fullScreen: Window.window !== null && Window.window.visibility === Window.FullScreen
    readonly property bool systemView: !nativeStream && Anime.playerEngine === "system"
    // Desktop layout for a native player view (SCANCORE_START
    // {"playerLayout": "docked"} tries it with Qt WebEngine).
    readonly property bool docked: !phone && (systemView || App.startAction.playerLayout === "docked")
    // The embedded browser cannot decode this video (see WebPlayer).
    property bool unsupported: false
    readonly property var progress: { App.dependOn(Library.revision); return Library.progressMap(series.id) }

    // Desktop chrome auto-hide. Docked chrome only hides in full screen, as
    // hiding it resizes the video.
    property bool idle: false
    readonly property bool chromeShown: !player.item || unsupported || (nativeStream && !currentUrl) || (docked ? !fullScreen || !idle : !idle)
    // Shown long enough to be meant: a tap that brings the controls back
    // must not also press the one under the finger (the player closed when
    // a hidden back button was pressed).
    property bool chromeSettled: chromeShown
    onChromeShownChanged: {
        chromeSettled = false
        if (chromeShown) settleTimer.restart()
    }
    Timer { id: settleTimer; interval: 350; onTriggered: watch.chromeSettled = watch.chromeShown }
    // Phone landscape: full screen, the controls over the video.
    readonly property bool immersed: phone && landscape

    // The server chips: ReAnime's servers, or every video of the hosters
    // that answered (and a chip for each hoster that only loads on request).
    readonly property var streams: {
        if (!nativeStream) return streamsTask && streamsTask.ok ? streamsTask.result.streams : []
        const out = []
        for (const h of (videoState ? videoState.hosters : [])) {
            if (h.state === "idle") out.push({ id: h.id, quality: h.name + "…", hoster: h.name, type: "hoster" })
            for (const v of (h.videos || []))
                if (!v.localServer)
                    out.push({ id: v.id, url: v.url, quality: (h.name ? h.name + " · " : "") + (v.title || qsTr("Video")), hoster: h.name, type: "video", video: v })
        }
        return out
    }
    // Where tracker anime can play from, as chips in the server area: the
    // built-in servers, the source in use and a few more (the ones ticked
    // for matching first), never every installed extension; More… has
    // the rest.
    readonly property var sourceChips: {
        if (fromSource || !hasSources) return []
        const all = Anime.sources().filter(s => s.enabled)
        const keys = []
        if (provider.source) keys.push(provider.source)
        for (const k of Array.from(App.settings.animeMatchSources || []))
            if (keys.length < 4 && keys.indexOf(k) < 0 && all.some(s => s.key === k)) keys.push(k)
        for (const s of all)
            if (keys.length < 4 && keys.indexOf(s.key) < 0) keys.push(s.key)
        const out = Anime.playerEngine !== "" ? [{ key: "", label: qsTr("Built-in") }] : []
        for (const k of keys) out.push({ key: k, label: Anime.sourceLabel(k) })
        if (all.length > keys.length) out.push({ key: "more", label: qsTr("More…") })
        return out
    }
    function pickSource(key) {
        if (key === "more") return watchFrom.start(media, false)
        if (key === (provider.source || "")) return
        if (!key) {
            Anime.setProvider(media.anilistId || 0, {})
            provider = { builtIn: true }
            load(index)
            return
        }
        watchFrom.startWith(media, [key])
    }

    // A video's own chapters, else AniSkip's.
    readonly property var skips: currentVideo.skips && currentVideo.skips.length ? currentVideo.skips
                               : skipTask && skipTask.ok ? skipTask.result : []
    readonly property var activeSkip: {
        for (const s of skips)
            if (position >= s.start && position < s.end - 1) return s
        return null
    }
    readonly property string skipLabel: activeSkip ? (activeSkip.type.indexOf("ed") >= 0 ? qsTr("Skip ending") : activeSkip.type === "recap" ? qsTr("Skip recap") : qsTr("Skip intro")) : ""
    // Settings → Anime: intros and recaps, and endings, can each be skipped
    // without asking; the Skip button is for the rest.
    function skipsItself(s) {
        if (!s) return false
        if (s.type === "ed" || s.type === "mixed-ed") return App.settings.autoSkipEndings === true
        return App.settings.autoSkip === true
    }
    readonly property bool showSkip: activeSkip !== null && !skipsItself(activeSkip)
    readonly property string episodeLine: qsTr("Episode ") + (episode.number || "") + (episode.title && !String(episode.title).startsWith("Episode") ? " · " + episode.title : "")
        + (offline ? qsTr(" · Downloaded") : "") + (App.settings.incognito === true ? qsTr(" · Incognito") : "")

    color: "#000000"
    focus: true

    function load(i) {
        saveProgress(true)
        nextCountdown.stop()
        index = i
        position = 0
        duration = 0
        ended = false
        unsupported = false
        skipTask = null
        const p = Library.progressMap(series.id)[episodes[i].key]
        resumeAt = p && !p.completed && p.position ? (p.position.seconds || 0) : 0
        currentUrl = ""
        currentId = ""
        currentVideo = ({})
        failedIds = ({})
        playError = ""
        videoState = null
        resolveTask = null
        hosterTasks = []
        pickGrace.stop()
        streamsTask = null
        videosTask = null
        const local = Downloads.localVideo(series.id || "", episodes[i].key || "")
        offline = !!local.url
        if (offline) {
            currentVideo = local
            currentId = "offline"
            currentUrl = local.url
        } else if (nativeStream)
            videosTask = Anime.videos(provider.source, fromSource ? episodes[i].url : "", fromSource ? "" : provider.url, Number(episodes[i].number))
        else if (!fromSource && !provider.builtIn && hasSources && !asked && trackerPlayer !== "builtin") {
            asked = true
            // Extensions: picks by itself when one clearly has it; ask: shows the choice.
            watchFrom.start(media, trackerPlayer === "extensions")
        } else
            streamsTask = Anime.streams(media.anilistId, episodes[i].number)
        Presence.show("anime", series.title || "", qsTr("Episode ") + (episodes[i].number || ""))
        if (phone && !landscape) phoneList.positionViewAtIndex(i, ListView.Contain)
    }

    // The screen stays on while the episode plays: SCANCORE's player says
    // so, a web player while its ticks keep coming and it is not paused.
    property bool webPlaying: false
    readonly property bool keepScreenOn: !casting && Qt.application.state === Qt.ApplicationActive
        && (player.item && player.item.playing !== undefined ? player.item.playing : webPlaying)
    onKeepScreenOnChanged: App.keepAwake("watch", keepScreenOn)
    Timer { id: webTick; interval: 5000; onTriggered: watch.webPlaying = false }

    Component.onCompleted: {
        load(index)
        if (phone && landscape) Window.window.showFullScreen()
        wake()
        Activity.setMedia("anime")
        Party.watching = media.anilistId || 0
    }
    // However the player is closed (Back, Esc, the arrow), leave full screen.
    Component.onDestruction: {
        App.keepAwake("watch", false)
        Activity.setMedia("")
        Presence.clear()
        if (Party.watching === (media.anilistId || 0)) Party.watching = 0
        saveProgress(true)
        if (castDevice) Cast.stop()
        if (phone && fullScreen) Window.window.showNormal()
        if (phone) App.setOrientation("")
        // The brightness a swipe set was the player's only.
        if (phone) App.setScreenBrightness(-1)
    }

    // Phones: landscape is full screen, by turning the phone or with the
    // full screen buttons (which turn it, even with auto-rotate off).
    onLandscapeChanged: {
        if (!phone) return
        if (landscape) Window.window.showFullScreen()
        else Window.window.showNormal()
    }

    Connections {
        target: watch.streamsTask
        function onFinished(result) {
            // The server picked last time, when this episode has it and it
            // is not the other audio (Settings → Anime); else the first with
            // the audio asked for.
            const preferred = result.streams.find(s => s.quality === App.settings.animeServer && VideoPrefs.audioFit(s.quality) >= 0)
            const byAudio = VideoPrefs.audio ? result.streams.find(s => VideoPrefs.audioFit(s.quality) > 0) : null
            if (result.streams.length > 0) watch.currentUrl = (preferred || byAudio || result.streams[0]).url
        }
    }

    function pickServer(stream) {
        if (!stream) return
        if (stream.type === "hoster") return loadHoster(stream)
        if (stream.type === "video") {
            // A new server goes on from where this one was.
            if (position > 5) resumeAt = position
            return playVideo(stream)
        }
        currentUrl = stream.url
        App.setSetting("animeServer", stream.quality)
    }

    // What plays first, from the hosters that answered: the audio asked
    // for (Settings → Anime) above all, then the server picked last time,
    // then the picture height asked for (else the tallest), as
    // VideoPrefs.score ranks names. It plays once every hoster answered, or
    // the server picked last time did, or (with no audio asked for) the
    // source marks a video as its preferred one; otherwise a few seconds
    // after the first video turns up.
    function rankVideos(state) {
        const wanted = App.settings.animeHoster || ""
        let top = null
        let topScore = -Infinity
        for (const h of (state ? state.hosters : [])) {
            if (h.state !== "ready") continue
            for (const v of (h.videos || [])) {
                if (v.localServer || failedIds[v.id]) continue
                const s = VideoPrefs.score((h.name || "") + " " + (v.title || ""), v.preferred) + (wanted && h.name === wanted ? 300 : 0)
                if (s > topScore) {
                    top = { video: v, hoster: h.name || "" }
                    topScore = s
                }
            }
        }
        return top
    }
    function applyVideos(result, done) {
        videoState = result
        if (currentId) return
        const wanted = App.settings.animeHoster || ""
        const h = wanted ? result.hosters.findIndex(x => x.name === wanted) : -1
        if (h >= 0 && result.hosters[h].state === "loading" && !done) return
        const top = rankVideos(result)
        if (!top) return
        const settled = done || (h >= 0 && top.hoster === wanted) || (top.video.preferred && !VideoPrefs.audio)
        if (settled) {
            pickGrace.stop()
            playVideo(streams.find(s => s.id === top.video.id))
        } else if (!pickGrace.running)
            pickGrace.start()
    }
    Timer {
        id: pickGrace
        interval: 4000
        onTriggered: if (!watch.currentId) {
            const top = watch.rankVideos(watch.videoState)
            if (top) watch.playVideo(watch.streams.find(s => s.id === top.video.id))
        }
    }

    // The playing server's other videos (its resolutions, usually), for
    // the player's Quality menu.
    readonly property var alternatives: {
        const playing = streams.find(s => s.id === currentId)
        if (!playing || playing.type !== "video") return []
        return streams.filter(s => s.type === "video" && s.hoster === playing.hoster && !failedIds[s.id])
            .map(s => ({ id: s.id, label: s.video.title || s.quality, height: VideoPrefs.heightOf(s.video.title || ""), current: s.id === currentId }))
    }

    function playVideo(stream) {
        if (!stream) return
        currentId = stream.id
        playError = ""
        // Jellyfin: where the server says the episode was left (in another
        // of its apps, say), unless it already plays here.
        if (position <= 5 && (stream.video.resume || 0) > 5) resumeAt = stream.video.resume
        App.setSetting("animeHoster", stream.hoster || "")
        // Only the video that plays asks for its final address.
        if (stream.video.initialized === false || !stream.url) {
            currentUrl = ""
            const task = Anime.resolveVideo(provider.source, stream.id)
            resolveTask = task
            task.finished.connect((v) => {
                if (watch.resolveTask !== task) return
                watch.currentVideo = v
                watch.currentUrl = v.url
            })
            task.failed.connect((error) => { if (watch.resolveTask === task) watch.videoFailed(error, task.status) })
        } else {
            resolveTask = null
            currentVideo = stream.video
            currentUrl = stream.url
        }
    }

    // A hoster that only lists its videos when asked.
    function loadHoster(stream) {
        const task = Anime.hosterVideos(provider.source, stream.id)
        hosterTasks = hosterTasks.concat([task])
        task.finished.connect((videos) => {
            if (!watch.videoState) return
            const state = Object.assign({}, watch.videoState)
            state.hosters = state.hosters.map(h => h.id === stream.id ? Object.assign({}, h, { state: "ready", videos: videos }) : h)
            watch.videoState = state
            const first = videos.find(v => !v.localServer)
            if (first) watch.playVideo(watch.streams.find(s => s.id === first.id))
        })
        task.failed.connect((error) => Router.toast(error, "error"))
    }

    // The video would not play: the next one does, if there is one. After
    // the extension host restarted, the episode's videos are asked again.
    function videoFailed(message, status) {
        if (status === 410) return load(index)
        const failed = Object.assign({}, failedIds)
        failed[currentId] = true
        failedIds = failed
        const next = streams.find(s => s.type === "video" && !failedIds[s.id])
        currentId = ""
        currentUrl = ""
        if (next) {
            Router.toast(qsTr("Trying another server: ") + message, "info")
            playVideo(next)
        } else
            playError = message
    }

    // This episode for watching offline: the video playing now (the source
    // is asked again if its address has expired by then).
    readonly property var downloadJob: { App.dependOn(Downloads.revision); return (Downloads.states(series.id || "")[episode.key] || null) }
    function downloadEpisode() {
        Downloads.enqueue(series, [{ key: episode.key, title: episode.title, url: episode.url || "",
            download: Object.assign({ source: provider.source, video: currentVideo },
                                    fromSource ? { episodeUrl: episode.url } : { animeUrl: provider.url, number: Number(episode.number) }) }])
        Router.toast(qsTr("Downloading episode %1").arg(episode.number), "info")
    }

    // Other players (mpv, VLC, IINA; on Android the chooser of player apps).
    readonly property var outsidePlayers: Anime.externalPlayers()
    readonly property bool canOpenOutside: nativeStream && outsidePlayers.length > 0 && currentUrl.length > 0
                                           && !(offline && App.platform === "android")
    function openOutside(p) {
        const video = Object.assign({}, currentVideo, { url: currentUrl })
        if (Anime.openExternally(p.id, video, (series.title || "") + " · " + qsTr("Episode ") + (episode.number || ""), position)) {
            saveProgress(true)
            if (player.item && player.item.player) player.item.player.paused = true
            // The stream reaches the other player through SCANCORE.
            if (Anime.relayedExternally(video))
                Router.toast(qsTr("Keep SCANCORE open while it plays there: the video comes through it."), "info")
        } else
            Router.toast(qsTr("Could not open it there."), "error")
    }
    function pickOutside(anchor) {
        if (outsidePlayers.length === 1) return openOutside(outsidePlayers[0])
        moreMenu.openAt(anchor, 0, anchor.height, outsidePlayers.map(p => ({ text: p.name, icon: "external-link", run: () => openOutside(p) })))
    }
    // Watch parties (Party): the host's player goes out on every tick and
    // its changes; someone playing along follows it (episode, position, and
    // with SCANCORE's player, play and pause).
    readonly property var partyAnime: ({ anilistId: media.anilistId || 0, title: series.title || media.title || "",
                                         cover: series.cover || media.cover || "", episode: Number(episode.number) || 1 })
    readonly property bool playingNow: casting ? Cast.state === "playing" : nativeStream ? !!player.item && player.item.playing === true : webPlaying
    readonly property bool following: Party.active && !Party.hosting && !!Party.state.anime && Party.state.anime.anilistId === (media.anilistId || 0)
    function reportParty() {
        if (Party.hosting && media.anilistId)
            Party.report(partyAnime, Number(episode.number) || 0, playingNow, position)
    }
    onPlayingNowChanged: reportParty()
    function followParty() {
        if (!following) return
        const s = Party.state
        const i = episodes.findIndex(e => Number(e.number) === Number(s.episode))
        if (i >= 0 && i !== index) {
            load(i)
            return
        }
        // Once the video is there (followParty again when it says how long it is).
        if (!player.item || duration <= 0 || casting) return
        const target = Math.min(Party.hostPosition(), duration - 1)
        if (Math.abs(target - position) > 2.5) player.seek(target)
        if (nativeStream && player.item.setPlaying) player.item.setPlaying(s.playing === true)
    }
    Connections {
        target: Party
        function onStateReceived() { watch.followParty() }
        function onMessageReceived(name, text) { if (!partyPanel.opened) Router.toast(name + ": " + text, "info") }
        function onEnded() { Router.toast(Party.error || qsTr("The party is over."), "info") }
    }
    PartyPanel { id: partyPanel; anime: watch.partyAnime }

    // Phones: what does not fit on the bar.
    function moreItems() {
        const items = []
        if (!App.settings.incognito)
            for (const d of Live.devices)
                items.push({ text: qsTr("Continue on %1").arg(d.name), icon: d.platform === "android" ? "smartphone" : "monitor", run: () => {
                    saveProgress(true)
                    if (Live.handoff(d.id, series.id || "")) Router.toast(qsTr("Sent to %1").arg(d.name), "success")
                    else Router.toast(qsTr("Nothing to send yet: read or watch a little first."), "info")
                } })
        if (nativeStream && !offline && !!provider.source && Anime.nativePlayerAvailable)
            items.push({ text: downloadJob ? qsTr("Downloading…") : qsTr("Download this episode"), icon: "download",
                         enabled: !downloadJob && currentUrl.length > 0, run: downloadEpisode })
        if (nativeStream && (currentUrl.length > 0 || casting))
            items.push(casting ? { text: qsTr("Stop casting"), icon: "cast", run: stopCasting }
                               : { text: qsTr("Cast to a TV"), icon: "cast", run: () => castPicker.open() })
        if (canOpenOutside)
            items.push({ text: qsTr("Open in another app"), icon: "external-link", run: () => openOutside(outsidePlayers[0]) })
        if (media.anilistId)
            items.push({ text: Party.active ? qsTr("Watch party (%1)").arg(Party.code) : qsTr("Watch together"), icon: "users", run: () => partyPanel.open() })
        if (media.anilistId)
            items.push({ text: qsTr("Comments"), icon: "message-circle", run: () => comments.open() })
        return items
    }
    ActionMenu { id: moreMenu }
    // Server names differ at their end (Sub or Dub, 720p): room for them.
    ActionMenu { id: serverMenu; implicitWidth: 400 }

    // Casting: the TV plays (through Cast) and this page follows it, saving
    // progress; another episode goes to the same TV.
    property string castDevice: ""
    readonly property bool casting: castDevice !== "" && Cast.active
    function castTo(device) {
        castDevice = device.id
        castCurrent()
    }
    function castCurrent() {
        if (!castDevice || !currentUrl) return
        Cast.play(castDevice, Object.assign({}, currentVideo, { url: currentUrl }),
                  (series.title || "") + " · " + qsTr("Episode ") + (episode.number || ""), series.cover || media.cover || "",
                  position > 5 ? position : resumeAt)
    }
    onCurrentUrlChanged: {
        if (castDevice && currentUrl) castCurrent()
        if (panel.opened) fillPanel()
    }
    function stopCasting() {
        // This device goes on from where the TV was.
        if (Cast.position > 5) resumeAt = Cast.position
        castDevice = ""
        Cast.stop()
    }
    Connections {
        target: Cast
        function onPositionChanged() {
            if (!watch.casting) return
            watch.position = Cast.position
            if (Cast.duration > 0) watch.duration = Cast.duration
            watch.saveProgress(false)
        }
        function onStateChanged() {
            if (!watch.casting || Cast.state !== "ended") return
            watch.position = watch.duration
            watch.saveProgress(true)
            if (watch.hasNext && App.settings.autoNext !== false) watch.load(watch.index + 1)
        }
    }
    CastPicker { id: castPicker; onPicked: (device) => watch.castTo(device) }

    function isCurrent(stream) {
        return stream.type === "video" ? currentId === stream.id : stream.type !== "hoster" && currentUrl === stream.url
    }

    WatchFromDialog {
        id: watchFrom
        onChosen: (picked) => {
            watch.provider = picked.source ? picked : { builtIn: true }
            watch.load(watch.index)
        }
        // Not picked: the built-in servers, this time.
        onDismissed: if (!watch.provider.source && !watch.streamsTask) {
            watch.provider = { builtIn: true }
            watch.load(watch.index)
        }
    }
    Connections {
        target: watch.videosTask
        function onProgressed(partial) { watch.applyVideos(partial, false) }
        function onFinished(result) { watch.applyVideos(result, true) }
    }

    property real lastSaved: 0
    function saveProgress(force) {
        if (!series.id || !episode.key || duration <= 0) return
        if (!force && Date.now() - lastSaved < 5000) return
        lastSaved = Date.now()
        const percent = Math.min(1, position / duration)
        Library.recordProgress(series, { key: episode.key, title: episode.title, number: String(episode.number) },
                               { type: "video", seconds: position, duration: duration }, percent, watched())
    }

    function onPlayerTime(seconds, total, status) {
        // Web players say how they are doing with every tick.
        webPlaying = status !== "Paused" && status !== "Ended"
        webTick.restart()
        if (casting) return
        if (total > 0 && duration === 0) {
            duration = total
            if (media.anilistId)
                skipTask = Anime.skipTimes(media.anilistId, episode.number, Math.round(total))
            // SCANCORE's player starts there by itself; web players are told.
            if (resumeAt > 5 && resumeAt < total - 30 && (nativeStream || seconds < resumeAt - 5)) {
                if (!nativeStream) player.seek(resumeAt)
                Router.toast(qsTr("Resumed at ") + formatTime(resumeAt))
            }
        }
        if (total > 0 && duration === 0 && following) Qt.callLater(followParty)
        if (total > 0) duration = total
        // Moving forward: the episode is playing, which counts as watching
        // time on the Activity page.
        if (seconds > position && seconds - position < 5) Activity.playing()
        position = seconds
        saveProgress(false)
        reportParty()
        if (skipsItself(activeSkip))
            player.seek(activeSkip.end)
        if ((status === "Ended" || (total > 0 && seconds >= total - 1)) && !ended) {
            ended = true
            saveProgress(true)
            if (App.settings.autoNext !== false && hasNext) {
                nextCountdown.start()
                wake()
            }
        }
    }

    function formatTime(s) {
        s = Math.floor(s)
        const m = Math.floor(s / 60)
        return m + ":" + String(s % 60).padStart(2, "0")
    }

    function toggleFullScreen() {
        if (phone) App.setOrientation(landscape ? "portrait" : "landscape")
        else if (fullScreen) Window.window.showNormal()
        else Window.window.showFullScreen()
    }

    function wake() {
        idle = false
        idleTimer.restart()
    }

    function exit() {
        saveProgress(true)
        if (fullScreen) Window.window.showNormal()
        Router.leave(watch)
    }

    // Android's Back: the panel closes (or the web player's own menu), then
    // full screen ends, then the player closes (Main asks before popping the
    // page). A TV skips the middle step: it is always on its side and cannot
    // turn to portrait, so Back only ever tried to and the player could not
    // be left.
    function handleBack() {
        if (panel.back() || comments.visible && (comments.close(), true)) return true
        // The site's own menu (quality, subtitles) in the web player.
        if (systemView && App.closeWebPlayerMenu()) return true
        if (phone && landscape && !App.tv) {
            toggleFullScreen()
            return true
        }
        if (fullScreen && !App.tv) {
            Window.window.showNormal()
            return true
        }
        saveProgress(true)
        return false
    }

    // A tap on the video (phones): the controls show, or hide again.
    function toggleChrome() {
        if (chromeShown && !idle) {
            idleTimer.stop()
            idle = true
        } else {
            wake()
        }
    }

    // The settings panel: the player's own settings, the server and the
    // episodes, then the page's actions.
    function openPanel(section) {
        fillPanel()
        panel.open(section)
        wake()
    }
    // What the panel lists; again while it is open when the episode or its
    // video changes (auto-next, a new server), so it never shows the last one.
    onIndexChanged: if (panel.opened) fillPanel()
    function fillPanel() {
        const sections = nativeStream && player.item && player.item.settingsSections ? player.item.settingsSections() : []
        if (streams.length > 1)
            sections.push({ key: "server", icon: "server", label: qsTr("Server"), value: (streams.find(s => isCurrent(s)) || { quality: "" }).quality, stay: false,
                            choices: streams.map(s => ({ label: s.quality, selected: isCurrent(s), pick: () => pickServer(s) })) })
        if (episodes.length > 1)
            sections.push({ key: "episodes", icon: "list", label: qsTr("Episodes"), value: qsTr("Episode ") + (episode.number || ""),
                            choices: episodes.map((e, i) => ({ label: qsTr("Episode ") + e.number, detail: e.title && !String(e.title).startsWith("Episode") ? e.title : "",
                                                               selected: i === index, pick: () => load(i) })) })
        const actions = []
        if (!fromSource && (hasSources || nativeStream))
            actions.push({ icon: "server", label: qsTr("Watch from…"), run: () => watchFrom.start(media, false) })
        for (const item of moreItems())
            actions.push({ icon: item.icon, label: item.text, enabled: item.enabled, run: item.run })
        panel.sections = sections
        panel.actions = actions
    }

    function leave() {
        if (fullScreen) Window.window.showNormal()
        else exit()
    }

    // Watched: most of it, or into the credits (the last two minutes, or the
    // ending's skip time). Moving on to the next episode past 70% counts too.
    property bool movedOn: false
    function watched() {
        if (duration <= 0) return false
        const percent = position / duration
        const ending = skips.find(s => s.type === "ed" || s.type === "mixed-ed")
        return percent >= 0.85 || (duration > 600 && duration - position <= 120) || (ending && position >= ending.start - 2)
            || (movedOn && percent >= 0.7)
    }
    function nextEpisode() {
        if (!hasNext) return
        nextCountdown.stop()
        movedOn = true
        saveProgress(true)
        movedOn = false
        load(index + 1)
    }

    // The embedded browser takes keyboard focus once the video is clicked and
    // keeps keys from reaching this page, so desktop keys are shortcuts. A
    // system web view keeps them from Qt altogether; its page passes them on.
    function pageKey(press) {
        if (press.key === "Escape") leave()
        else if (Keymap.matchesWeb("player.next", press)) nextEpisode()
        else if (Keymap.matchesWeb("player.prev", press) && hasPrev) load(index - 1)
    }
    function keyHint(action) {
        const keys = Keymap.labels(action)
        return keys.length ? " (" + keys[0] + ")" : ""
    }
    // The keys come from Settings → Keyboard (see Keymap).
    Shortcut { sequence: "Esc"; enabled: !watch.phone; onActivated: watch.leave() }
    Shortcut { sequences: Keymap.sequences("player.fullscreen"); enabled: !watch.phone; onActivated: watch.toggleFullScreen() }
    Shortcut { sequences: Keymap.sequences("player.next"); enabled: !watch.phone && watch.hasNext; onActivated: watch.load(watch.index + 1) }
    Shortcut { sequences: Keymap.sequences("player.prev"); enabled: !watch.phone && watch.hasPrev; onActivated: watch.load(watch.index - 1) }

    // Any mouse movement, including over the video, brings the chrome back.
    PointerMotion {
        onMoved: watch.wake()
    }
    Timer {
        id: idleTimer
        interval: 2500
        onTriggered: {
            if (barHover.hovered || sideHover.hovered || nextCountdown.running) restart()
            else watch.idle = true
        }
    }

    // ---------------------------------------------------------------- player
    // Full screen with the docked chrome hidden: a strip along the top edge
    // that the native view does not cover, so the pointer can bring it back.
    Item {
        id: edge
        visible: watch.docked && !watch.chromeShown
        width: parent.width
        height: 4
        HoverHandler { onHoveredChanged: if (hovered) watch.wake() }
    }

    Item {
        id: stage
        x: 0
        y: watch.phone ? (watch.landscape ? (strip.visible ? strip.height : 0) : bar.height)
         : watch.docked ? (watch.chromeShown ? bar.height : edge.visible ? edge.height : 0) : 0
        width: watch.docked ? watch.width - side.width : watch.width
        height: watch.phone && !watch.landscape ? Math.round(Math.min(watch.width * 9 / 16, watch.height * 0.45))
              : watch.height - y

        Loader {
            id: player
            anchors.fill: parent
            // A system web view draws above everything in the window, so it
            // steps aside while the comments panel is open.
            visible: !(watch.systemView && (comments.visible || panel.opened))
            // SCANCORE's player stays for the whole page (servers and
            // episodes change under it); a web player comes with its page.
            // One binding decides, so the two never mix while it changes.
            // Casting: the TV plays, so this one does not.
            source: watch.casting ? "" : watch.nativeStream ? "../components/NativePlayer.qml"
                  : watch.currentUrl.length === 0 || Anime.playerEngine === "" ? ""
                  : watch.systemView ? "../components/WebPlayerSystem.qml" : "../components/WebPlayer.qml"
            onLoaded: {
                item.url = Qt.binding(() => watch.currentUrl)
                if (!watch.nativeStream) return
                item.video = Qt.binding(() => watch.currentVideo)
                item.startAt = Qt.binding(() => watch.resumeAt > 5 ? watch.resumeAt : 0)
                item.chromeShown = Qt.binding(() => watch.chromeShown)
                item.alternatives = Qt.binding(() => watch.alternatives)
                item.hasPrev = Qt.binding(() => watch.hasPrev)
                item.hasNext = Qt.binding(() => watch.hasNext)
                item.panelOpen = Qt.binding(() => panel.opened)
                item.fullScreen = Qt.binding(() => watch.immersed || watch.fullScreen)
                // Phone landscape: the picture may go under a side cutout.
                item.bleedLeft = Qt.binding(() => watch.immersed && watch.StackView.view ? watch.StackView.view.cutoutLeft : 0)
                item.bleedRight = Qt.binding(() => watch.immersed && watch.StackView.view ? watch.StackView.view.cutoutRight : 0)
            }
            function seek(seconds) { if (item) item.seek(seconds) }
            Connections {
                target: player.item
                // NativePlayer has more to say than the web players.
                ignoreUnknownSignals: true
                function onTime(seconds, total, status) { watch.onPlayerTime(seconds, total, status) }
                // NativePlayer's controls in use (web players' taps are theirs).
                function onInteracted() { watch.wake() }
                function onKey(press) { watch.pageKey(press) }
                function onUnsupported() { watch.unsupported = true }
                function onFailed(message) { watch.videoFailed(message, 0) }
                function onFullScreenRequested() { watch.toggleFullScreen() }
                function onAlternativePicked(id) { watch.pickServer(watch.streams.find(s => s.id === id)) }
                function onSettingsRequested() { watch.openPanel() }
                function onPreviousRequested() { if (watch.hasPrev) watch.load(watch.index - 1) }
                function onNextRequested() { watch.nextEpisode() }
                function onTapped() { watch.toggleChrome() }
            }
        }
        CastOverlay {
            anchors.fill: parent
            z: 3
            visible: watch.casting
            title: qsTr("Episode ") + (watch.episode.number || "")
            hasNext: watch.hasNext
            onStopRequested: watch.stopCasting()
            onNextRequested: watch.load(watch.index + 1)
        }

        // This build's browser cannot play the video: say so, and offer the
        // system browser, which can. Drawn over the (Qt WebEngine) player.
        Rectangle {
            anchors.fill: parent
            visible: watch.unsupported && !!player.item
            color: "#000000"
            Column {
                anchors.centerIn: parent
                width: Math.min(460, parent.width - 48)
                spacing: 14
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "tv-minimal"
                    size: 34
                    color: "#a1a1aa"
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("This copy of SCANCORE cannot play this video")
                    color: "white"
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.textLg
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("Its built-in browser lacks the H.264 video codec. Watch this episode in your browser, or use ")
                        + qsTr("the SCANCORE Flatpak or your distribution's package, which include it.")
                    color: "#a1a1aa"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    lineHeight: 1.3
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8
                    ScButton {
                        variant: "primary"
                        text: qsTr("Open in browser")
                        iconName: "external-link"
                        onClicked: App.openUrl(watch.currentUrl)
                    }
                    ScButton {
                        text: qsTr("Get SCANCORE")
                        onClicked: App.openUrl("https://scancore.onl/desktop")
                    }
                }
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: 14
            visible: !player.item || (watch.nativeStream && !watch.currentUrl)
            Spinner {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: watch.finding || (watch.streamsTask !== null && watch.streamsTask.loading)
                         || (watch.videosTask !== null && watch.videosTask.loading) || (watch.resolveTask !== null && watch.resolveTask.loading)
                size: 30
                color: "#a1a1aa"
            }
            Text {
                width: Math.min(440, stage.width - 32)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: text.length > 0
                text: watch.finding ? qsTr("Looking for %1 on your anime sources…").arg(watch.media.title || "")
                    : watch.nativeStream ? (watch.playError || (watch.videosTask && !watch.videosTask.loading
                        ? (watch.videosTask.ok ? (watch.videosTask.result.message || "") : watch.videosTask.error) : ""))
                    : !watch.streamsTask ? ""
                    : Anime.playerEngine === "" ? qsTr("This build of SCANCORE has no embedded browser, which the ReAnime player needs.")
                    : !watch.streamsTask.loading ? (watch.streamsTask.ok ? (watch.streamsTask.result.message || "") : watch.streamsTask.error) : ""
                color: "#d4d4d8"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 8
                ScButton {
                    visible: (watch.streamsTask !== null && !watch.streamsTask.loading && watch.streams.length === 0)
                             || (watch.videosTask !== null && !watch.videosTask.loading && !watch.currentId)
                    text: qsTr("Try again")
                    iconName: "refresh-cw"
                    onClicked: watch.load(watch.index)
                }
                ScButton {
                    visible: !watch.fromSource && watch.hasSources && !watch.finding
                             && ((watch.streamsTask !== null && !watch.streamsTask.loading && watch.streams.length === 0)
                                 || (watch.videosTask !== null && !watch.videosTask.loading && !watch.currentId))
                    text: qsTr("Watch from…")
                    iconName: "server"
                    onClicked: watchFrom.start(watch.media, false)
                }
            }
        }

        // Desktop only: on phones these would sit under the native WebView.
        ScButton {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 40
            anchors.bottomMargin: 80
            visible: (!watch.phone || watch.immersed) && !watch.systemView && watch.showSkip
            variant: "primary"
            iconName: "skip-forward"
            text: watch.skipLabel
            onClicked: player.seek(watch.activeSkip.end)
        }

        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 40
            width: 320
            height: 96
            radius: Theme.radiusLg
            color: "#141416"
            border.width: 1
            border.color: "#2a2a30"
            visible: (!watch.phone || watch.immersed) && !watch.systemView && nextCountdown.running
            Column {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10
                Text {
                    text: qsTr("Next episode in ") + Math.ceil(nextCountdown.remaining) + "s"
                    color: "white"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textMd
                    font.weight: Font.DemiBold
                }
                Row {
                    spacing: 8
                    ScButton { compact: true; variant: "primary"; text: qsTr("Play now"); onClicked: watch.nextEpisode() }
                    ScButton { compact: true; text: qsTr("Cancel"); onClicked: nextCountdown.stop() }
                }
            }
        }
        Timer {
            id: nextCountdown
            // Settings → Anime: how long the countdown lasts.
            readonly property real length: Number(App.settings.nextCountdown) || 8
            property real remaining: length
            interval: 100
            repeat: true
            onRunningChanged: if (running) remaining = length
            onTriggered: {
                remaining -= 0.1
                if (remaining <= 0) {
                    stop()
                    if (watch.hasNext) watch.load(watch.index + 1)
                }
            }
        }
    }

    // ---------------------------------------------------------------- top bar
    // Desktop: floats over the video and fades with the rest of the chrome.
    // Phone portrait: a plain header above the player. Hidden in landscape.
    Rectangle {
        id: bar
        width: parent.width
        height: watch.phone ? 52 : watch.docked ? 64 : 72
        visible: watch.phone ? !watch.landscape : watch.docked ? watch.chromeShown : opacity > 0.01
        opacity: watch.phone || watch.docked || watch.chromeShown ? 1 : 0
        // Fading or just back: looks, takes no clicks (see chromeSettled).
        enabled: watch.phone || watch.docked || watch.chromeSettled
        Behavior on opacity { enabled: !watch.phone && !watch.docked; NumberAnimation { duration: Motion.slow } }
        color: watch.phone || watch.docked ? "#0b0b0c" : "transparent"
        gradient: watch.phone || watch.docked ? null : barShade
        Gradient {
            id: barShade
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.85) }
            GradientStop { position: 1.0; color: "transparent" }
        }
        HoverHandler { id: barHover }

        IconButton {
            id: backButton
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            iconName: "arrow-left"
            iconColor: "white"
            tip: qsTr("Close player (Esc)")
            onClicked: watch.exit()
        }
        Column {
            anchors.left: backButton.right
            anchors.leftMargin: 10
            anchors.right: watch.phone ? parent.right : barActions.left
            anchors.rightMargin: watch.phone ? 16 : 12
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width
                text: watch.media.title || ""
                color: "white"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: !watch.phone
                text: watch.episodeLine + (watch.provider.sourceLabel ? "  ·  " + watch.provider.sourceLabel : "")
                color: "#a1a1aa"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                elide: Text.ElideRight
            }
        }
        // Docked: skip and up-next live here, as nothing can float over the video.
        Row {
            id: barActions
            anchors.right: controls.left
            anchors.rightMargin: visible ? 12 : 0
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            visible: watch.systemView && !watch.phone && (skipNow.visible || nextNow.visible)
            ScButton {
                id: skipNow
                visible: watch.showSkip
                compact: true
                variant: "primary"
                iconName: "skip-forward"
                text: watch.skipLabel
                onClicked: player.seek(watch.activeSkip.end)
            }
            ScButton {
                id: nextNow
                visible: nextCountdown.running
                compact: true
                variant: "primary"
                text: qsTr("Next in ") + Math.ceil(nextCountdown.remaining) + "s"
                onClicked: watch.nextEpisode()
            }
            ScButton {
                visible: nextCountdown.running
                compact: true
                text: qsTr("Cancel")
                onClicked: nextCountdown.stop()
            }
        }
        Row {
            id: controls
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            visible: !watch.phone
            // The sources and servers as chips while they fit beside the
            // title (at least 200 px of it) and the buttons; else one chip
            // with the server in use that lists them all.
            readonly property bool chipsFit: inlineChips.implicitWidth <= bar.width - backButton.x - backButton.width - 10 - 200
                                             - (barActions.visible ? barActions.width + 12 : 0) - 12 - playerButtons.width - 14
            readonly property bool anyChips: watch.sourceChips.length > 0 || watch.streams.length > 0
            Row {
                id: inlineChips
                visible: controls.chipsFit && controls.anyChips
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Repeater {
                    model: watch.sourceChips
                    Chip {
                        required property var modelData
                        text: modelData.label
                        iconName: modelData.key === "more" ? "" : "server"
                        selected: modelData.key === (watch.provider.source || "")
                        onClicked: watch.pickSource(modelData.key)
                    }
                }
                Rectangle { visible: watch.sourceChips.length > 0 && watch.streams.length > 0; width: 1; height: 20; color: "#3f3f46"; anchors.verticalCenter: parent.verticalCenter }
                Repeater {
                    model: watch.streams
                    Chip {
                        required property var modelData
                        text: modelData.quality
                        selected: watch.isCurrent(modelData)
                        onClicked: watch.pickServer(modelData)
                    }
                }
            }
            Chip {
                id: serverPick
                visible: !controls.chipsFit && controls.anyChips
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, 320)
                iconName: "server"
                trailingIcon: "chevron-down"
                text: (watch.streams.find(s => watch.isCurrent(s)) || { quality: "" }).quality
                      || (watch.sourceChips.find(c => c.key === (watch.provider.source || "")) || { label: qsTr("Server") }).label
                onClicked: {
                    const list = watch.sourceChips.map(c => ({ text: c.label, icon: c.key === "more" ? "" : "server",
                                                               checked: c.key === (watch.provider.source || ""), run: () => watch.pickSource(c.key) }))
                    if (list.length > 0 && watch.streams.length > 0) list.push(null)
                    for (const s of watch.streams)
                        list.push({ text: s.quality, checked: watch.isCurrent(s), run: () => watch.pickServer(s) })
                    serverMenu.openAt(serverPick, 0, height + 6, list)
                }
            }
            Item { width: 8; height: 1 }
            Row {
                id: playerButtons
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                IconButton { iconName: "skip-back"; iconColor: "white"; enabled: watch.hasPrev; tip: qsTr("Previous episode") + watch.keyHint("player.prev"); onClicked: watch.load(watch.index - 1) }
                IconButton { iconName: "skip-forward"; iconColor: "white"; enabled: watch.hasNext; tip: qsTr("Next episode") + watch.keyHint("player.next"); onClicked: watch.load(watch.index + 1) }
                IconButton { iconName: "list"; iconColor: "white"; active: watch.sideOpen; tip: qsTr("Episodes"); onClicked: watch.sideOpen = !watch.sideOpen }
                IconButton {
                    visible: !watch.fromSource && (watch.hasSources || watch.nativeStream)
                    iconName: "server"
                    iconColor: "white"
                    tip: qsTr("Watch from")
                    onClicked: watchFrom.start(watch.media, false)
                }
                // On in a window of its own (a second screen).
                IconButton {
                    visible: Router.windows && !(Window.window && Window.window.isPageWindow === true)
                    iconName: "columns-2"
                    iconColor: "white"
                    tip: qsTr("Open in a new window")
                    onClicked: {
                        watch.saveProgress(true)
                        Router.openWindow("pages/WatchPage.qml", { media: watch.media, episodes: watch.episodes, index: watch.index })
                        Router.leave(watch)
                    }
                }
                IconButton {
                    visible: !!watch.media.anilistId
                    iconName: "users"
                    iconColor: Party.active ? Theme.accent : "white"
                    tip: Party.active ? qsTr("Watch party (%1)").arg(Party.code) : qsTr("Watch together")
                    onClicked: partyPanel.open()
                    Rectangle {
                        visible: Party.unread > 0
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 4
                        width: 8
                        height: 8
                        radius: 4
                        color: Theme.accent
                    }
                }
                IconButton { visible: !!watch.media.anilistId; iconName: "message-circle"; iconColor: "white"; tip: qsTr("Comments"); onClicked: comments.open() }
                ContinueOn { seriesId: watch.series.id || ""; iconColor: "white"; save: () => watch.saveProgress(true) }
                IconButton {
                    visible: watch.nativeStream && !watch.offline && !!watch.provider.source && Anime.nativePlayerAvailable
                    iconName: !watch.downloadJob ? "download" : watch.downloadJob.status === "done" ? "check" : "loader-circle"
                    iconColor: "white"
                    enabled: !watch.downloadJob && watch.currentUrl.length > 0
                    tip: watch.downloadJob ? qsTr("Downloading…") : qsTr("Download this episode")
                    onClicked: watch.downloadEpisode()
                }
                IconButton {
                    visible: watch.nativeStream && (watch.currentUrl.length > 0 || watch.casting)
                    iconName: "cast"
                    iconColor: watch.casting ? Theme.accent : "white"
                    tip: watch.casting ? qsTr("Stop casting") : qsTr("Cast to a TV")
                    onClicked: watch.casting ? watch.stopCasting() : castPicker.open()
                }
                IconButton {
                    id: outsideButton
                    visible: watch.canOpenOutside
                    iconName: "external-link"
                    iconColor: "white"
                    tip: qsTr("Open in another player")
                    onClicked: watch.pickOutside(outsideButton)
                }
                IconButton {
                    // A web player's page opens in the browser.
                    visible: !watch.nativeStream
                    iconName: "external-link"
                    iconColor: "white"
                    enabled: watch.currentUrl.length > 0
                    tip: qsTr("Open in your browser")
                    onClicked: App.openUrl(watch.currentUrl)
                }
                IconButton {
                    iconName: watch.fullScreen ? "minimize" : "maximize"
                    iconColor: "white"
                    tip: (watch.fullScreen ? qsTr("Exit full screen") : qsTr("Full screen")) + watch.keyHint("player.fullscreen")
                    onClicked: watch.toggleFullScreen()
                }
            }
        }
    }

    // ---------------------------------------------------------------- episodes
    component EpisodeRow: AbstractButton {
        id: row
        required property var modelData
        required property int index
        readonly property var state: watch.progress[modelData.key] || null
        height: 64
        hoverEnabled: true
        onClicked: watch.load(index)
        background: Rectangle {
            radius: Theme.radiusMd
            color: row.index === watch.index ? "#222226" : row.hovered ? "#18181b" : "transparent"
        }
        contentItem: Row {
            spacing: 10
            leftPadding: 6
            Item {
                width: 92
                height: 52
                anchors.verticalCenter: parent.verticalCenter
                Cover { anchors.fill: parent; radius: 6; source: row.modelData.image || ""; title: qsTr("EP ") + row.modelData.number }
                Rectangle {
                    visible: row.state !== null
                    anchors.bottom: parent.bottom
                    width: parent.width * (row.state ? (row.state.completed ? 1 : row.state.percent) : 0)
                    height: 3
                    color: "white"
                }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: row.width - 92 - 10 - 6 - 8
                Text {
                    width: parent.width
                    text: qsTr("Episode ") + row.modelData.number
                    color: row.index === watch.index ? "white" : "#d4d4d8"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textSm
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: row.modelData.title
                    color: "#8b8b93"
                    font.family: Theme.fontSans
                    font.pixelSize: Theme.textXs + 1
                    elide: Text.ElideRight
                }
            }
        }
    }

    // Desktop: slides over the right edge of the video; docked, it sits
    // beside it (and does not animate, which would resize the video on
    // every frame).
    Rectangle {
        id: side
        visible: !watch.phone && width > 0
        y: bar.visible ? bar.height : 0
        height: parent.height - y
        anchors.right: parent.right
        width: watch.sideOpen && watch.chromeShown ? 340 : 0
        color: watch.docked ? "#0b0b0c" : Qt.rgba(0.055, 0.055, 0.063, 0.94)
        radius: watch.docked ? 0 : Theme.radiusLg
        clip: true
        Behavior on width { enabled: !watch.docked; NumberAnimation { duration: Motion.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Motion.emphasized } }
        HoverHandler { id: sideHover }

        ListView {
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            WheelScroll {}
            anchors.fill: parent
            anchors.margins: 8
            width: 324
            clip: true
            model: watch.phone ? [] : watch.episodes
            currentIndex: watch.index
            ScrollBar.vertical: ScScrollBar {}
            Component.onCompleted: positionViewAtIndex(watch.index, ListView.Center)
            delegate: EpisodeRow { width: 324 }
        }
    }

    // Phone portrait: details, skip/next and the episode list under the player.
    Item {
        id: phonePanel
        visible: watch.phone && !watch.landscape
        anchors.top: stage.bottom
        anchors.bottom: parent.bottom
        width: parent.width

        Column {
            id: phoneHead
            width: parent.width
            topPadding: 14
            bottomPadding: 10
            spacing: 12

            Text {
                x: 16
                width: parent.width - 32
                text: watch.episodeLine
                color: "white"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Item {
                x: 16
                width: parent.width - 32
                height: 36
                Flickable {
                    maximumFlickVelocity: Motion.flickVelocity
                    flickDeceleration: Motion.flickDeceleration
                    anchors.left: parent.left
                    anchors.right: phoneNav.left
                    anchors.rightMargin: 8
                    height: parent.height
                    contentWidth: chips.width
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    Row {
                        id: chips
                        spacing: 6
                        anchors.verticalCenter: parent.verticalCenter
                        Repeater {
                            model: watch.sourceChips
                            Chip {
                                required property var modelData
                                text: modelData.label
                                iconName: modelData.key === "more" ? "" : "server"
                                selected: modelData.key === (watch.provider.source || "")
                                onClicked: watch.pickSource(modelData.key)
                            }
                        }
                        Rectangle { visible: watch.sourceChips.length > 0 && watch.streams.length > 0; width: 1; height: 20; color: "#3f3f46"; anchors.verticalCenter: parent.verticalCenter }
                        Repeater {
                            model: watch.streams
                            Chip {
                                required property var modelData
                                text: modelData.quality
                                selected: watch.isCurrent(modelData)
                                onClicked: watch.pickServer(modelData)
                            }
                        }
                    }
                }
                Row {
                    id: phoneNav
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    IconButton {
                        visible: !watch.fromSource && (watch.hasSources || watch.nativeStream)
                        iconName: "server"
                        iconColor: "white"
                        diameter: 40
                        onClicked: watchFrom.start(watch.media, false)
                    }
                    // Our own player's bar has these; a web player has none.
                    IconButton { visible: !watch.nativeStream; iconName: "skip-back"; iconColor: "white"; diameter: 40; enabled: watch.hasPrev; onClicked: watch.load(watch.index - 1) }
                    IconButton { visible: !watch.nativeStream; iconName: "skip-forward"; iconColor: "white"; diameter: 40; enabled: watch.hasNext; onClicked: watch.load(watch.index + 1) }
                    IconButton {
                        visible: !watch.nativeStream
                        iconName: "settings"
                        iconColor: "white"
                        diameter: 40
                        tip: qsTr("Settings")
                        onClicked: watch.openPanel()
                    }
                }
            }
            Row {
                x: 16
                spacing: 8
                visible: (watch.showSkip) || nextCountdown.running
                ScButton {
                    visible: watch.showSkip
                    variant: "primary"
                    compact: true
                    iconName: "skip-forward"
                    text: watch.skipLabel
                    onClicked: player.seek(watch.activeSkip.end)
                }
                ScButton {
                    visible: nextCountdown.running
                    variant: "primary"
                    compact: true
                    text: qsTr("Next in ") + Math.ceil(nextCountdown.remaining) + "s"
                    onClicked: watch.nextEpisode()
                }
                ScButton {
                    visible: nextCountdown.running
                    compact: true
                    text: qsTr("Cancel")
                    onClicked: nextCountdown.stop()
                }
            }
            Rectangle { width: parent.width; height: 1; color: "#1d1d21" }
        }

        ListView {
            maximumFlickVelocity: Motion.flickVelocity
            flickDeceleration: Motion.flickDeceleration
            WheelScroll {}
            id: phoneList
            anchors.top: phoneHead.bottom
            anchors.bottom: parent.bottom
            x: 8
            width: parent.width - 16
            clip: true
            model: watch.phone ? watch.episodes : []
            currentIndex: watch.index
            Component.onCompleted: positionViewAtIndex(watch.index, ListView.Contain)
            delegate: EpisodeRow { width: phoneList.width }
        }
    }

    // Phone landscape: the close button, the title and the settings over
    // the video's top, with the rest of the controls (the player's bar).
    // A system web view cannot be covered: the same as a strip above it,
    // while the controls show.
    component LandscapeTop: Item {
        id: top
        property bool overVideo: true
        height: 56
        Rectangle {
            anchors.fill: parent
            visible: top.overVideo
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.75) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
        Rectangle { anchors.fill: parent; visible: !top.overVideo; color: "#0b0b0c" }
        IconButton {
            id: closeButton
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            diameter: 44
            iconName: "arrow-left"
            iconColor: "white"
            tip: qsTr("Close player")
            onClicked: watch.exit()
        }
        Column {
            anchors.left: closeButton.right
            anchors.leftMargin: 6
            anchors.right: topActions.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width
                text: watch.media.title || ""
                color: "white"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textMd
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: watch.episodeLine
                color: "#a1a1aa"
                font.family: Theme.fontSans
                font.pixelSize: Theme.textSm
                elide: Text.ElideRight
            }
        }
        Row {
            id: topActions
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            // A web player has no bar of ours: skip and next sit here.
            ScButton {
                visible: !top.overVideo && watch.showSkip
                compact: true
                variant: "primary"
                iconName: "skip-forward"
                text: watch.skipLabel
                onClicked: player.seek(watch.activeSkip.end)
            }
            ScButton {
                visible: !top.overVideo && nextCountdown.running
                compact: true
                variant: "primary"
                text: qsTr("Next in ") + Math.ceil(nextCountdown.remaining) + "s"
                onClicked: watch.nextEpisode()
            }
            IconButton {
                visible: !top.overVideo
                diameter: 44
                iconName: "skip-forward"
                iconColor: "white"
                enabled: watch.hasNext
                tip: qsTr("Next episode")
                onClicked: watch.nextEpisode()
            }
            // Over our own player its bar has these.
            IconButton {
                visible: !top.overVideo
                diameter: 44
                iconName: "settings"
                iconColor: "white"
                tip: qsTr("Settings")
                onClicked: watch.openPanel()
            }
            IconButton {
                visible: !top.overVideo
                diameter: 44
                iconName: "minimize"
                iconColor: "white"
                tip: qsTr("Exit full screen")
                onClicked: watch.toggleFullScreen()
            }
        }
    }
    LandscapeTop {
        id: overlayTop
        parent: stage
        z: 4
        width: stage.width
        visible: watch.immersed && !watch.systemView && opacity > 0.01
        opacity: watch.chromeShown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Motion.normal } }
        enabled: watch.chromeSettled
    }
    // Over a web player taps belong to its own controls, so this strip only
    // shows on its own: as the player opens and while skip or the next
    // episode is on offer. Back leaves full screen for the rest.
    LandscapeTop {
        id: strip
        overVideo: false
        width: watch.width
        visible: watch.immersed && watch.systemView && (watch.chromeShown || watch.showSkip || nextCountdown.running)
        enabled: watch.chromeSettled || watch.showSkip || nextCountdown.running
    }

    PlayerPanel {
        id: panel
        parent: watch.phone ? watch : stage
        onOpenedChanged: if (!opened) watch.wake()
    }

    CommentsPanel {
        id: comments
        media: "anime"
        anilistId: watch.media.anilistId || 0
        title: watch.media.title || watch.series.title || ""
        unit: String(watch.episode.number || "")
        label: qsTr("Episode ") + (watch.episode.number || "")
    }
}
