#include "core/Aes.h"
#include "core/Epub.h"
#include "core/Text.h"
#include "core/translation/Cloud.h"
#include "core/translation/Layout.h"
#include "core/Http.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "sources/local/AudioTags.h"
#include "sources/local/LocalSources.h"

#include <QBuffer>
#include <QtEndian>

#include <QTemporaryDir>
#include <QUrl>

#include <QTest>

using namespace sc;

// Text helpers that need no network or database.
class TextTests : public QObject {
    Q_OBJECT

    static QStringList split(const QString &text) {
        QStringList out;
        for (const auto &[start, length] : sentenceSpans(text))
            out.append(text.mid(start, length));
        return out;
    }

private slots:
    // One number per chapter whichever source names it, so it shares one
    // discussion and moves between sources by number (v3 plan, section 5).
    void chapterNumbers() {
        const QList<std::tuple<QString, QString, QString>> cases{
            {"12", "", "12"},
            {"012", "", "12"},
            {"12.50", "", "12.5"},
            {"12,5", "", "12.5"},
            {"0", "", "0"},
            {"", "Chapter 12", "12"},
            {"", "Ch. 12.5 - The Return", "12.5"},
            {"", "Vol. 3 Ch. 12", "12"},
            {"Vol.3 Ch.12", "", "12"},
            {"", "Vol.03 Chapter 007: Night", "7"},
            {"", "Episode 5", "5"},
            {"", "Ep. 05", "5"},
            {"", "#45", "45"},
            {"", "Capítulo 7", "7"},
            {"", "Chương 9", "9"},
            {"", QString::fromUtf8("第12話"), "12"},
            {"", QString::fromUtf8("第 3 话 再会"), "3"},
            {"", QString::fromUtf8("제12화"), "12"},
            {"", QString::fromUtf8("12화"), "12"},
            {"", "12 - A New Start", "12"},
            {"", "Chapter 12a", "12"},
            {"", "Chapter 12 Part 2", "12"},
            {"", "Prologue", "0"},
            // A volume or an extra is not a numbered chapter.
            {"", "Volume 4", ""},
            {"", "Extra 2", ""},
            {"", QString::fromUtf8("番外 1"), ""},
            {"", "Oneshot", ""},
            {"", "", ""},
            // The number field wins over a title that disagrees.
            {"13", "Chapter 12.5 (Special)", "13"},
        };
        for (const auto &[number, title, expected] : cases)
            QCOMPARE(chapterNumber(number, title), expected);
    }

