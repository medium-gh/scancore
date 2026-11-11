import QtQuick
import QtQuick.Controls.Basic
import QtTest
import Scancore

Item {
    width: 1100; height: 800
    FontLoader { source: "../../resources/fonts/DMSans.ttf" }
    FontLoader { source: "../../resources/fonts/SpaceGrotesk.ttf" }
    Rectangle { anchors.fill: parent; color: Theme.bg }
    Component { id: pageComponent; ComicsPage { anchors.fill: parent } }
    // Capture navigation with either the real Router or the QML test mock.
    QtObject {
        id: navigationSpy
        property var opened: []
        function push(path, props) {
            if (props && props.summary)
                opened = opened.concat([props.summary])
        }
    }
    TestCase {
        name: "InlineCatalogue"
        when: windowShown
        property var page
        function cleanup() { Theme.compact = false; parent.width = 1100 }
        function init() {
            App.settings = {comicSource:"all",catalogComic:"comick",theme:"dark",reducedMotion:true}
            Comics.reset()
            navigationSpy.opened = []
            Router.shell = navigationSpy
            page = createTemporaryObject(pageComponent, parent)
            verify(page !== null)
            wait(30)
        }
        function descendants(item) {
            let result = []
            for (const child of item.children || []) result = result.concat([child], descendants(child))
            return result
        }
        function pick(key) {
            const chips = descendants(page).find(item => typeof item.picked === "function" && item.showLanguage === true)
            verify(chips !== undefined, "source chips are discoverable")
            chips.picked(key)
            wait(10)
        }
        function entries(start, count) {
            return Array.from({length:count}, (_, i) => ({id:String(start+i), title:"Catalogue title " + (start+i), sourceLabel:"Alpha"}))
        }
        function grid() { return descendants(page).find(item => item.items !== undefined && item.columns !== undefined && item.visible) }
        function view() { return descendants(page).find(item => item.contentY !== undefined && item.scrollTo !== undefined) }
        function test_queryAllLocalAndSearchOnly() {
            pick("alpha")
            page.search("  hero  ")
            compare(page.query, "hero")
            compare(Comics.calls[1].query, "hero")
            compare(Comics.calls[1].source, "alpha")
            pick("beta")
            compare(Comics.calls[2].query, "hero")
            compare(Comics.calls[2].source, "beta")
            page.search("")
            compare(Comics.calls[3].kind, "popular")
            pick("all")
            verify(!page.searching)
            compare(page.found, null)
            page.search("hero")
            compare(Comics.calls[Comics.calls.length - 1].source, "catalog")
            page.search("")
            pick("local")
            compare(Comics.calls[Comics.calls.length - 1].source, "local")
            compare(Comics.calls[Comics.calls.length - 1].query, "")
            const count = Comics.calls.length
            pick("search-only")
            verify(!page.browsingSource)
            compare(Comics.calls.length, count)
        }
        function test_restoreSavedSource() {
            page.destroy()
            App.setSetting("comicSource", "beta")
            page = createTemporaryObject(pageComponent, parent)
            compare(Comics.calls.length, 1)
            compare(Comics.calls[0].source, "beta")
        }
        function test_staleCompletionCancelled() {
            pick("alpha")
            const old = Comics.calls[0].task
            pick("beta")
            verify(old.cancelled)
            old.succeed({items:entries(90, 2),hasNextPage:false})
            compare(page.found.comics.length, 0)
            Comics.calls[1].task.succeed({items:entries(1, 2),hasNextPage:false})
            compare(page.found.comics[0].id, "1")
        }
        function test_failedPageBrowserRetryKeepsItems() {
            pick("alpha")
            Comics.calls[0].task.succeed({items:entries(1, 20),hasNextPage:true})
            page.moreSource()
            page.moreSource()
            compare(Comics.calls.length, 2)
            Comics.calls[1].task.fail("Browser check", "https://fixture.invalid/check")
            compare(page.browsePage, 1)
            compare(page.found.comics.length, 20)
            App.siteChecked("https://different.invalid/")
            compare(Comics.calls.length, 2)
            App.siteChecked("https://fixture.invalid/done")
            compare(Comics.calls.length, 3)
            compare(Comics.calls[2].page, 2)
            Comics.calls[2].task.succeed({items:entries(21, 10),hasNextPage:false})
            compare(page.found.comics.length, 30)
            page.moreSource()
            compare(Comics.calls.length, 3)
        }
        function test_showMorePreservesScroll_data() {
            return [{tag:"desktop",width:1100},{tag:"phone",width:600}]
        }
        function test_showMorePreservesScroll(data) {
            parent.width = data.width
            Theme.compact = data.width < 720
            pick("alpha")
            Comics.calls[0].task.succeed({items:entries(1, 40),hasNextPage:true})
            wait(80)
            const flick = view()
            flick.contentY = flick.contentHeight - flick.height
            const before = flick.contentY
            verify(before > 0)
            const button = descendants(page).find(item => item.text === "Show more" && item.visible)
            verify(button !== undefined)
            const screen = button.mapToItem(flick, 0, 0)
            verify(screen.y >= 0 && screen.y + button.height <= flick.height)
            mouseClick(button)
            compare(Comics.calls[1].page, 2)
            Comics.calls[1].task.succeed({items:entries(41, 20),hasNextPage:false})
            wait(50)
            compare(page.found.comics.length, 60)
            compare(flick.contentY, before)
            flick.contentY = 0
            wait(80)
            verify(grid().width <= page.width)
            grabImage(page).save("inline-catalogue-" + data.tag + ".png")
            parent.width = 1100
        }
        function test_keyboardGridFocus() {
            pick("alpha")
            Comics.calls[0].task.succeed({items:entries(1, 100),hasNextPage:true})
            wait(60)
            const covers = descendants(grid()).filter(item => item.itemIndex !== undefined)
            const first = covers.find(item => item.itemIndex === 0)
            first.forceActiveFocus(Qt.TabFocusReason)
            verify(first.activeFocus)
            keyClick(Qt.Key_Right)
            const second = covers.find(item => item.itemIndex === 1)
            verify(second.activeFocus, "Right moves focus to next cover")
            keyClick(Qt.Key_Down)
            verify(covers.find(item => item.itemIndex === 1 + grid().columns).activeFocus)
            let expected = 1 + grid().columns
            for (let i = 0; i < 12; ++i) {
                keyClick(Qt.Key_Down)
                expected += grid().columns
                verify(covers.find(item => item.itemIndex === expected).activeFocus)
            }
            verify(view().contentY > 0, "D-pad follows virtualized rows")
            for (let i = 0; i < 5; ++i) {
                keyClick(Qt.Key_Up)
                expected -= grid().columns
                verify(covers.find(item => item.itemIndex === expected).activeFocus)
            }
            keyClick(Qt.Key_Return)
            compare(navigationSpy.opened.length, 1)
            compare(navigationSpy.opened[0].id, String(expected + 1))
        }
        function test_deduplicatesWithinAndAcrossPages() {
            pick("alpha")
            Comics.calls[0].task.succeed({items:[{id:"a"},{id:"a"},{id:"b"}],hasNextPage:true})
            compare(page.found.comics.length, 2)
            page.moreSource()
            Comics.calls[1].task.succeed({items:[{id:"b"},{id:"c"},{id:"c"}],hasNextPage:true})
            compare(page.found.comics.length, 3)
        }
        function test_duplicateOnlyPageKeepsPagination() {
            pick("alpha")
            Comics.calls[0].task.succeed({items:[{id:"a"}],hasNextPage:true})
            page.moreSource()
            Comics.calls[1].task.succeed({items:[{id:"a"}],hasNextPage:true})
            verify(page.browseHasNext)
            page.moreSource()
            compare(Comics.calls[2].page, 3)
            Comics.calls[2].task.succeed({items:[{id:"b"}],hasNextPage:false})
            compare(page.found.comics.length, 2)
        }
        // AniList, MyAnimeList or no catalogue chosen: All sources still
        // offers Comick's filters, and a filtered search goes through Comick.
        function test_comickFiltersWithOtherCatalogues_data() { return [{tag:"anilist", catalog:"anilist"}, {tag:"none", catalog:"none"}] }
        function test_comickFiltersWithOtherCatalogues(data) {
            page.destroy()
            App.settings = {comicSource:"all",catalogComic:data.catalog,theme:"dark",reducedMotion:true}
            page = createTemporaryObject(pageComponent, parent)
            const button = descendants(page).find(item => item.iconName === "funnel" && item.diameter !== undefined)
            verify(button !== undefined && button.visible, "the filter button shows")
            // Without filters, words go where they did.
            page.search("hero")
            const plain = Comics.calls[Comics.calls.length - 1]
            compare(plain.source, data.catalog === "none" ? "all" : "catalog")
            compare(plain.catalogKey || "", "")
            App.setSetting("comickFilters", {genres:{action:1}})
            page.search("")
            const last = Comics.calls[Comics.calls.length - 1]
            compare(last.source, "catalog")
            compare(last.catalogKey, "comick")
            compare(page.catalogLabel, "Comick")
            verify(page.searching)
            App.setSetting("comickFilters", {})
        }
        function test_selectSourceLoadsPopularInline() {
            pick("alpha")
            compare(App.settings.comicSource, "alpha")
            compare(Comics.calls.length, 1)
            compare(Comics.calls[0].source, "alpha")
            compare(Comics.calls[0].kind, "popular")
            compare(Comics.calls[0].page, 1)
            Comics.calls[0].task.succeed({items:[{id:"one",title:"First title"}],hasNextPage:true})
            compare(page.found.comics.length, 1)
            verify(page.searching)
            compare(navigationSpy.opened.length, 0)
        }
    }
}
