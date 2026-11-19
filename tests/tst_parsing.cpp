// Offline tests for the HTML layer and parsers. Live site checks are a
// separate, opt-in run because sites change and CI must stay deterministic.

#include "app/Resolve.h"
#include "core/Database.h"
#include "core/Html.h"
#include "core/Parallel.h"
#include "core/Proto.h"
#include "core/Text.h"
#include "sources/Manifest.h"
#include "sources/comics/ComicSource.h"
#include "sources/bundled/comics/Engines.h"
#include "sources/comics/Registry.h"
#include "sources/local/Local.h"
#include "sources/bundled/novels/Engines.h"
#include "sources/novels/NovelSource.h"
#include "sources/novels/Registry.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QVersionNumber>
#include <QtTest>

using namespace sc;

// Markup and data shaped like each site's, in tests/fixtures.
static QByteArray fixture(const char *name) {
    QFile file(QStringLiteral(SCANCORE_SOURCE_DIR "/tests/fixtures/") + QLatin1String(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// parallelByKeyEach's outcomes by job, and the order they came in.
template <typename T>
static QList<Outcome<T>> byKey(const QList<std::function<T()>> &jobs, const QStringList &keys, int perKey,
                               QList<int> *order = nullptr) {
    QList<Outcome<T>> out(jobs.size());
    parallelByKeyEach<T>(jobs, keys, perKey, [&](int i, const Outcome<T> &r) {
        out[i] = r;
        if (order)
            order->append(i);
    });
    return out;
}

class Parsing : public QObject {
    Q_OBJECT

private slots:
    void selectorsReturnDocumentOrder() {
        HtmlDocument doc(QStringLiteral("<ul><li class=a>1</li><li class=b>2</li><li class=a>3</li></ul>"));
        const auto nodes = doc.select(QStringLiteral("li.a, li.b"));
        QCOMPARE(nodes.size(), 3);
        QCOMPARE(nodes[0].text(), QStringLiteral("1"));
        QCOMPARE(nodes[2].text(), QStringLiteral("3"));
    }

    void attributesAndClosest() {
        HtmlDocument doc(QStringLiteral("<div class=\"row\"><h3 class=\"title\"><a href=\"/n/x\" title=\"X\">X</a></h3><img data-src=\"/c.jpg\"></div>"));
        const auto a = doc.first(QStringLiteral("h3[class*='title'] > a"));
        QCOMPARE(a.attr("title"), QStringLiteral("X"));
        const auto row = a.closest(QStringLiteral(".row"));
        QVERIFY(!row.isNull());
        QCOMPARE(novels::imgSrc(row.first(QStringLiteral("img")), QStringLiteral("https://example.com/")),
                 QStringLiteral("https://example.com/c.jpg"));
    }

    void resolveRejectsScripts() {
        QVERIFY(resolveUrl(QStringLiteral("https://a.test/"), QStringLiteral("javascript:alert(1)")).isEmpty());
        QVERIFY(resolveUrl(QStringLiteral("https://a.test/"), QStringLiteral("data:image/png;base64,AA")).isEmpty());
        QCOMPARE(resolveUrl(QStringLiteral("https://a.test/x/"), QStringLiteral("../y")), QStringLiteral("https://a.test/y"));
    }

    void entities() {
        QCOMPARE(decodeEntities(QStringLiteral("Tom &amp; Jerry &#8211; Ep")), QString::fromUtf8("Tom & Jerry – Ep"));
    }

    void textBlocksKeepFormattingOnly() {
        HtmlDocument doc(QStringLiteral(
            "<div id=c><p>Hello <i>world</i> <script>x()</script></p><p style=\"display:none\">hidden</p>"
            "<h2>Title</h2>line one<br>line two<p><a href=\"https://evil.test\" onclick=\"x\">link</a> &lt;b&gt;</p></div>"));
        const auto blocks = novels::textBlocks(doc.first(QStringLiteral("#c")));
        QCOMPARE(blocks.size(), 5);
        QCOMPARE(blocks[0].text, QStringLiteral("Hello world"));
        QCOMPARE(blocks[0].html, QStringLiteral("Hello <i>world</i>"));
        QCOMPARE(blocks[1].kind, QStringLiteral("heading"));
        QCOMPARE(blocks[2].text, QStringLiteral("line one"));
        QCOMPARE(blocks[3].text, QStringLiteral("line two"));
        // Markup from the page arrives escaped, never as tags.
        QCOMPARE(blocks[4].html, QStringLiteral("link &lt;b&gt;"));
    }

    void chapterNumbers() {
        QCOMPARE(comics::chapterNumberFrom(QStringLiteral("Chapter 12.5 - The end")), QStringLiteral("12.5"));
        QCOMPARE(comics::chapterNumberFrom(QStringLiteral("Ep. 7")), QStringLiteral("7"));
        QCOMPARE(comics::chapterNumberFrom(QStringLiteral("Prologue")), QStringLiteral("0"));
        QCOMPARE(comics::chapterNumberFrom(QStringLiteral("Season 2 Chapter 14")), QStringLiteral("14"));
        QCOMPARE(comics::chapterNumberFrom(QStringLiteral("Ch.5")), QStringLiteral("5"));
        QCOMPARE(comics::chapterNumberFrom(QStringLiteral("Vol. 3 Chap 21")), QStringLiteral("21"));
        QCOMPARE(comics::chapterNumberFrom(QStringLiteral("Episode 40")), QStringLiteral("40"));
        // Spanish labels.
        QCOMPARE(comics::chapterNumberFrom(QString::fromUtf8("Capítulo 12")), QStringLiteral("12"));
        QCOMPARE(comics::chapterNumberFrom(QString::fromUtf8("CAPÍTULO 7.5")), QStringLiteral("7.5"));
        QCOMPARE(comics::chapterNumberFrom(QStringLiteral("Capitulo 3")), QStringLiteral("3"));
        QCOMPARE(comics::chapterNumberFrom(QStringLiteral("Cap. 12")), QStringLiteral("12"));
        QCOMPARE(comics::chapterNumberFrom(QStringLiteral("Cap 9: El regreso")), QStringLiteral("9"));
        QCOMPARE(comics::chapterNumberFrom(QString::fromUtf8("Temporada 2 Capítulo 12")), QStringLiteral("12"));
        QCOMPARE(comics::chapterNumberFrom(QStringLiteral("Episodio 4")), QStringLiteral("4"));
        // Decomposed accents (NFD) read the same.
        QCOMPARE(comics::chapterNumberFrom(QString::fromUtf8("Capi\xcc\x81tulo 30")), QStringLiteral("30"));
        // A label has to start a word: "Batch" and "Match" are not "ch".
        QCOMPARE(comics::chapterNumberFrom(QStringLiteral("Match 2 Chapter 10")), QStringLiteral("10"));
        QCOMPARE(comics::chapterNumberFrom(QStringLiteral("Captain 3")), QStringLiteral("3"));
        QList<comics::Chapter> list{{QStringLiteral("10")}, {QStringLiteral("2")}, {QStringLiteral("2.5")}};
        comics::sortChapters(list);
        QCOMPARE(list[0].number, QStringLiteral("2"));
        QCOMPARE(list[2].number, QStringLiteral("10"));
    }

    void titleNormalizer() {
        // Plain English titles: as before, words split on anything else.
        QCOMPARE(normalizedTitle(QStringLiteral("Solo Leveling: Ragnarok")), QStringLiteral("solo leveling ragnarok"));
        QCOMPARE(normalizedTitle(QStringLiteral("  The  Beginning-After_the END! ")), QStringLiteral("the beginning after the end"));
        QCOMPARE(normalizedTitle(QStringLiteral("Re:Zero (2nd Season)")), QStringLiteral("re zero 2nd season"));
        QCOMPARE(titleKey(QStringLiteral("Re:Zero (2nd Season)")), QStringLiteral("rezero2ndseason"));
        // Accents and the tilde fold away; Spanish punctuation separates.
        QCOMPARE(normalizedTitle(QString::fromUtf8("Año 2")), QStringLiteral("ano 2"));
        QCOMPARE(normalizedTitle(QString::fromUtf8("¿Quién es él?")), QStringLiteral("quien es el"));
        QCOMPARE(normalizedTitle(QString::fromUtf8("¡La Pequeña Señorita!")), QStringLiteral("la pequena senorita"));
        QCOMPARE(titleKey(QString::fromUtf8("Pequeña")), titleKey(QStringLiteral("pequena")));
        QCOMPARE(titleKey(QString::fromUtf8("PINGÜINO")), QStringLiteral("pinguino"));
        // Decomposed input folds the same as composed input.
        QCOMPARE(normalizedTitle(QString::fromUtf8("Pequen\xcc\x83" "a")), QStringLiteral("pequena"));
        // Letters with no accent to drop are kept, not split.
        QCOMPARE(normalizedTitle(QString::fromUtf8("Ørsted Æon")), QString::fromUtf8("ørsted æon"));
        // Other scripts stay whole and distinct.
        QCOMPARE(normalizedTitle(QString::fromUtf8("俺だけレベルアップな件")), QString::fromUtf8("俺だけレベルアップな件"));
        QCOMPARE(normalizedTitle(QString::fromUtf8("나 혼자만 레벨업")), QString::fromUtf8("나 혼자만 레벨업"));
        QCOMPARE(titleKey(QString::fromUtf8("全职法师 2")), QString::fromUtf8("全职法师2"));
        QVERIFY(!normalizedTitle(QString::fromUtf8("進撃の巨人")).isEmpty());
        // Japanese voicing marks change the word, so they are not dropped.
        QVERIFY(titleKey(QString::fromUtf8("がっこう")) != titleKey(QString::fromUtf8("かっこう")));
        QCOMPARE(normalizedTitle(QString::fromUtf8("Только я")), QString::fromUtf8("только я"));
        QVERIFY(normalizedTitle(QStringLiteral("?!... --")).isEmpty());
    }

    void languageNames() {
        QCOMPARE(languageName(QStringLiteral("en")), QStringLiteral("English"));
        QCOMPARE(languageName(QStringLiteral("es")), QString::fromUtf8("Español"));
        QCOMPARE(languageName(QStringLiteral("es-la")), QString::fromUtf8("Español"));
        QCOMPARE(languageName(QStringLiteral("zz")), QStringLiteral("zz"));
    }

    void chapterLabels() {
        QCOMPARE(comics::chapterLabel(QStringLiteral("en"), QStringLiteral("3")), QStringLiteral("Chapter 3"));
        QCOMPARE(comics::chapterLabel(QStringLiteral("en"), QStringLiteral("3"), QStringLiteral("Start")), QStringLiteral("Chapter 3: Start"));
        QCOMPARE(comics::chapterLabel(QStringLiteral("es"), QStringLiteral("3")), QString::fromUtf8("Capítulo 3"));
        QCOMPARE(comics::chapterLabel(QStringLiteral("es-la"), QStringLiteral("4.5"), QStringLiteral("Fin")), QString::fromUtf8("Capítulo 4.5: Fin"));
        // Unknown languages fall back to English.
        QCOMPARE(comics::chapterLabel(QStringLiteral("xx"), QStringLiteral("1")), QStringLiteral("Chapter 1"));
        QCOMPARE(comics::chapterLabel(QStringLiteral("pt-br"), QStringLiteral("2")), QString::fromUtf8("Capítulo 2"));
        QCOMPARE(comics::chapterLabel(QStringLiteral("ko"), QStringLiteral("12")), QString::fromUtf8("12화"));
        QCOMPARE(comics::chapterLabel(QStringLiteral("fr"), QStringLiteral("1"), QStringLiteral("Début")), QString::fromUtf8("Chapitre 1: Début"));
    }

    void minVersion() {
        QVERIFY(!Manifest::appVersion().isEmpty());
        QVERIFY(Manifest::supports(QJsonObject{}));
        QVERIFY(Manifest::supports(QJsonObject{{"minVersion", "1.0.0"}}));
        QVERIFY(Manifest::supports(QJsonObject{{"minVersion", Manifest::appVersion()}}));
        QVERIFY(!Manifest::supports(QJsonObject{{"minVersion", "999.0"}}));
        // A version this build cannot read counts as newer.
        QVERIFY(!Manifest::supports(QJsonObject{{"minVersion", "soon"}}));
    }

    void registriesReadNewFields() {
        const auto manifest = QJsonDocument(QJsonObject{
            {"version", 1000000},
            {"comics", QJsonArray{
                QJsonObject{{"key", "dex"}, {"engine", "mangadex"}, {"base", "https://mangadex.org"}, {"hosts", QJsonArray{"mangadex.org"}}},
                QJsonObject{{"key", "dex-es"}, {"label", QString::fromUtf8("MangaDex (Español)")}, {"engine", "mangadex"},
                            {"base", "https://mangadex.org"}, {"hosts", QJsonArray{"mangadex.org"}}, {"language", "es"},
                            {"urlTag", "es"}, {"minVersion", "1.0.0"}, {"options", QJsonObject{{"languages", QJsonArray{"es", "es-la"}}}}},
                QJsonObject{{"key", "future"}, {"engine", "madara"}, {"base", "https://future.test"},
                            {"hosts", QJsonArray{"future.test"}}, {"minVersion", "999.0"}},
                QJsonObject{{"key", "vortex"}, {"engine", "iken"}, {"base", "https://vortexscans.org"}, {"hosts", QJsonArray{"vortexscans.org"}}},
                QJsonObject{{"key", "lector"}, {"engine", "madara"}, {"base", "https://lector.test"},
                            {"hosts", QJsonArray{"lector.test"}}, {"language", "es"}, {"nsfw", false}}}},
            {"novels", QJsonArray{
                QJsonObject{{"key", "novelas"}, {"engine", "novelfull"}, {"base", "https://novelas.test"},
                            {"hosts", QJsonArray{"novelas.test"}}, {"language", "es"}},
                QJsonObject{{"key", "later"}, {"engine", "novelfull"}, {"base", "https://later.test"},
                            {"hosts", QJsonArray{"later.test"}}, {"minVersion", "999.0"}}}}});
        QVERIFY(Manifest::loadData(manifest.toJson()));
        comics::Registry::rebuild();
        novels::Registry::rebuild();

        // Fields and defaults.
        const auto dex = comics::Registry::byKey(QStringLiteral("dex"));
        const auto dexEs = comics::Registry::byKey(QStringLiteral("dex-es"));
        QVERIFY(dex && dexEs);
        QCOMPARE(dex->language(), QStringLiteral("en"));
        QVERIFY(dex->config().urlTag.isEmpty());
        QCOMPARE(dexEs->language(), QStringLiteral("es"));
        QCOMPARE(dexEs->config().urlTag, QStringLiteral("es"));
        QCOMPARE(dexEs->config().options.value("languages").toArray().size(), 2);
        QCOMPARE(comics::Registry::byKey(QStringLiteral("lector"))->language(), QStringLiteral("es"));
        QCOMPARE(novels::Registry::byKey(QStringLiteral("novelas"))->config().language, QStringLiteral("es"));

        // Entries for a newer app are skipped.
        QVERIFY(!comics::Registry::byKey(QStringLiteral("future")));
        QVERIFY(!novels::Registry::byKey(QStringLiteral("later")));

        // Links: the tag picks the tagged source; untagged links (and every
        // id already in a library) stay with the untagged one.
        const auto keyFor = [](const QString &url) {
            const auto source = comics::Registry::forUrl(url);
            return source ? source->key() : QString();
        };
        QCOMPARE(keyFor(QStringLiteral("https://mangadex.org/title/0b1c-x")), QStringLiteral("dex"));
        QCOMPARE(keyFor(QStringLiteral("https://mangadex.org/chapter/9f-y")), QStringLiteral("dex"));
        QCOMPARE(keyFor(QStringLiteral("https://mangadex.org/title/0b1c-x#es")), QStringLiteral("dex-es"));
        QCOMPARE(keyFor(QStringLiteral("https://mangadex.org/chapter/9f-y#es")), QStringLiteral("dex-es"));
        // Iken keeps its numeric post id in the fragment.
        QCOMPARE(keyFor(QStringLiteral("https://vortexscans.org/series/some-title#1234")), QStringLiteral("vortex"));
        // A tag on a host its source does not own is nobody's.
        QCOMPARE(keyFor(QStringLiteral("https://vortexscans.org/series/some-title#es")), QString());
        QCOMPARE(keyFor(QStringLiteral("https://unknown.test/x")), QString());
        QCOMPARE(novels::Registry::forUrl(QStringLiteral("https://novelas.test/libro"))->key(), QStringLiteral("novelas"));
    }

    void challengeDetection() {
        Response page;
        page.status = 200;
        page.contentType = "text/html; charset=UTF-8";
        // Cloudflare's beacon on an ordinary page is not a check.
        page.body = "<html><head><title>Solo Leveling - M440</title>"
                    "<script src=\"/cdn-cgi/challenge-platform/scripts/jsd/main.js\"></script></head></html>";
        QVERIFY(!page.isChallenge());
        page.status = 403;
        QVERIFY(page.isChallenge());
        page.status = 200;
        page.body = "<!DOCTYPE html><html><head><title>Just a moment...</title></head></html>";
        QVERIFY(page.isChallenge());
    }

    void leerCapitulo() {
        const QString base = QStringLiteral("https://www.leercapitulo.co");
        const auto results = comics::LeerCapituloSource::parseSearch(fixture("leercapitulo-search.html"), base);
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].title, QStringLiteral("Level Up Alone"));
        QCOMPARE(results[0].url, QStringLiteral("https://www.leercapitulo.co/manga/0wdem0o1jg/level-up-alone/"));
        QCOMPARE(results[0].cover, QStringLiteral("https://www.leercapitulo.co/covers/95/be.jpg?v1"));
        QCOMPARE(results[0].type, QStringLiteral("Manhwa"));

        const QString series = base + QStringLiteral("/manga/0rradr8n/fin/");
        const auto chapters = comics::LeerCapituloSource::parseChapters(fixture("leercapitulo-chapters.html"), series);
        QCOMPARE(chapters.size(), 3);
        QCOMPARE(chapters[0].number, QStringLiteral("1"));
        QCOMPARE(chapters[1].number, QStringLiteral("53.5"));
        QCOMPARE(chapters[2].url, QStringLiteral("https://www.leercapitulo.co/leer/0rradr8n/fin/54/"));
        QCOMPARE(chapters[2].released, QStringLiteral("2026-10-19"));

        const auto panels = comics::LeerCapituloSource::parsePanels(fixture("leercapitulo-pages.html"), base + QStringLiteral("/leer/x/y/1/"));
        QCOMPARE(panels.size(), 2);
        QCOMPARE(panels[1].url, QStringLiteral("https://img.test/a/2.webp"));
    }

    void mangaverse() {
        const QString base = QStringLiteral("https://www.mangaoni.com/es");
        const auto cards = comics::MangaverseSource::parseCards(fixture("mangaverse-search.html"), base, QStringLiteral("ES"));
        QCOMPARE(cards.size(), 1);
        QCOMPARE(cards[0].title, QStringLiteral("Solo Leveling"));
        QCOMPARE(cards[0].cover, QStringLiteral("https://www.mangaoni.com/wp-content/uploads/c.jpg"));
        QCOMPARE(comics::MangaverseSource::cleanTitle(QStringLiteral("Wind Breaker ES"), QStringLiteral("ES")), QStringLiteral("Wind Breaker"));
        QCOMPARE(comics::MangaverseSource::cleanTitle(QStringLiteral("Hades"), QStringLiteral("ES")), QStringLiteral("Hades"));

        const auto chapters = comics::MangaverseSource::parseChapters(fixture("mangaverse-chapters.html"), base + QStringLiteral("/manga/sl.html"));
        QCOMPARE(chapters.size(), 2);
        QCOMPARE(chapters[0].title, QStringLiteral("Capitulo 201"));
        QCOMPARE(chapters[0].number, QStringLiteral("201"));
        QCOMPARE(chapters[1].number, QStringLiteral("200.5"));
    }

    void inManga() {
        const auto results = comics::InMangaSource::parseSearch(fixture("inmanga-search.html"), QStringLiteral("https://inmanga.com"));
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].title, QStringLiteral("One Piece"));
        QCOMPARE(results[0].url, QStringLiteral("https://inmanga.com/ver/manga/One-Piece/dfc7ecb5-e9b3-4aa5-a61b-a498993cd935"));
        QCOMPARE(results[0].cover, QStringLiteral("https://cdn1.intomanga.com/i/m/dfc7ecb5/t/o/dfc7ecb5.jpg"));

        // The page picker appears twice; ids are lower-cased into the path.
        const QByteArray controls = fixture("inmanga-pages.html");
        const auto pages = comics::InMangaSource::parsePages(controls, QStringLiteral("https://cdn1.intomanga.com"),
                                                             QStringLiteral("DDAB755F-B5A2"));
        QCOMPARE(pages.size(), 2);
        QCOMPARE(pages[0].url, QStringLiteral("https://cdn1.intomanga.com/i/m/dfc7ecb5-aaaa/c/ddab755f-b5a2/o/32dfc965-0001.jpg"));
        QVERIFY(comics::InMangaSource::parsePages("<select id=\"PageList\"></select>", QStringLiteral("https://x"), QStringLiteral("y")).isEmpty());
    }

    void manhwaWeb() {
        // Links can name an older id of the series; chapters use the current one.
        const auto series = QJsonDocument::fromJson(fixture("manhwaweb-series.json")).object();
        const auto chapters = comics::ManhwaWebSource::parseChapters(series, QStringLiteral("comic-solo_leveling_169"),
                                                                     QStringLiteral("https://manhwaweb.com"), QStringLiteral("es"));
        QCOMPARE(chapters.size(), 2);
        QCOMPARE(chapters[0].number, QStringLiteral("0.01"));
        QCOMPARE(chapters[1].url, QStringLiteral("https://manhwaweb.com/leer/comic-solo_leveling_169-1_01"));
        QCOMPARE(chapters[1].title, QString::fromUtf8("Capítulo 1"));
        QCOMPARE(chapters[1].released, QStringLiteral("2023-08-07T19:20:00Z"));
    }

    void naver() {
        // Paid previews are left out; the episode number is the chapter.
        const auto page = QJsonDocument::fromJson(fixture("naver-articles.json")).object();
        const auto chapters = comics::NaverSource::parseChapters(page, QStringLiteral("https://comic.naver.com"));
        QCOMPARE(chapters.size(), 2);
        QCOMPARE(chapters[0].number, QStringLiteral("3"));
        QCOMPARE(chapters[0].released, QStringLiteral("2026-06-09"));
        QCOMPARE(chapters[1].title, QString::fromUtf8("000. 프롤로그"));
        QCOMPARE(chapters[1].url, QStringLiteral("https://comic.naver.com/webtoon/detail?titleId=747269&no=1"));
        QCOMPARE(chapters[1].released, QStringLiteral("2022-04-22"));
        // The age notice is not a page.
        const auto panels = comics::NaverSource::parsePanels(fixture("naver-viewer.html"));
        QCOMPARE(panels.size(), 2);
        QCOMPARE(panels[1].url, QStringLiteral("https://image-comic.pstatic.net/webtoon/747269/1/IMAG01_2.jpg"));
    }

    void kakaoPage() {
        // Free episodes only, without the video teaser; an unnumbered
        // prologue keeps its place.
        const auto page = QJsonDocument::fromJson(fixture("kakaopage-products.json")).object().value("result").toObject();
        auto chapters = comics::KakaoPageSource::parseEpisodes(page.value("list").toArray(), QStringLiteral("https://page.kakao.com"),
                                                               QStringLiteral("50866481"), QString::fromUtf8("나 혼자만 레벨업"));
        QCOMPARE(chapters.size(), 2);
        QCOMPARE(chapters[0].number, QStringLiteral("0.003"));
        QCOMPARE(chapters[0].title, QString::fromUtf8("프롤로그"));
        QCOMPARE(chapters[1].number, QStringLiteral("1"));
        QCOMPARE(chapters[1].title, QString::fromUtf8("1화"));
        QCOMPARE(chapters[1].url, QStringLiteral("https://page.kakao.com/content/50866481/viewer/50896826"));
        QCOMPARE(chapters[1].released, QStringLiteral("2018-03-04T21:59:57+09:00"));
        // AniList's links to the series page open it in "Read from".
        comics::SourceConfig config;
        config.key = QStringLiteral("kakaopage");
        config.base = QStringLiteral("https://page.kakao.com");
        config.hosts = {QStringLiteral("page.kakao.com")};
        const comics::KakaoPageSource source(config);
        QCOMPARE(source.seriesLink(QUrl(QStringLiteral("https://page.kakao.com/content/50866481"))),
                 QStringLiteral("https://page.kakao.com/content/50866481"));
        QCOMPARE(source.seriesLink(QUrl(QStringLiteral("https://page.kakao.com/content/50866481/viewer/50896826"))),
                 QStringLiteral("https://page.kakao.com/content/50866481"));
        QVERIFY(source.seriesLink(QUrl(QStringLiteral("https://page.kakao.com/menu/10010"))).isEmpty());
        QVERIFY(source.seriesLink(QUrl(QStringLiteral("https://webtoon.kakao.com/content/x/2320"))).isEmpty());
    }

    void zonaTmo() {
        const auto base = QStringLiteral("https://zonatmo.to");
        // Novels and (by default) doujinshi stay out; other names come along.
        const auto search = QJsonDocument::fromJson(fixture("zonatmo-search.json")).object();
        const auto results = comics::ZonaTmoSource::parseSearch(search, base, QStringLiteral("zonatmo"), false);
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].url, QStringLiteral("https://zonatmo.to/manga/solo-leveling"));
        QCOMPARE(results[0].cover, QStringLiteral("https://zonatmo.to/wp-content/uploads/2025/11/5c2efcd42cd5e.webp"));
        QVERIFY(results[0].names.contains(QStringLiteral("I Level Up Alone")));
        QVERIFY(!results[0].names.contains(QStringLiteral("Solo Leveling")));
        QCOMPARE(comics::ZonaTmoSource::parseSearch(search, base, QStringLiteral("zonatmo"), true).size(), 2);
        // One number from two groups says whose each is; a title that only
        // repeats the number is dropped.
        const auto rows = QJsonDocument::fromJson(fixture("zonatmo-chapters.json")).array();
        const auto chapters = comics::ZonaTmoSource::parseChapters(rows, base, QStringLiteral("solo-leveling"), QStringLiteral("es"));
        QCOMPARE(chapters.size(), 3);
        QCOMPARE(chapters[0].title, QString::fromUtf8("Capítulo 1: Lectorxd_com"));
        QCOMPARE(chapters[1].title, QString::fromUtf8("Capítulo 1: Otro Scan"));
        QCOMPARE(chapters[1].url, QStringLiteral("https://zonatmo.to/manga/solo-leveling/solo-leveling-capitulo-1-2"));
        QCOMPARE(chapters[1].slug, QStringLiteral("solo-leveling-capitulo-1-2"));
        QCOMPARE(chapters[2].number, QStringLiteral("200.5"));
        QCOMPARE(chapters[2].title, QString::fromUtf8("Capítulo 200.5: Epílogo"));
        QCOMPARE(chapters[2].released, QStringLiteral("2026-06-01T00:00:00Z"));
    }

    // Volumes, chapters and extras from their titles, in the ways Chinese,
    // Japanese, Korean and English sources write them.
    void chapterParts() {
        for (const auto &value : QJsonDocument::fromJson(fixture("chapter-parts.json")).array()) {
            const auto pair = value.toArray();
            QCOMPARE(chapterPart(pair.at(0).toString()), pair.at(1).toString());
        }
    }

    static comics::SourceConfig kaganeConfig(const QString &base) {
        comics::SourceConfig config;
        config.key = QStringLiteral("kagane-es");
        config.label = QStringLiteral("Kagane");
        config.engine = QStringLiteral("kagane");
        config.base = base;
        config.api = base + QStringLiteral("/api/v2");
        config.hosts = {QUrl(base).host()};
        config.language = QStringLiteral("es");
        config.urlTag = QStringLiteral("es");
        config.options = QJsonObject{{"languages", QJsonArray{"es", "es-419"}}};
        return config;
    }

    void kagane() {
        const comics::KaganeSource source(kaganeConfig(QStringLiteral("https://kagane.si")));
        const auto results = source.parseSearch(QJsonDocument::fromJson(fixture("kagane-search.json")).object());
        QCOMPARE(results.size(), 2);
        // The same series from two uploaders: the fuller first, with its
        // chapter count for matching ("Read from") and the results.
        QCOMPARE(results[0].title, QStringLiteral("ONE PIECE"));
        QCOMPARE(results[0].units, 1170);
        const auto &first = results[1];
        QCOMPARE(first.units, 12);
        QCOMPARE(first.title, QStringLiteral("One Piece"));
        QCOMPARE(first.url, QStringLiteral("https://kagane.si/series/019c3a10-837e-7d7c-bf6c-006df658a021#es"));
        // The site's own cover size, not the original.
        QCOMPARE(first.cover, QStringLiteral("https://kagane.si/api/v2/image/019e2c21-50db-789e-95a9-c20199434cf3/compressed"));
        QCOMPARE(first.type, QStringLiteral("Manga"));
        QVERIFY(first.names.contains(QString::fromUtf8("ワンピース")));

        const auto latest = source.parseLatest(QJsonDocument::fromJson(fixture("kagane-latest.json")).object());
        QCOMPARE(latest.size(), 2);
        QVERIFY(latest[0].chapterUrl.startsWith(QStringLiteral("https://kagane.si/series/019c340e-2d44-72ea-9ed9-8037f057079a/reader/")));
        QCOMPARE(latest[0].chapterNumber, QStringLiteral("56"));

        // Rows by the index's own column order; chapters without pages left
        // out, the site's {notes} dropped, a missing number from sort_no.
        const auto id = QStringLiteral("019c3a10-837e-7d7c-bf6c-006df658a021");
        const auto chapters = source.parseChapters(QJsonDocument::fromJson(fixture("kagane-index.json")).object(), id);
        QCOMPARE(chapters.size(), 4);
        QCOMPARE(chapters[0].number, QStringLiteral("1"));
        QCOMPARE(chapters[0].title, QStringLiteral("Chapter 1 - Volume 1 (1r0n)"));
        QCOMPARE(chapters[0].url, QStringLiteral("https://kagane.si/series/%1/reader/019e2d3b-e841-705c-a47c-c8e4659731d2#es").arg(id));
        QCOMPARE(chapters[0].scanlator, QStringLiteral("The Hours Between"));
        QCOMPARE(chapters[0].released, QStringLiteral("2026-05-15T20:02:41Z"));
        QCOMPARE(chapters[1].number, QStringLiteral("1.5"));
        QCOMPARE(chapters[1].title, QString::fromUtf8("Capítulo 1.5"));
        QCOMPARE(chapters[3].number, QStringLiteral("1176"));

        const auto details = source.parseDetails(QJsonDocument::fromJson(fixture("kagane-series.json")).object());
        QCOMPARE(details.title, QStringLiteral("One Piece"));
        QCOMPARE(details.cover, QStringLiteral("https://kagane.si/api/v2/image/019e2c21-50db-789e-95a9-c20199434cf3/compressed"));
        QCOMPARE(details.status, QStringLiteral("Ongoing"));
        QCOMPARE(details.description, QStringLiteral("Original Webtoon: KakaoPage and more."));
        QCOMPARE(details.author, QStringLiteral("Eiichiro Oda"));
        QCOMPARE(details.trackerIds.value("anilist").toInt(), 30013);
        QCOMPARE(details.trackerIds.value("mal").toInt(), 13);
        QVERIFY(details.genres.contains(QStringLiteral("Drama")));

        // Pages in page order, on the image host, with their token.
        const auto book = QStringLiteral("019e2d3b-f83f-7f88-97b6-4fe2caf9ab2e");
        const auto panels = comics::KaganeSource::parsePanels(QJsonDocument::fromJson(fixture("kagane-book.json")).object(), book);
        QCOMPARE(panels.size(), 3);
        QVERIFY(panels[0].url.startsWith(QStringLiteral("https://kstatic.to/api/v2/books/page/%1/d1ad59eb-6c09-585e-8151-4dc61172adb6.jxl?token=").arg(book)));
        QVERIFY(panels[0].url.endsWith(QStringLiteral("token=test.token%2B%2F%3D")));
        QCOMPARE(panels[0].width, 1500);
        QCOMPARE(panels[0].height, 2250);
    }

    void kaganeRequests() {
        // A stand-in site: the integrity token is fetched once, a refused
        // page request gets a fresh one, and search sends the filters.
        int integrity = 0, books = 0;
        QByteArray searchBody;
        Http::setTestHandler([&](const Request &request) -> std::optional<Response> {
            const auto path = request.url.path();
            Response response;
            response.status = 200;
            response.contentType = "application/json";
            response.finalUrl = request.url;
            if (path == QLatin1String("/api/integrity")) {
                ++integrity;
                response.body = QStringLiteral(R"({"token":"t%1","exp":%2})").arg(integrity).arg(QDateTime::currentSecsSinceEpoch() + 300).toUtf8();
            } else if (path.startsWith(QLatin1String("/api/v2/books/"))) {
                ++books;
                const auto token = std::find_if(request.headers.begin(), request.headers.end(), [](const auto &h) { return h.first == "x-integrity-token"; });
                if (books == 2 && token != request.headers.end() && token->second == "t1")
                    response.status = 401; // as when the token expired early
                else
                    response.body = fixture("kagane-book.json");
            } else if (path == QLatin1String("/api/v2/search/series")) {
                searchBody = request.body;
                response.body = fixture("kagane-search.json");
            } else {
                response.status = 404;
            }
            return response;
        });
        comics::KaganeSource source(kaganeConfig(QStringLiteral("https://kagane.test")));
        const auto chapter = QStringLiteral("https://kagane.test/series/019c3a10-837e-7d7c-bf6c-006df658a021/reader/019e2d3b-f83f-7f88-97b6-4fe2caf9ab2e#es");
        QCOMPARE(source.panels(chapter).size(), 3);
        QCOMPARE(source.panels(chapter).size(), 3);
        QCOMPARE(integrity, 2);
        QCOMPARE(books, 3);
        QCOMPARE(source.search(QStringLiteral("one piece")).size(), 2);
        const auto body = QJsonDocument::fromJson(searchBody).object();
        QCOMPARE(body.value("title").toString(), QStringLiteral("one piece"));
        QCOMPARE(body.value("content_lang").toArray(), (QJsonArray{"es", "es-419"}));
        QCOMPARE(body.value("is_pornographic").toBool(true), false);
        QVERIFY_THROWS_EXCEPTION(Error, source.panels(QStringLiteral("https://kagane.test/series/nope/reader/x")));
        Http::setTestHandler({});
    }

    void olympus() {
        const auto panels = comics::OlympusSource::parsePanels(fixture("olympus-reader.html"));
        QCOMPARE(panels.size(), 2);
        QCOMPARE(panels[0].url, QStringLiteral("https://media.imagesolymp.xyz/comics/5/106167/01.webp"));
    }

    void wtrLab() {
        // The site's JSON and page data, through a stand-in site.
        Http::setTestHandler([](const Request &request) -> std::optional<Response> {
            const auto path = request.url.path();
            const char *name = path == QLatin1String("/api/search")             ? "wtrlab-search.json"
                               : path.startsWith(QLatin1String("/api/chapters/")) ? "wtrlab-chapters.json"
                               : path.startsWith(QLatin1String("/api/v2/reader/content/")) ? "wtrlab-content.json"
                               : path == QLatin1String("/api/reader/get")
                                   ? (request.body.contains("\"chapter_no\":2") ? "wtrlab-locked.json" : "wtrlab-reader.json")
                               : path.startsWith(QLatin1String("/en/novel/")) ? "wtrlab-novel.html"
                                                                               : nullptr;
            Response response;
            response.status = name ? 200 : 404;
            response.body = name ? fixture(name) : QByteArray();
            response.contentType = path.endsWith(QLatin1String("-path")) ? "text/html" : "application/json";
            response.finalUrl = request.url;
            return response;
        });
        novels::SourceConfig config;
        config.key = QStringLiteral("wtrlab");
        config.label = QStringLiteral("WTR-LAB");
        config.engine = QStringLiteral("wtrlab");
        config.base = QStringLiteral("https://wtrlab.test");
        config.hosts = {QStringLiteral("wtrlab.test")};
        auto source = novels::createSource(config);
        QVERIFY(source);

        const auto found = source->search(QStringLiteral("test"));
        QCOMPARE(found.size(), 1);
        QCOMPARE(found[0].title, QStringLiteral("Test Land: The Test Path"));
        QCOMPARE(found[0].url, QStringLiteral("https://wtrlab.test/en/novel/42/the-test-path"));

        QList<novels::ChapterRef> chapters;
        const auto book = source->book(QStringLiteral("https://wtrlab.test/pt/novel/42/the-test-path/chapter-1"), chapters);
        QCOMPARE(book.url, QStringLiteral("https://wtrlab.test/en/novel/42/the-test-path"));
        QCOMPARE(book.description, QStringLiteral("A Hero walks."));
        QCOMPARE(book.status, QStringLiteral("Completed"));
        QCOMPARE(book.rating, QStringLiteral("4.3"));
        QCOMPARE(chapters.size(), 2);
        QCOMPARE(chapters[1].title, QStringLiteral("Chapter 2: Onward"));
        QCOMPARE(chapters[1].url, QStringLiteral("https://wtrlab.test/en/novel/42/the-test-path/chapter-2"));

        // Glossary terms filled in, empty lines dropped, unknown terms empty,
        // text escaped for the reader.
        QString title;
        const auto blocks = source->chapter(chapters[0].url, title);
        QCOMPARE(title, QStringLiteral("Chapter 1: A Start"));
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].text, QStringLiteral("The young hero met the Sect Master."));
        QCOMPARE(blocks[1].html, QStringLiteral("&lt;She&gt; said ."));

        // A locked chapter stays locked.
        try {
            source->chapter(chapters[1].url, title);
            QFAIL("a locked chapter loaded");
        } catch (const Error &e) {
            QVERIFY(e.message().contains(QStringLiteral("locked")));
        }
        Http::setTestHandler({});
    }

    // Search fans out to every source and shows each answer as it arrives;
    // one that hangs is cut off at the deadline instead of holding the rest.
    void fanOutStreamsAndTimesOut() {
        const auto stuck = std::make_shared<std::atomic_bool>(false);
        QList<std::function<int()>> jobs{
            [] {
                QThread::msleep(150);
                return 2;
            },
            [] { return 1; },
            [stuck] {
                while (!CancelScope::cancelled())
                    QThread::msleep(5);
                stuck->store(true);
                return 3;
            },
        };
        QList<int> order;
        QList<int> statuses(3, 0);
        QElapsedTimer timer;
        timer.start();
        qint64 firstAt = -1;
        parallelEach<int>(jobs, 400, [&](int i, const Outcome<int> &r) {
            if (firstAt < 0)
                firstAt = timer.elapsed();
            order.append(i);
            statuses[i] = r.ok() ? 200 : r.status;
        });
        QCOMPARE(order, (QList<int>{1, 0, 2}));
        QVERIFY2(firstAt < 100, qPrintable(QStringLiteral("first answer after %1 ms").arg(firstAt)));
        QCOMPARE(statuses, (QList<int>{200, 200, 504}));
        QTRY_VERIFY_WITH_TIMEOUT(stuck->load(), 1000);
    }

    // Library updates: one request at a time per site, sites side by side,
    // each outcome as it comes, and the fan-out pool left to searches.
    void batchesOneAtATimePerSite() {
        QMutex mutex;
        QHash<QString, int> running;
        QHash<QString, int> most;
        int sitesAtOnce = 0;
        int mostSitesAtOnce = 0;
        const QStringList keys{"a", "b", "a", "c", "a", "b"};
        QList<std::function<int()>> jobs;
        for (int i = 0; i < keys.size(); ++i)
            jobs.append([&, i, key = keys[i]] {
                {
                    QMutexLocker lock(&mutex);
                    if (running[key]++ == 0)
                        mostSitesAtOnce = std::max(mostSitesAtOnce, ++sitesAtOnce);
                    most[key] = std::max(most[key], running[key]);
                }
                // Long enough for the sites started together to overlap on a
                // busy machine.
                QThread::msleep(150);
                {
                    QMutexLocker lock(&mutex);
                    if (--running[key] == 0)
                        --sitesAtOnce;
                }
                if (i == 3)
                    throw Error(QStringLiteral("gone"), 404);
                return i * 10;
            });
        QList<int> order;
        const auto results = byKey(jobs, keys, 1, &order);
        for (int i = 0; i < 6; ++i)
            if (i != 3)
                QCOMPARE(*results[i].value, i * 10);
        QCOMPARE(results[3].status, 404);
        QCOMPARE(order.size(), 6);
        // A site's jobs one after another, in their order; sites side by
        // side. (Nothing timed: CI machines are slower by any amount.)
        QVERIFY(order.indexOf(0) < order.indexOf(2) && order.indexOf(2) < order.indexOf(4));
        QVERIFY(order.indexOf(1) < order.indexOf(5));
        QCOMPARE(most.value("a"), 1);
        QCOMPARE(most.value("b"), 1);
        QVERIFY(mostSitesAtOnce > 1);
        QCOMPARE(fanoutPool().activeThreadCount(), 0);

        // Cancelled: what has not begun is not run.
        const auto cancel = std::make_shared<std::atomic_bool>(true);
        CancelScope scope(cancel);
        for (const auto &r : byKey(jobs, keys, 1))
            QCOMPARE(r.status, 499);
    }

    // A site answering quickly is asked a little more at once, never more
    // than allowed; one that pushes back is paused and asked again; one
    // whose browser check could not be passed is not asked for the rest.
    void batchesFollowEachSitesPace() {
        QMutex mutex;
        QHash<QString, int> running;
        QHash<QString, int> most;
        QHash<QString, int> asked;
        int limited = 0;
        QStringList keys;
        for (int i = 0; i < 12; ++i)
            keys.append(QStringLiteral("quick"));
        keys << "busy" << "busy" << "checked" << "checked" << "checked";
        QList<std::function<int()>> jobs;
        for (int i = 0; i < keys.size(); ++i)
            jobs.append([&, i, key = keys[i]]() -> int {
                {
                    QMutexLocker lock(&mutex);
                    ++asked[key];
                    most[key] = std::max(most[key], ++running[key]);
                }
                // Quick for the scheduler (under 5 s), and long enough for the
                // three it allows at once to overlap on a busy machine.
                QThread::msleep(150);
                QMutexLocker lock(&mutex);
                --running[key];
                // The busy site's first answer is "too many requests".
                if (key == QLatin1String("busy") && limited++ == 0)
                    throw Error(QStringLiteral("Too many requests"), 429);
                if (key == QLatin1String("checked"))
                    throw Error(QStringLiteral("Browser check"), 403, QStringLiteral("https://checked.test/"));
                return i;
            });
        QElapsedTimer timer;
        timer.start();
        const auto results = byKey(jobs, keys, 3);
        QCOMPARE(most.value("quick"), 3);
        for (int i = 0; i < 12; ++i)
            QCOMPARE(*results[i].value, i);
        // Asked again after its pause, and both answered.
        QCOMPARE(asked.value("busy"), 3);
        QCOMPARE(*results[12].value, 12);
        QCOMPARE(*results[13].value, 13);
        QVERIFY(timer.elapsed() >= 4900);
        QCOMPARE(most.value("busy"), 1);
        // Only the first met the check; the rest were skipped.
        QCOMPARE(asked.value("checked"), 1);
        QCOMPARE(results[14].verifyUrl, QStringLiteral("https://checked.test/"));
        QCOMPARE(results[15].status, 499);
        QCOMPARE(results[16].status, 499);
    }

    void gunzip() {
        // Python's gzip, with a file name in the header.
        const auto packed = QByteArray::fromHex("1f8b08080000000002ff6261636b75702e7461636869626b000b7676f473f60f7255484a4cce2e2d50081ee58ff2e9c8070008b786ca80020000");
        QCOMPARE(local::gunzip(packed), QByteArray("SCANCORE backup ").repeated(40));
        QVERIFY(local::gunzip(packed.left(12)).isEmpty());
        QVERIFY(local::gunzip("not gzip at all").isEmpty());
    }

    void protoWriterRoundTrip() {
        // What a Mihon backup export writes reads back the same, gzipped.
        ProtoWriter chapter;
        chapter.text(1, QStringLiteral("/chapter/1"));
        chapter.boolean(4, true);
        chapter.fixed32(9, 12.5f);
        ProtoWriter manga;
        manga.varint(1, quint64(Q_INT64_C(2499283573021220255)));
        manga.text(3, QString::fromUtf8("Título"));
        manga.message(16, chapter);
        ProtoWriter backup;
        backup.message(1, manga);
        const auto packed = local::gzip(backup.data());
        QVERIFY(packed.startsWith("\x1f\x8b"));
        const auto data = local::gunzip(packed);
        QCOMPARE(data, backup.data());
        Proto root(data);
        QVERIFY(root.next());
        QCOMPARE(root.field(), 1);
        Proto m(root.bytes());
        QVERIFY(m.next());
        QCOMPARE(m.varint(), quint64(Q_INT64_C(2499283573021220255)));
        QVERIFY(m.next());
        QCOMPARE(m.text(), QString::fromUtf8("Título"));
        QVERIFY(m.next());
        QCOMPARE(m.field(), 16);
        Proto c(m.bytes());
        QVERIFY(c.next() && c.text() == QStringLiteral("/chapter/1"));
        QVERIFY(c.next() && c.varint() == 1);
        QVERIFY(c.next());
        QCOMPARE(c.number(), 12.5);
    }

    void manifestIsValid() {
        QFile file(QStringLiteral(SCANCORE_SOURCE_DIR "/sources/manifest.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto doc = QJsonDocument::fromJson(file.readAll()).object();
        QVERIFY(doc.value("version").toInt() >= 1);
        QSet<QString> keys;
        for (const auto &section : {QStringLiteral("comics"), QStringLiteral("novels")})
            for (const auto &value : doc.value(section).toArray()) {
                const auto o = value.toObject();
                const auto key = section + o.value("key").toString();
                QVERIFY2(!keys.contains(key), qPrintable(key));
                keys.insert(key);
                QVERIFY(o.value("base").toString().startsWith(QLatin1String("https://")));
                QVERIFY(!o.value("hosts").toArray().isEmpty());
                // Optional fields: a short language code, a tag, an app version.
                static const QRegularExpression code(QStringLiteral("^[a-z]{2,3}(-[a-z0-9]{2,8})?$"));
                if (o.contains("language"))
                    QVERIFY2(code.match(o.value("language").toString()).hasMatch(), qPrintable(key));
                if (o.contains("urlTag"))
                    QVERIFY2(!o.value("urlTag").toString().isEmpty(), qPrintable(key));
                if (o.contains("minVersion"))
                    QVERIFY2(!QVersionNumber::fromString(o.value("minVersion").toString()).isNull(), qPrintable(key));
                if (section == QLatin1String("comics")) {
                    comics::SourceConfig config;
                    config.engine = o.value("engine").toString();
                    QVERIFY2(comics::createSource(config) != nullptr, qPrintable(config.engine));
                } else {
                    novels::SourceConfig config;
                    config.engine = o.value("engine").toString();
                    QVERIFY2(novels::createSource(config) != nullptr, qPrintable(config.engine));
                }
            }
    }

    // Last: it opens a database (for the content language), which the
    // tests above run without.
    void resolveNames() {
        QTemporaryDir dir;
        Database::open(dir.filePath(QStringLiteral("resolve.db")));
        // No source is in the content language: the sources' own order.
        Database::setPref(QStringLiteral("language"), QStringLiteral("fr"));
        // Stand-in sources for "Read from".
        struct Config {
            QString language;
        };
        struct Fake {
            QString name;
            Config cfg;
            QList<comics::Summary> hits;
            QString key() const { return name; }
            QString label() const { return name; }
            const Config &config() const { return cfg; }
            QList<comics::Summary> search(const QString &) const { return hits; }
        };
        const auto hit = [](const QString &title, const QString &url, const QStringList &names = {}) {
            comics::Summary s;
            s.title = title;
            s.url = url;
            s.names = names;
            return s;
        };
        // Listed only under its Russian name, known in English as an
        // alternative: found. A spin-off carrying the series' name as an
        // alternative loses to the series itself.
        auto ru = std::make_shared<Fake>(Fake{QStringLiteral("ru"), {QStringLiteral("ru")},
                                              {hit(QString::fromUtf8("Поднятие уровня"), QStringLiteral("ru:1"), {QStringLiteral("Solo Leveling")})}});
        auto es = std::make_shared<Fake>(Fake{QStringLiteral("es"), {QStringLiteral("es")},
                                              {hit(QStringLiteral("Solo Leveling: Spin-off"), QStringLiteral("es:2"), {QStringLiteral("Solo Leveling")}),
                                               hit(QStringLiteral("Solo Leveling"), QStringLiteral("es:1"))}});
        auto none = std::make_shared<Fake>(Fake{QStringLiteral("en"), {QStringLiteral("en")}, {hit(QStringLiteral("Other"), QStringLiteral("en:1"))}});
        const auto matches = resolveAcross<std::shared_ptr<Fake>>({ru, es, none}, {QStringLiteral("Solo Leveling")});
        QCOMPARE(matches.size(), 2);
        QCOMPARE(matches[0].toMap().value("url").toString(), QStringLiteral("ru:1"));
        QCOMPARE(matches[1].toMap().value("url").toString(), QStringLiteral("es:1"));
    }

    // "Read from" asks built-in sources unless an extension is picked.
    void resolveScope() {
        const QString mangadex = QStringLiteral("mihon:2499283573021220255");
        QVERIFY(resolveAsks({}, QStringLiteral("asura"), true, false, true));
        QVERIFY(!resolveAsks({}, QStringLiteral("asura"), true, false, false));
        QVERIFY(!resolveAsks({}, mangadex, true, true, true));
        QVERIFY(resolveAsks(QStringLiteral("extensions"), mangadex, true, true, false));
        QVERIFY(!resolveAsks(QStringLiteral("extensions"), mangadex, false, true, false));
        QVERIFY(!resolveAsks(QStringLiteral("extensions"), QStringLiteral("asura"), true, false, true));
        QVERIFY(resolveAsks(mangadex, mangadex, true, true, false));
        QVERIFY(!resolveAsks(mangadex, QStringLiteral("mihon:1"), true, true, true));
        QVERIFY(!resolveAsks(mangadex, QStringLiteral("asura"), true, false, true));
    }
};

QTEST_GUILESS_MAIN(Parsing)
#include "tst_parsing.moc"
