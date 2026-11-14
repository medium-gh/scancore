#include "LnCrawl.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

namespace sc::novels {

QString LnCrawlRuntime::root() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/extensions/lncrawl");
}

QString LnCrawlRuntime::python() {
    QFile file(root() + QStringLiteral("/python-path.txt"));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

QString LnCrawlRuntime::vendor() { return root() + QStringLiteral("/vendor"); }
QString LnCrawlRuntime::bridge() { return root() + QStringLiteral("/bridge.py"); }

bool LnCrawlRuntime::installed() {
#ifdef SCANCORE_DISABLE_NOVEL_PACK
    return false;
#else
    const auto py = python();
    return !py.isEmpty() && QFile::exists(py) && QDir(vendor() + QStringLiteral("/sources")).exists() &&
           QFile::exists(bridge());
#endif
}

QJsonObject LnCrawlSource::run(QJsonObject request, int timeoutMs) {
    if (!LnCrawlRuntime::installed())
        throw Error(QStringLiteral("Install the extended novel sources in Settings to use %1.").arg(label()));
    request.insert("source", m_entry);
    request.insert("origin", origin());

#if !QT_CONFIG(process)
    // No Python to start (iOS); the pack cannot be installed there either.
    Q_UNUSED(timeoutMs)
    throw Error(QStringLiteral("Could not start the novel source runtime."));
#else
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONPATH"), LnCrawlRuntime::vendor());
    env.insert(QStringLiteral("SCANCORE_LNCRAWL_VENDOR"), LnCrawlRuntime::vendor());
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
    process.setProcessEnvironment(env);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    // -s skips the user's site-packages so only the pack's libraries load.
    process.start(LnCrawlRuntime::python(), {QStringLiteral("-s"), LnCrawlRuntime::bridge()});
    if (!process.waitForStarted(10000))
        throw Error(QStringLiteral("Could not start the novel source runtime."));
    process.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');

    QElapsedTimer timer;
    timer.start();
    QByteArray buffer;
    int fetches = 0;
    while (true) {
        if (CancelScope::cancelled()) {
            process.kill();
            throw Error(QStringLiteral("Cancelled"), 499);
        }
        if (timer.elapsed() > timeoutMs) {
            process.kill();
            throw Error(QStringLiteral("%1 took too long to respond.").arg(label()), 504);
        }
        if (!buffer.contains('\n') && !process.waitForReadyRead(200)) {
            if (process.state() == QProcess::NotRunning && !process.bytesAvailable())
                break;
            continue;
        }
        buffer += process.readAllStandardOutput();
        int newline;
        while ((newline = buffer.indexOf('\n')) >= 0) {
            const auto line = buffer.left(newline);
            buffer.remove(0, newline + 1);
            const auto message = QJsonDocument::fromJson(line).object();
            if (message.contains("result")) {
                process.closeWriteChannel();
                process.waitForFinished(2000);
                return message.value("result").toObject();
            }
            if (message.contains("failure"))
                throw Error(QStringLiteral("%1 could not read this page (%2).")
                                .arg(label(), message.value("failure").toString()),
                            502);
            const auto fetch = message.value("fetch").toObject();
            if (fetch.isEmpty())
                continue;
            QJsonObject answer;
            if (++fetches > 400) {
                answer.insert("error", "Too many requests for one operation");
            } else {
                try {
                    Request req;
                    req.url = QUrl(fetch.value("url").toString());
                    req.method = fetch.value("method").toString(QStringLiteral("GET")).toUtf8();
                    const auto headers = fetch.value("headers").toObject();
                    for (auto it = headers.begin(); it != headers.end(); ++it)
                        req.headers.append({it.key().toUtf8(), it.value().toString().toUtf8()});
                    if (!fetch.value("body").isNull())
                        req.body = fetch.value("body").toString().toUtf8();
                    req.timeoutMs = 20000;
                    req.maxBytes = 4 * 1024 * 1024;
                    const auto response = Http::send(req);
                    if (response.isChallenge())
                        throw Error(QStringLiteral("%1 asked for a browser check.").arg(req.url.host()), 403,
                                    QStringLiteral("%1://%2/").arg(req.url.scheme(), req.url.host()));
                    QJsonObject responseHeaders{{"content-type", QString::fromUtf8(response.contentType)}};
                    answer = QJsonObject{{"status", response.status},
                                         {"url", response.finalUrl.toString()},
                                         {"headers", responseHeaders},
                                         {"body", QString::fromUtf8(response.body)}};
                } catch (const Error &e) {
                    if (!e.verifyUrl().isEmpty()) {
                        process.kill();
                        throw;
                    }
                    answer.insert("error", e.message());
                }
            }
            process.write(QJsonDocument(answer).toJson(QJsonDocument::Compact) + '\n');
        }
    }
    const auto stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    throw Error(QStringLiteral("%1 stopped unexpectedly%2")
                    .arg(label(), stderrText.isEmpty() ? QStringLiteral(".") : QStringLiteral(": ") + stderrText.section('\n', -1)),
                502);
#endif
}

QList<Book> LnCrawlSource::search(const QString &query) {
    const auto result = run(QJsonObject{{"operation", "search"}, {"query", query}}, 40000);
    QList<Book> books;
    for (const auto &value : result.value("results").toArray()) {
        const auto row = value.toObject();
        Book book;
        book.source = key();
        book.title = row.value("title").toString();
        book.url = row.value("url").toString();
        book.description = row.value("info").toString();
        if (!book.title.isEmpty() && !book.url.isEmpty())
            books.append(book);
    }
    return books;
}

Book LnCrawlSource::book(const QString &url, QList<ChapterRef> &chapters) {
    const auto result = run(QJsonObject{{"operation", "book"}, {"url", url}}, 90000);
    Book book;
    book.source = key();
    book.url = url;
    book.title = result.value("title").toString();
    book.author = result.value("author").toString();
    book.cover = result.value("cover").toString();
    const auto description = result.value("description").toString();
    book.description = description.contains(QLatin1Char('<')) ? HtmlDocument(description).root().textTrim() : description;
    for (const auto &tag : result.value("tags").toArray())
        book.genres.append(tag.toString());
    for (const auto &value : result.value("chapters").toArray()) {
        const auto row = value.toObject();
        auto title = row.value("title").toString();
        if (title.isEmpty())
            title = QStringLiteral("Chapter %1").arg(chapters.size() + 1);
        chapters.append({row.value("url").toString(), title, QString::number(chapters.size() + 1)});
    }
    book.chapterCount = int(chapters.size());
    if (book.title.isEmpty())
        throw Error(QStringLiteral("%1 returned no book details.").arg(label()), 502);
    return book;
}

QList<Block> LnCrawlSource::chapter(const QString &url, QString &title) {
    const auto result = run(QJsonObject{{"operation", "chapter"}, {"url", url}, {"bookUrl", url}}, 60000);
    title = result.value("title").toString();
    HtmlDocument doc(QStringLiteral("<div id=\"sc\">%1</div>").arg(result.value("body").toString()));
    auto blocks = textBlocks(doc.first(QStringLiteral("#sc")));
    if (blocks.isEmpty())
        throw Error(QStringLiteral("%1 returned no readable text for this chapter.").arg(label()), 502);
    return blocks;
}

} // namespace sc::novels
