#pragma once

#include "core/Http.h"

#include <QImage>
#include <QPointer>
#include <QQuickAsyncImageProvider>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QTimer>

namespace sc {

// An image file's picture at its own size, AVIF included (worker threads).
// Throws when it cannot be read.
QImage decodeImage(const QByteArray &data);

// image://remote/<url>~<referer> — covers and thumbnails, decoded at the
// requested size on a background thread through the disk cache.
class RemoteImageProvider : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
    static QString encode(const QString &url, const QString &referer);
};

// image://icon/<name>?color=#rrggbb — Lucide icons tinted to a theme colour.
class IconProvider : public QQuickImageProvider {
public:
    IconProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// A chapter page. Decodes off the GUI thread and draws the image as a stack
// of tiles, so very tall webtoon strips stay sharp without exceeding the
// GPU's texture size limit. Pages are decoded at the size they are drawn
// at on screen (Qt's area-averaging scale, which keeps screentones free of
// moiré) and drawn pixel for pixel; when that size changes (a wider strip,
// a zoom, another screen) the page is decoded again, the old picture
// showing until the new one is there.
class PanelImage : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString referer READ referer WRITE setReferer NOTIFY sourceChanged)
    // The most pixels across a page is decoded at, however large it is drawn.
    Q_PROPERTY(int maxDecodeWidth READ maxDecodeWidth WRITE setMaxDecodeWidth NOTIFY sourceChanged)
    // A scale the page is under that it cannot see (a zoom layer above it).
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    // The box a page is fitted into (paged reading), when its width follows
    // from its shape: it is then decoded for the width it will have, known
    // from the file before decoding, instead of the one it has now.
    Q_PROPERTY(QSizeF fitIn READ fitIn WRITE setFitIn NOTIFY fitInChanged)
    // Only fetched into the cache and measured (sourceWidth, sourceHeight
    // from the file's header), never decoded or drawn: pages read ahead.
    Q_PROPERTY(bool preload READ preload WRITE setPreload NOTIFY sourceChanged)
    // Trims a scan's plain white or black margin.
    Q_PROPERTY(bool cropBorders READ cropBorders WRITE setCropBorders NOTIFY sourceChanged)
    // "grey", "invert" or both ("grey invert"), applied while decoding, so
    // drawing costs nothing extra.
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY sourceChanged)
    // 0 to 1: how much a page drawn larger than its picture (a small scan)
    // is sharpened as it is enlarged (resources/shaders/panel.frag).
    Q_PROPERTY(qreal sharpen READ sharpen WRITE setSharpen NOTIFY sharpenChanged)
    // "left" or "right": only that half (a wide spread shown as two pages).
    Q_PROPERTY(QString half READ half WRITE setHalf NOTIFY sourceChanged)
    // Smooth shrinking (mipmaps) for pages shown smaller than they were
    // decoded: fine lines and screentones stay clean instead of shimmering.
    // A third more video memory. Pages decoded for a zoom have them anyway,
    // for the moment between zooming out and the page being decoded again.
    Q_PROPERTY(bool mipmap READ mipmap WRITE setMipmap NOTIFY mipmapChanged)
    Q_PROPERTY(int sourceWidth READ sourceWidth NOTIFY loaded)
    Q_PROPERTY(int sourceHeight READ sourceHeight NOTIFY loaded)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)

public:
    enum Status { Null, Loading, Ready, Error };
    Q_ENUM(Status)

    explicit PanelImage(QQuickItem *parent = nullptr);
    ~PanelImage() override;

    QString source() const { return m_source; }
    void setSource(const QString &source);
    QString referer() const { return m_referer; }
    void setReferer(const QString &referer);
    int maxDecodeWidth() const { return m_maxDecodeWidth; }
    void setMaxDecodeWidth(int width);
    bool cropBorders() const { return m_cropBorders; }
    void setCropBorders(bool crop);
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    qreal sharpen() const { return m_sharpen; }
    void setSharpen(qreal sharpen);
    QString half() const { return m_half; }
    void setHalf(const QString &half);
    bool mipmap() const { return m_mipmap; }
    void setMipmap(bool mipmap);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    QSizeF fitIn() const { return m_fitIn; }
    void setFitIn(const QSizeF &box);
    bool preload() const { return m_preload; }
    void setPreload(bool preload);
    int sourceWidth() const { return m_size.width(); }
    int sourceHeight() const { return m_size.height(); }
    Status status() const { return m_status; }
    QString errorString() const { return m_error; }

    Q_INVOKABLE void reload();

signals:
    void sourceChanged();
    void sharpenChanged();
    void loaded();
    void statusChanged();
    void mipmapChanged();
    void zoomChanged();
    void fitInChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void componentComplete() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    // `keep`: the picture on screen stays until the new one is decoded
    // (the same page at another size).
    void start(bool keep = false);
    // The pixels across the page is drawn at, no more than maxDecodeWidth.
    int targetWidth() const;
    // Decodes again, a moment after the drawn size settles, when it moved
    // far enough from the decoded one.
    void checkSize();
    // Starts once the current round of property changes is over, so a
    // source and its referer set together load together.
    void startSoon();
    void setStatus(Status status, const QString &error = {});

    QString m_source;
    QString m_referer;
    int m_maxDecodeWidth = 1600;
    bool m_cropBorders = false;
    QString m_filter;
    qreal m_sharpen = 0;
    QString m_half;
    bool m_mipmap = false;
    qreal m_zoom = 1;
    QSizeF m_fitIn;
    bool m_preload = false;
    // The width the picture on screen was decoded for, and whether it is
    // all the picture has (no sharper decode to be had).
    int m_decodedFor = 0;
    bool m_full = false;
    // The tiles were decoded for a zoom (mipmapped, see mipmap), and the
    // textures on screen have mipmaps.
    bool m_zoomedTiles = false;
    bool m_textureMipmaps = false;
    QTimer *m_resize = nullptr;
    QSize m_size;
    Status m_status = Null;
    QString m_error;
    QList<QImage> m_pendingTiles;
    bool m_tilesDirty = false;
    CancelFlag m_cancel;
    int m_generation = 0;
    bool m_startQueued = false;
};

} // namespace sc
