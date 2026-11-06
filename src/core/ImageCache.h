#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

namespace sc {

// Disk cache for remote images (covers and chapter pages), keyed by URL.
// Trimmed to a size budget on startup, least recently used first.
class ImageCache {
public:
    static void init(const QString &dir, qint64 budgetBytes);
    // Blocking; returns the image bytes from disk or the network.
    static QByteArray fetch(const QString &url, const QString &referer);
    // Drops what is kept for `url` (it could not be decoded), so the next
    // fetch downloads it again.
    static void forget(const QString &url);
    static void setBudget(qint64 bytes);
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
