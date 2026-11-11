pragma Singleton
import QtQuick
QtObject {
    id: service
    property var calls: []
    property var tasks: []
    property Component factory: Component {
        QtObject {
            property bool loading: true
            property bool ok: false
            property string error: ""
            property string verifyUrl: ""
            property var result: null
            property var partial: null
            property bool cancelled: false
            signal finished(var result)
            signal failed()
            function cancel() { cancelled = true; loading = false }
            function succeed(value) { result = value; loading = false; ok = true; finished(value) }
            function fail(message, url) { error = message; verifyUrl = url || ""; loading = false; failed() }
        }
    }
    function task() { return factory.createObject(service) }
    function reset() { calls = [] }
    function browse(source, kind, page) { const t = task(); calls = calls.concat([{source, kind, page, task:t}]); return t }
    function search(query, source) { const t = task(); calls = calls.concat([{query, source, task:t}]); return t }
    function catalog(query, filters, catalogKey) { const t = task(); calls = calls.concat([{query, source:"catalog", catalogKey:catalogKey || "", task:t}]); return t }
    function latest(limit) { const t = task(); t.succeed({updates:[]}); return t }
    function catalogFeeds() { const t = task(); t.succeed({shelves:[]}); return t }
    function sources() { return [{key:"alpha",label:"Alpha",enabled:true,canBrowse:true},{key:"beta",label:"Beta",enabled:true,canBrowse:true},{key:"search-only",label:"Search only",enabled:true,canBrowse:false},{key:"local",label:"On this device",enabled:true}] }
    function sourceForUrl(url) { return "" }
    function imageReferer(url) { return "" }
    function hasExtensions() { return false }
    function otherLanguages() { return [] }
}