    // Music files' own tags, from hand-made headers of each kind.
    void readsAudioTags() {
        const auto be = [](quint32 v) { QByteArray b(4, 0); qToBigEndian(v, b.data()); return b; };
        const auto le = [](quint32 v) { QByteArray b(4, 0); qToLittleEndian(v, b.data()); return b; };
        const auto read = [](const QByteArray &data, const char *name) {
            QBuffer buffer;
            buffer.setData(data);
            buffer.open(QIODevice::ReadOnly);
            return local::readAudioTags(buffer, QString::fromLatin1(name), true);
        };
        // ID3v2.3: text in Latin-1 and UTF-16, a front cover, then an MPEG-1
        // Layer III frame whose Xing header says 1000 frames (26.1 s).
        const auto frame = [&](const char *id, const QByteArray &body) { return QByteArray(id) + be(body.size()) + QByteArray(2, 0) + body; };
        QByteArray utf16("\x01\xff\xfe", 3);
        for (const QChar c : QStringLiteral("Gurenge"))
            utf16 += QByteArray(1, char(c.unicode() & 0xff)) + QByteArray(1, char(c.unicode() >> 8));
        auto frames = frame("TIT2", utf16) + frame("TPE1", QByteArray("\0LiSA", 5)) + frame("TRCK", QByteArray("\0" "3/12", 5)) +
                      frame("APIC", QByteArray("\0image/jpeg\0\x03\0", 14) + QByteArray("\xff\xd8JPEGDATA", 10));
        QByteArray size(4, 0);
        for (int i = 0; i < 4; ++i)
            size[i] = char((frames.size() >> (7 * (3 - i))) & 0x7f);
        QByteArray mpeg("\xff\xfb\x90\x00", 4);
        mpeg += QByteArray(32, 0) + "Xing" + be(1) + be(1000) + QByteArray(400, 0);
        const auto mp3 = read(QByteArray("ID3\x03\x00\x00", 6) + size + frames + mpeg, "a.mp3");
        QCOMPARE(mp3.title, QStringLiteral("Gurenge"));
        QCOMPARE(mp3.artists, QStringList{QStringLiteral("LiSA")});
        QCOMPARE(mp3.track, 3);
        QCOMPARE(mp3.art, QByteArray("\xff\xd8JPEGDATA"));
        QCOMPARE(qRound(mp3.duration * 10), 261);

        // FLAC: STREAMINFO (44.1 kHz, 441000 samples) and Vorbis comments.
        QByteArray info(18, 0);
        info[10] = char(0x0a); info[11] = char(0xc4); info[12] = char(0x42); // 44100 << 4 | channels
        qToBigEndian<quint32>(441000, info.data() + 14);
        QByteArray comments = le(6) + "vendor" + le(3);
        for (const QByteArray c : {QByteArray("TITLE=Zankyou Sanka"), QByteArray("ARTIST=Aimer"), QByteArray("LYRICS=[00:01.00]hi")})
            comments += le(c.size()) + c;
        const auto block = [&](int type, bool last, const QByteArray &body) {
            return QByteArray(1, char(type | (last ? 0x80 : 0))) + be(body.size()).mid(1) + body;
        };
        const auto flac = read("fLaC" + block(0, false, info) + block(4, true, comments), "b.flac");
        QCOMPARE(flac.title, QStringLiteral("Zankyou Sanka"));
        QCOMPARE(flac.artists, QStringList{QStringLiteral("Aimer")});
        QCOMPARE(flac.lyrics, QStringLiteral("[00:01.00]hi"));
        QCOMPARE(flac.duration, 10.0);

        // M4A: moov after the audio, mvhd (1000 per second, 95 s), ilst.
        const auto atom = [&](const QByteArray &type, const QByteArray &body) { return be(body.size() + 8) + type + body; };
        const auto item = [&](const QByteArray &type, int kind, const QByteArray &value) {
            return atom(type, atom("data", be(kind) + be(0) + value));
        };
        QByteArray mvhd(100, 0);
        qToBigEndian<quint32>(1000, mvhd.data() + 12);
        qToBigEndian<quint32>(95000, mvhd.data() + 16);
        const auto ilst = atom("ilst", item("\xa9nam", 1, "Unravel") + item("\xa9" "ART", 1, "TK") +
                                           item("trkn", 0, QByteArray("\0\0\0\x07\0\x0c\0\0", 8)));
        const auto moov = atom("moov", atom("mvhd", mvhd) + atom("udta", atom("meta", QByteArray(4, 0) + atom("hdlr", QByteArray(25, 0)) + ilst)));
        const auto m4a = read(atom("ftyp", "M4A ") + atom("mdat", QByteArray(64, 0)) + moov, "c.m4a");
        QCOMPARE(m4a.title, QStringLiteral("Unravel"));
        QCOMPARE(m4a.artists, QStringList{QStringLiteral("TK")});
        QCOMPARE(m4a.track, 7);
        QCOMPARE(m4a.duration, 95.0);

        // Not music, or cut short: nothing, and no crash.
        QVERIFY(read(QByteArray("ID3\x04\x00\x00\x7f\x7f\x7f\x7f", 10), "d.mp3").title.isEmpty());
        QVERIFY(read("fLaC", "e.flac").title.isEmpty());
        QVERIFY(read(atom("moov", QByteArray(3, 0)), "f.m4a").title.isEmpty());
        QVERIFY(local::isAudio(QStringLiteral("x.OPUS")));
        QVERIFY(!local::isAudio(QStringLiteral("x.lrc")));
    }
    // Reading aloud speaks a sentence at a time.
    void sentences() {
        QCOMPARE(split(QStringLiteral("He ran. She stayed! Why?  ")),
                 (QStringList{QStringLiteral("He ran."), QStringLiteral("She stayed!"), QStringLiteral("Why?")}));
        // Quotes stay with their sentence; a lone ellipsis is not one.
        QCOMPARE(split(QStringLiteral("“Wait.” He turned. …")),
                 (QStringList{QStringLiteral("“Wait.”"), QStringLiteral("He turned.")}));
        // Chinese and Japanese full stops.
        QCOMPARE(split(QStringLiteral("他走了。她留下！为什么？")),
                 (QStringList{QStringLiteral("他走了。"), QStringLiteral("她留下！"), QStringLiteral("为什么？")}));
        QCOMPARE(split(QString()).size(), 0);
        QCOMPARE(split(QStringLiteral("***")).size(), 0);
        // Positions point into the original text.
        const auto spans = sentenceSpans(QStringLiteral("  One. Two."));
        QCOMPARE(spans.size(), 2);
        QCOMPARE(spans[0].first, 2);
        QCOMPARE(spans[1].first, 7);
        QCOMPARE(spans[1].second, 4);
    }

