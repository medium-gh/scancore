import QtQuick
import QtWebEngine
import Scancore

// A plain browser view for passing a site's browser check. Cookies it
// receives are shared with SCANCORE's own requests (see WebBridge).
WebEngineView {
    id: view
    property string startUrl
    backgroundColor: "white"
    // The user agent the extension met the check with, so the clearance
    // works for it; SCANCORE's again once the window closes.
    onStartUrlChanged: {
        profile.httpUserAgent = App.siteAgent(startUrl)
        url = startUrl
    }
    Component.onDestruction: {
        profile.httpUserAgent = App.defaultAgent()
        App.setSiteCheckOpen(false)
    }
    settings.javascriptCanOpenWindows: false
    onNewWindowRequested: (request) => { /* stay in this view */ }
    // The check stays on its site: an ad that sends the page elsewhere on a
    // tap or by itself goes nowhere (ad networks are blocked meanwhile).
    onNavigationRequested: (request) => {
        if (request.isMainFrame && startUrl && !App.sameSite(startUrl, String(request.url))) request.reject()
    }
    // Before startUrl is set: shares cookies and the user agent.
    Component.onCompleted: {
        App.prepareWeb()
        App.setSiteCheckOpen(true)
    }
}
