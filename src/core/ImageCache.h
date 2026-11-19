#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

namespace sc {

// Disk cache for remote images (covers and chapter pages), keyed by URL.
// Trimmed to a size budget on startup, least recently used first.
// Covers the app should never lose (series in the library, ones read
// lately; see setKeptCovers) live in a "covers" folder beside it instead,
// as Mihon keeps its library covers: neither the budget nor clear() removes
// them, so they show without asking the site again (and meeting its
// browser check).
class ImageCache {
public:
    static void init(const QString &dir, qint64 budgetBytes);
    // The links of the covers to keep, asked for each time the cache is
    // trimmed or cleared. Kept covers already downloaded move to the
    // covers folder then; ones no longer listed move back and age out.
    static void setKeptCovers(std::function<QStringList()> urls);
    // Blocking; returns the image bytes from disk or the network.
    static QByteArray fetch(const QString &url, const QString &referer);
    // Drops what is kept for `url` (it could not be decoded), so the next
    // fetch downloads it again.
    static void forget(const QString &url);
    static void setBudget(qint64 bytes);
    // What the budget and clear() apply to: kept covers are not counted.
    static qint64 sizeOnDisk();
    static void clear();
    static void trim();

    // Images that are not plain links ("extimage:..." from extensions):
    // `writer` puts the image for `url` into the cache file at `path`, or
    // throws. Duplicate requests still share one fetch.
    using Writer = std::function<void(const QString &url, const QString &path)>;
    static void setSchemeWriter(const QString &scheme, Writer writer);
    // Plain links whose "referer" names a scheme instead of a page
    // ("extsource:<key>"): the writer fetches them its own way (an
    // extension's client, with its headers). They are cached under the
    // link, so a later plain request for it finds the file.
    using RefererWriter = std::function<void(const QString &url, const QString &referer, const QString &path)>;
    static void setRefererWriter(const QString &scheme, RefererWriter writer);
};

} // namespace sc
