import QtQuick
import QtWebView
import Scancore

// Player in the system's web view: Android WebView on phones, WebView2 on
// Windows and WKWebView on macOS. These play the H.264 episodes that the Qt
// WebEngine in Qt's own packages cannot. Like any native view it is drawn
// above everything Qt renders, so nothing can overlap it.
//
// Phones load the player inside a small page of ours (PlayerPage), as
// WebPlayer does, and read its time through getTime/seek messages; taps go
// to the player's own controls. The page reports back through
// its title, never through runJavaScript callbacks: on Android, Qt WebView
// runs those callbacks on the Android UI thread, alongside the QML engine on
// Qt's own thread, which crashes the app (a SIGSEGV in the JavaScript engine,
// often right after a tap). Title changes arrive as a signal, which Qt queues
// onto its own thread.
//
// Desktops open the player page itself and read its video element. WebView2
// cannot give a page of ours a real address, and inside a page without one
// the player cannot use its storage and never starts.
Item {
    id: root
    property string url
    readonly property bool direct: App.platform !== "android"
    signal time(real seconds, real total, string status)
    // Desktop: a key pressed while the page had focus, as the DOM tells it:
    // {key, ctrl, alt, shift, meta}.
    signal key(var press)
    // The player's own codec support, for parity with WebPlayer. System web
    // views play H.264, so this never fires.
    signal unsupported()

    function seek(seconds) {
        if (direct)
            web.runJavaScript("(function(){var v=document.querySelector('video');if(v)v.currentTime=" + Number(seconds) + "})()")
        else
            web.runJavaScript("window.scSeek && window.scSeek(" + Number(seconds) + ")") // no callback: see above
    }

    // A TV's remote (WatchPage): play or pause, and a jump from where it
    // is. Desktops reach the video element; phones and TVs go through the
    // player's message API in our page (no callbacks: see above).
    function togglePause() {
        if (direct)
            web.runJavaScript("(function(){var v=document.querySelector('video');if(v){if(v.paused)v.play();else v.pause()}})()")
        else
            web.runJavaScript("window.scToggle && window.scToggle()")
    }
    function seekBy(seconds) {
        if (direct)
            web.runJavaScript("(function(){var v=document.querySelector('video');if(v)v.currentTime=Math.max(0,v.currentTime+(" + Number(seconds) + "))})()")
        else
            web.runJavaScript("window.scSeekBy && window.scSeekBy(" + Number(seconds) + ")")
    }

    // Desktop: the video's state, plus the keys pressed since the last call.
    readonly property string probe: "(function(){var w=window;if(!w.__sc){w.__sc={k:[]};"
        + "w.addEventListener('keydown',function(e){if(e.repeat||/^(Control|Shift|Alt|Meta)$/.test(e.key))return;"
        + "w.__sc.k.push({key:e.key,ctrl:e.ctrlKey,alt:e.altKey,shift:e.shiftKey,meta:e.metaKey})},true)}"
        + "var v=document.querySelector('video'),k=w.__sc.k;w.__sc.k=[];"
        + "return JSON.stringify({t:v?v.currentTime:0,d:v&&isFinite(v.duration)?v.duration:0,s:v&&v.ended?'Ended':'',k:k})})()"

    function host(address) {
        return String(address).replace(/^[a-z]+:\/\//i, "").replace(/[\/?#].*$/, "").toLowerCase()
    }

    // Phones: video with sound, and the player's own full screen. The page
    // loads once the web view is prepared for them.
    property bool ready: direct
    Component.onCompleted: if (!direct) App.prepareWebPlayer()
    Connections {
        target: App
        enabled: !root.ready
        function onWebPlayerPrepared() { root.ready = true }
    }

    function load() {
        if (!url || !ready) return
        if (direct) web.url = url
        else web.loadHtml(PlayerPage.wrapper(url, true), "https://reanime.to/")
    }
    onReadyChanged: load()
    onUrlChanged: load()

    WebView {
        // Never a stop for a TV's remote (WatchPage works the player).
        activeFocusOnTab: false
        id: web
        anchors.fill: parent
        onTitleChanged: {
            if (root.direct) return
            const title = String(web.title)
            if (!title.startsWith("sc:")) return
            try {
                const s = JSON.parse(title.slice(3))
                if (s.d > 0) root.time(s.t, s.d, s.s || "")
            } catch (e) {}
        }
        // Desktop: the player page stays put; an ad that navigates the page
        // away is sent back. (Pop-up windows are refused by the web view.)
        onLoadingChanged: (request) => {
            if (root.direct && root.url && request.status === WebView.LoadStartedStatus
                && root.host(request.url) !== root.host(root.url))
                web.url = root.url
        }
    }

    Timer {
        interval: 500
        running: root.direct && root.visible && root.url.length > 0
        repeat: true
        onTriggered: web.runJavaScript(root.probe, (result) => {
            try {
                const s = JSON.parse(result)
                for (const k of s.k || []) root.key(k)
                if (s.d > 0) root.time(s.t, s.d, s.s || "")
            } catch (e) {}
        })
    }
}