    // CryptoJS's AES, as sites use it for chapter lists (vector made with
    // OpenSSL's own AES-256-CBC and key derivation).
    void decryptsCryptoJs() {
        const auto data = QByteArray::fromBase64(
            "NOU6l25NzRPiNPCe9QKc2dUf6ctPLw7uP7yM14/ZyFnJBQi7f98Q40HAU/wU1NuDLd430sJ9ufOvGBO4Bw8ebyoYp6fOZlqHB5+Weu4czEw=");
        const auto out = cryptoJsDecrypt("not-the-site-key", QByteArray::fromHex("0123456789abcdef"), data);
        QVERIFY(out.has_value());
        QCOMPARE(QString::fromUtf8(*out), QStringLiteral("[{\"slug\":\"1-abc\",\"number\":\"1\"},{\"slug\":\"2-def\",\"number\":\"2\"}] — capítulo"));
        // The wrong key: the padding gives it away.
        QVERIFY(!cryptoJsDecrypt("wrong", QByteArray::fromHex("0123456789abcdef"), data).has_value());
    }

    // Page translation: lines found on a page become bubbles.
    void groupsLinesIntoBubbles() {
        using namespace translation;
        // Two bubbles of horizontal Korean text, far apart.
        const QList<TextLine> lines{{QRectF(100, 100, 200, 30), QStringLiteral("안녕하세요,")},
                                    {QRectF(110, 135, 180, 30), QStringLiteral("반갑습니다.")},
                                    {QRectF(500, 600, 150, 30), QStringLiteral("뭐라고?")}};
        const auto blocks = groupLines(lines, QStringLiteral("ko"));
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].text, QStringLiteral("안녕하세요, 반갑습니다."));
        QCOMPARE(blocks[0].box, QRectF(100, 100, 200, 65));
        QCOMPARE(blocks[1].text, QStringLiteral("뭐라고?"));
        // Japanese columns read right to left, joined without spaces.
        const QList<TextLine> columns{{QRectF(200, 100, 30, 150), QStringLiteral("ですか")},
                                      {QRectF(240, 100, 30, 160), QStringLiteral("本当")}};
        const auto ja = groupLines(columns, QStringLiteral("ja"));
        QCOMPARE(ja.size(), 1);
        QVERIFY(ja[0].vertical);
        QCOMPARE(ja[0].text, QStringLiteral("本当ですか"));
        // A word broken over two lines joins again.
        const auto en = groupLines({{QRectF(0, 0, 100, 20), QStringLiteral("a won-")}, {QRectF(0, 24, 100, 20), QStringLiteral("derful day")}},
                                   QStringLiteral("en"));
        QCOMPARE(en.size(), 1);
        QCOMPARE(en.first().text, QStringLiteral("a wonderful day"));
    }

    // The services, answered here: what is sent and how answers are read.
    void cloudEngines() {
        QList<Request> seen;
        Http::setTestHandler([&](const Request &request) -> std::optional<Response> {
            seen.append(request);
            Response r;
            r.status = 200;
            const auto host = request.url.host();
            if (host == QLatin1String("api-free.deepl.com"))
                r.body = R"({"translations":[{"text":"Hello"},{"text":"What?"}]})";
            else if (host == QLatin1String("translation.googleapis.com"))
                r.body = R"({"data":{"translations":[{"translatedText":"Hello"}]}})";
            else if (host == QLatin1String("llm.test"))
                r.body = QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"message", QJsonObject{{"content", "```json\n[\"Hi\", \"No way\"]\n```"}}}}}}})
                             .toJson();
            else if (host == QLatin1String("vision.googleapis.com"))
                r.body = R"({"responses":[{"fullTextAnnotation":{"pages":[{"blocks":[{"paragraphs":[
                    {"boundingBox":{"vertices":[{"x":10,"y":10},{"x":110,"y":10},{"x":110,"y":40},{"x":10,"y":40}]},
                     "words":[{"symbols":[{"text":"안"},{"text":"녕","property":{"detectedBreak":{"type":"SPACE"}}}]},
                              {"symbols":[{"text":"하"},{"text":"세"},{"text":"요"}]}]}]}]}]}}]})";
            else
                return std::nullopt;
            return r;
        });
        using namespace translation;
        QCOMPARE(deepl({QStringLiteral("안녕"), QStringLiteral("뭐?")}, QStringLiteral("ko"), QStringLiteral("en"), QStringLiteral("k:fx")),
                 (QStringList{QStringLiteral("Hello"), QStringLiteral("What?")}));
        QCOMPARE(seen.last().headers.value(0).second, QByteArray("DeepL-Auth-Key k:fx"));
        const auto deeplBody = QJsonDocument::fromJson(seen.last().body).object();
        QCOMPARE(deeplBody.value(QStringLiteral("target_lang")).toString(), QStringLiteral("EN-US"));
        QCOMPARE(deeplBody.value(QStringLiteral("source_lang")).toString(), QStringLiteral("KO"));

        QCOMPARE(google({QStringLiteral("안녕")}, QString(), QStringLiteral("en"), QStringLiteral("g")), QStringList{QStringLiteral("Hello")});
        QVERIFY(seen.last().url.query().contains(QStringLiteral("key=g")));

        QCOMPARE(chatModel({QStringLiteral("안녕"), QStringLiteral("말도 안 돼")}, QStringLiteral("ko"), QStringLiteral("en"),
                           QStringLiteral("http://llm.test/v1/"), QString(), QStringLiteral("m")),
                 (QStringList{QStringLiteral("Hi"), QStringLiteral("No way")}));
        QCOMPARE(seen.last().url.path(), QStringLiteral("/v1/chat/completions"));

        const auto blocks = googleVision("jpeg", QStringLiteral("ko"), QStringLiteral("g"));
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].text, QStringLiteral("안녕 하세요"));
        QCOMPARE(blocks[0].box, QRectF(10, 10, 100, 30));

        // No key: a message saying where to put it, nothing sent.
        const auto before = seen.size();
        QVERIFY_THROWS_EXCEPTION(Error, deepl({QStringLiteral("x")}, {}, QStringLiteral("en"), {}));
        QCOMPARE(seen.size(), before);
        Http::setTestHandler({});
    }

    // Exported novels open in SCANCORE's own EPUB reader again, chapters,
    // titles and text intact, markup that is not XHTML written as text.
    void epubRoundTrip() {
        EpubBook book;
        book.id = QStringLiteral("novel:test:1");
        book.title = QStringLiteral("The Lantern & the Hill");
        book.author = QStringLiteral("Test Author");
        book.chapters.append({QStringLiteral("One"),
                              {QVariantMap{{"kind", "paragraph"}, {"text", "Plain text."}, {"html", "Plain <i>text</i>."}},
                               QVariantMap{{"kind", "break"}},
                               QVariantMap{{"kind", "paragraph"}, {"text", "Broken < markup"}, {"html", "Broken <b>markup"}}}});
        book.chapters.append({QStringLiteral("Two"), {QVariantMap{{"kind", "paragraph"}, {"text", "Second."}, {"html", "Second."}}}});
        const auto bytes = writeEpub(book);
        QVERIFY(bytes.mid(30, 8) == "mimetype");
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("book.epub"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
        file.close();

        novels::SourceConfig config;
        config.key = QStringLiteral("local");
        const auto source = local::createNovelSource(config);
        QList<novels::ChapterRef> chapters;
        const auto read = source->book(QUrl::fromLocalFile(path).toString(), chapters);
        QCOMPARE(read.title, book.title);
        QCOMPARE(chapters.size(), 2);
        QString title;
        const auto blocks = source->chapter(chapters[0].url, title);
        QCOMPARE(title, QStringLiteral("One"));
        QStringList texts;
        for (const auto &b : blocks)
            texts.append(b.text);
        QVERIFY2(texts.contains(QStringLiteral("Plain text.")), qPrintable(texts.join(" | ")));
        QVERIFY2(texts.contains(QStringLiteral("Broken < markup")), qPrintable(texts.join(" | ")));
    }
};

QTEST_GUILESS_MAIN(TextTests)
#include "tst_text.moc"
