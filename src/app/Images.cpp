#include "Images.h"

#include "Platform.h"
#include "core/ImageCache.h"

#include <QBuffer>
#include <QCache>
#include <QGuiApplication>
#include <QFile>
#include <QImageReader>
#include <QMutex>
#include <QPainter>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>
#include <QSvgRenderer>
#include <QThreadPool>
#include <QtEndian>
#include <QtMath>

#include <cmath>
#include <cstring>
#include <functional>
#include <memory>

#ifdef SCANCORE_HAVE_AVIF
#include <avif/avif.h>
#endif

namespace sc {

namespace {

QThreadPool &coverPool() {
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(8);
        return p;
    }();
    return *pool;
}

QThreadPool &panelPool() {
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(6);
        return p;
    }();
    return *pool;
}

QString b64(const QString &value) {
    return QString::fromLatin1(value.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString unb64(const QString &value) {
    return QString::fromUtf8(QByteArray::fromBase64(value.toLatin1(), QByteArray::Base64UrlEncoding));
}

// AVIF (an ISO media file of brand avif or avis): Qt has no decoder of its
// own, so libavif (desktop) or the system (Android) decodes it.
bool isAvif(const QByteArray &data) {
    if (data.size() < 16 || data.mid(4, 4) != "ftyp")
        return false;
    const auto box = data.mid(8, std::min<qsizetype>(qFromBigEndian<quint32>(data.constData()) - 8, 64));
    return box.contains("avif") || box.contains("avis");
}

QImage decodeAvif(const QByteArray &data) {
#ifdef SCANCORE_HAVE_AVIF
    QImage out;
    avifDecoder *decoder = avifDecoderCreate();
    if (!decoder)
        return out;
    decoder->maxThreads = 2;
    decoder->strictFlags = AVIF_STRICT_DISABLED;
    if (avifDecoderSetIOMemory(decoder, reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size())) == AVIF_RESULT_OK &&
        avifDecoderParse(decoder) == AVIF_RESULT_OK && avifDecoderNextImage(decoder) == AVIF_RESULT_OK) {
        avifRGBImage rgb;
        avifRGBImageSetDefaults(&rgb, decoder->image);
        rgb.format = AVIF_RGB_FORMAT_RGBA;
        rgb.depth = 8;
        out = QImage(int(decoder->image->width), int(decoder->image->height), QImage::Format_RGBA8888);
        rgb.pixels = out.bits();
        rgb.rowBytes = uint32_t(out.bytesPerLine());
        if (out.isNull() || avifImageYUVToRGB(decoder->image, &rgb) != AVIF_RESULT_OK)
            out = QImage();
    }
    avifDecoderDestroy(decoder);
    return out;
#elif defined(Q_OS_ANDROID)
    return platform::decodeImage(data);
#else
    Q_UNUSED(data)
    return {};
#endif
}

// The size an image of `size` is decoded at: to cover `requested` (covers),
// or no wider than `maxWidth` (pages).
QSize decodedSize(const QSize &size, const QSize &requested, int maxWidth) {
    QSize target = size;
    if (requested.width() > 0 && requested.height() > 0)
        target = size.scaled(requested, Qt::KeepAspectRatioByExpanding);
    else if (requested.width() > 0 && size.width() > requested.width())
        target = QSize(requested.width(), size.height() * requested.width() / size.width());
    else if (requested.height() > 0 && size.height() > requested.height())
        target = QSize(size.width() * requested.height() / size.height(), requested.height());
    if (maxWidth > 0 && target.width() > maxWidth)
        target = QSize(maxWidth, target.height() * maxWidth / target.width());
    return target.width() < size.width() ? target : size;
}

// `full`: set to whether the image came out at its own size (no sharper
// decode to be had). `widthFor`, when given, says the width to decode at
// once the image's own size is known.
QImage decode(const QByteArray &data, const QSize &requested, int maxWidth = 0, bool *full = nullptr,
              const std::function<int(const QSize &)> &widthFor = {}) {
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    // A Qt image plugin for AVIF (KDE's, in the Flatpak) goes first.
    if (isAvif(data) && !reader.canRead()) {
        auto image = decodeAvif(data);
        if (image.isNull())
            throw Error(QStringLiteral("This AVIF image could not be read."));
        if (widthFor)
            maxWidth = widthFor(image.size());
        const auto target = decodedSize(image.size(), requested, maxWidth);
        if (full)
            *full = target == image.size();
        return target == image.size() ? image : image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    auto size = reader.size();
    if (full)
        *full = true;
    if (size.isValid() && widthFor) {
        // As it will be shown: a turned photo's width is its height.
        const bool turned = reader.transformation() & QImageIOHandler::TransformationRotate90;
        maxWidth = widthFor(turned ? size.transposed() : size);
        if (turned)
            maxWidth = maxWidth * size.width() / std::max(1, size.height());
    }
    if (size.isValid()) {
        const auto target = decodedSize(size, requested, maxWidth);
        if (target != size)
            reader.setScaledSize(target);
        if (full)
            *full = target == size;
    }
    QImage image = reader.read();
    if (image.isNull())
        throw Error(QStringLiteral("This image format could not be read."));
    return image;
}

// A cover being fetched. The engine deletes a response it no longer wants
// (the cover scrolled away) whenever it likes, also while the fetch runs or
// before it starts, so the fetch is a job of its own: it shares this state
// with the response and tells the response it is done only while the
// response is known to be alive. Signalling from the response itself, on the
// pool's thread, used a deleted object when a fast scroll dropped covers
// (crash at 0x20 on a slow phone).
struct RemoteState {
    QMutex mutex;
    QQuickImageResponse *response = nullptr; // null once it is deleted
    QImage image;
    QString error;
};

class RemoteResponse : public QQuickImageResponse {
public:
    RemoteResponse(std::shared_ptr<RemoteState> state, CancelFlag cancel) : m_state(std::move(state)), m_cancel(std::move(cancel)) {
        m_state->response = this;
    }
    ~RemoteResponse() override {
        m_cancel->store(true);
        QMutexLocker lock(&m_state->mutex);
        m_state->response = nullptr;
    }

    QQuickTextureFactory *textureFactory() const override {
        QMutexLocker lock(&m_state->mutex);
        return QQuickTextureFactory::textureFactoryForImage(m_state->image);
    }
    QString errorString() const override {
        QMutexLocker lock(&m_state->mutex);
        return m_state->error;
    }
    void cancel() override { m_cancel->store(true); }

private:
    std::shared_ptr<RemoteState> m_state;
    CancelFlag m_cancel;
};

class RemoteJob : public QRunnable {
public:
    RemoteJob(QString url, QString referer, QSize size, std::shared_ptr<RemoteState> state, CancelFlag cancel)
        : m_url(std::move(url)), m_referer(std::move(referer)), m_size(size), m_state(std::move(state)), m_cancel(std::move(cancel)) {}

    void run() override {
        QImage image;
        QString error;
        if (m_cancel->load()) {
            error = QStringLiteral("Cancelled");
        } else {
            CancelScope scope(m_cancel);
            try {
                image = decode(ImageCache::fetch(m_url, m_referer), m_size);
            } catch (const Error &e) {
                error = e.message();
            } catch (const std::exception &e) {
                error = QString::fromUtf8(e.what());
            }
        }
        QMutexLocker lock(&m_state->mutex);
        m_state->image = std::move(image);
        m_state->error = error;
        // Posted while the response cannot go away; a response deleted
        // later drops the event with it.
        if (auto *response = m_state->response)
            QMetaObject::invokeMethod(response, &QQuickImageResponse::finished, Qt::QueuedConnection);
    }

private:
    QString m_url;
    QString m_referer;
    QSize m_size;
    std::shared_ptr<RemoteState> m_state;
    CancelFlag m_cancel;
};

constexpr int kTileHeight = 4096;

// An image's size from its header, turned as it is shown, without decoding
// it (unless no reader knows it, as AVIF without a plugin).
QSize measure(const QByteArray &data) {
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    auto size = reader.size();
    if (size.isValid() && (reader.transformation() & QImageIOHandler::TransformationRotate90))
        size.transpose();
    return size.isValid() ? size : decode(data, {}).size();
}

// `image` as tiles that share its pixels instead of copying them: the image
// lives as long as any tile does.
QList<QImage> tilesOf(QImage image) {
    if (image.height() <= kTileHeight)
        return {image};
    const auto whole = std::make_shared<const QImage>(std::move(image));
    QList<QImage> tiles;
    for (int y = 0; y < whole->height(); y += kTileHeight)
        tiles.append(QImage(
            whole->constScanLine(y), whole->width(), std::min(kTileHeight, whole->height() - y), whole->bytesPerLine(), whole->format(),
            [](void *hold) { delete static_cast<std::shared_ptr<const QImage> *>(hold); }, new std::shared_ptr<const QImage>(whole)));
    return tiles;
}

} // namespace

QImage decodeImage(const QByteArray &data) { return decode(data, {}); }

QString RemoteImageProvider::encode(const QString &url, const QString &referer) {
    return QStringLiteral("image://remote/%1~%2").arg(b64(url), b64(referer));
}

QQuickImageResponse *RemoteImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
    const auto parts = id.split(QLatin1Char('~'));
    auto state = std::make_shared<RemoteState>();
    auto cancel = std::make_shared<std::atomic_bool>(false);
    auto *response = new RemoteResponse(state, cancel);
    coverPool().start(new RemoteJob(unb64(parts.value(0)), unb64(parts.value(1)), requestedSize, state, cancel));
    return response;
}

// ---------------------------------------------------------------- Icons

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Image, ForceAsynchronousImageLoading) {}

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    static QMutex mutex;
    static QHash<QString, QByteArray> sources;
    const auto name = id.section(QLatin1Char('?'), 0, 0);
    const auto query = QUrlQuery(id.section(QLatin1Char('?'), 1));
    const auto color = query.queryItemValue(QStringLiteral("color"), QUrl::FullyDecoded);
    const auto stroke = query.queryItemValue(QStringLiteral("stroke"));
    QByteArray svg;
    {
        QMutexLocker lock(&mutex);
        if (!sources.contains(name)) {
            QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
            sources.insert(name, file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray());
        }
        svg = sources.value(name);
    }
    svg.replace("currentColor", (color.isEmpty() ? QStringLiteral("#ffffff") : color).toUtf8());
    if (!stroke.isEmpty())
        svg.replace("stroke-width=\"2\"", QStringLiteral("stroke-width=\"%1\"").arg(stroke).toUtf8());
    const QSize target = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(48, 48);
    QImage image(target, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QSvgRenderer renderer(svg);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    painter.end();
    if (size)
        *size = target;
    return image;
}

// ---------------------------------------------------------------- PanelImage

namespace {

// The page inside a scan's plain margin: the box around everything that
// differs from its corners, when they share a near-white or near-black
// colour. Pages without such a margin, or where trimming would take most
// of the page, stay whole.
QRect contentRect(const QImage &image) {
    const int w = image.width();
    const int h = image.height();
    if (w < 64 || h < 64)
        return image.rect();
    const QRgb corners[] = {image.pixel(1, 1), image.pixel(w - 2, 1), image.pixel(1, h - 2), image.pixel(w - 2, h - 2)};
    const auto close = [](QRgb a, QRgb b, int tolerance) {
        return std::abs(qRed(a) - qRed(b)) <= tolerance && std::abs(qGreen(a) - qGreen(b)) <= tolerance &&
               std::abs(qBlue(a) - qBlue(b)) <= tolerance;
    };
    const QRgb margin = corners[0];
    for (const auto corner : corners)
        if (!close(corner, margin, 24))
            return image.rect();
    const int level = qGray(margin);
    if (level > 30 && level < 225)
        return image.rect();
    // A line counts as content when more than a few of its pixels differ.
    const auto busyRow = [&](int y) {
        int count = 0;
        for (int x = 0; x < w; x += 2)
            if (!close(image.pixel(x, y), margin, 40) && ++count > w / 400 + 2)
                return true;
        return false;
    };
    const auto busyColumn = [&](int x, int top, int bottom) {
        int count = 0;
        for (int y = top; y <= bottom; y += 2)
            if (!close(image.pixel(x, y), margin, 40) && ++count > h / 400 + 2)
                return true;
        return false;
    };
    int top = 0;
    while (top < h - 1 && !busyRow(top))
        ++top;
    int bottom = h - 1;
    while (bottom > top && !busyRow(bottom))
        --bottom;
    int left = 0;
    while (left < w - 1 && !busyColumn(left, top, bottom))
        ++left;
    int right = w - 1;
    while (right > left && !busyColumn(right, top, bottom))
        --right;
    // A little air around the art.
    const int pad = std::max(2, w / 200);
    const QRect box = QRect(QPoint(left, top), QPoint(right, bottom)).adjusted(-pad, -pad, pad, pad) & image.rect();
    if (box.width() < w * 0.6 || box.height() < h * 0.6)
        return image.rect();
    return box;
}

} // namespace

// A page's tile on the GPU, drawn by resources/shaders/panel.* : as decoded
// (the sampler's filtering) or, shown larger than decoded, with Catmull-Rom.
class PanelMaterial : public QSGMaterial {
public:
    explicit PanelMaterial(QSGTexture *texture, bool blend) : texture(texture) { setFlag(Blending, blend); }

    QSGMaterialType *type() const override {
        static QSGMaterialType type;
        return &type;
    }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial *other) const override {
        const auto *o = static_cast<const PanelMaterial *>(other);
        if (o->texture != texture)
            return texture < o->texture ? -1 : 1;
        if (bicubic != o->bicubic)
            return int(bicubic) - int(o->bicubic);
        return sharpen < o->sharpen ? -1 : sharpen > o->sharpen ? 1 : 0;
    }

    QSGTexture *texture;
    bool bicubic = false;
    float sharpen = 0;
};

class PanelShader : public QSGMaterialShader {
public:
    PanelShader() {
        setShaderFileName(VertexStage, QStringLiteral(":/shaders/panel.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/shaders/panel.frag.qsb"));
    }

    // std140: mat4 qt_Matrix, float qt_Opacity, float bicubic, vec2 texSize,
    // float sharpen.
    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial, QSGMaterial *) override {
        auto *data = state.uniformData()->data();
        if (state.isMatrixDirty())
            std::memcpy(data, state.combinedMatrix().constData(), 64);
        if (state.isOpacityDirty()) {
            const float opacity = state.opacity();
            std::memcpy(data + 64, &opacity, 4);
        }
        const auto *material = static_cast<PanelMaterial *>(newMaterial);
        const auto size = material->texture->textureSize();
        const float values[4] = {material->bicubic ? 1.f : 0.f, float(size.width()), float(size.height()), material->sharpen};
        std::memcpy(data + 68, values, sizeof values);
        return true;
    }

    void updateSampledImage(RenderState &state, int, QSGTexture **texture, QSGMaterial *newMaterial, QSGMaterial *) override {
        auto *t = static_cast<PanelMaterial *>(newMaterial)->texture;
        t->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
        *texture = t;
    }
};

QSGMaterialShader *PanelMaterial::createShader(QSGRendererInterface::RenderMode) const { return new PanelShader; }

// One tile: its texture (owned), a blending material for when the page
// fades and an opaque one for the rest of the time.
class PanelTile : public QSGGeometryNode {
public:
    explicit PanelTile(QSGTexture *texture)
        : m_texture(texture), m_geometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4),
          m_blending(texture, true), m_opaque(texture, texture->hasAlphaChannel()) {
        setGeometry(&m_geometry);
        setMaterial(&m_blending);
        setOpaqueMaterial(&m_opaque);
    }

    QSGTexture *texture() const { return m_texture.get(); }

    void place(const QRectF &rect, QSGTexture::Filtering filtering, bool mipmaps, bool bicubic, float sharpen) {
        // Bicubic sampling builds on bilinear samples.
        m_texture->setFiltering(bicubic ? QSGTexture::Linear : filtering);
        m_texture->setMipmapFiltering(mipmaps ? QSGTexture::Linear : QSGTexture::None);
        if (m_blending.bicubic != bicubic || m_blending.sharpen != sharpen) {
            m_blending.bicubic = m_opaque.bicubic = bicubic;
            m_blending.sharpen = m_opaque.sharpen = sharpen;
            markDirty(DirtyMaterial);
        }
        if (rect != m_rect) {
            m_rect = rect;
            QSGGeometry::updateTexturedRectGeometry(&m_geometry, rect, m_texture->normalizedTextureSubRect());
            markDirty(DirtyGeometry);
        }
        markDirty(DirtyMaterial);
    }

private:
    std::unique_ptr<QSGTexture> m_texture;
    QSGGeometry m_geometry;
    PanelMaterial m_blending;
    PanelMaterial m_opaque;
    QRectF m_rect;
};

PanelImage::PanelImage(QQuickItem *parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

PanelImage::~PanelImage() {
    if (m_cancel)
        m_cancel->store(true);
}

// Pages bind source and referer to the same chapter result, and QML updates
// the two in no particular order. Starting on the source alone could request
// the page without its referer, which sites that protect their images answer
// with 403 (the first page in page mode stayed black), so both wait for the
// end of the update.
void PanelImage::setSource(const QString &source) {
    if (source == m_source)
        return;
    m_source = source;
    emit sourceChanged();
    if (isComponentComplete())
        startSoon();
}

void PanelImage::setReferer(const QString &referer) {
    if (referer == m_referer)
        return;
    m_referer = referer;
    emit sourceChanged();
    if (isComponentComplete() && !m_source.isEmpty() && m_status != Ready)
        startSoon();
}

void PanelImage::startSoon() {
    if (m_startQueued)
        return;
    m_startQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_startQueued = false;
            start();
        },
        Qt::QueuedConnection);
}

void PanelImage::setMaxDecodeWidth(int width) {
    if (width == m_maxDecodeWidth)
        return;
    m_maxDecodeWidth = width;
    emit sourceChanged();
    checkSize();
}

void PanelImage::setZoom(qreal zoom) {
    if (qFuzzyCompare(zoom, m_zoom))
        return;
    m_zoom = zoom;
    emit zoomChanged();
    update();
    checkSize();
}

void PanelImage::setFitIn(const QSizeF &box) {
    if (box == m_fitIn)
        return;
    m_fitIn = box;
    emit fitInChanged();
}

void PanelImage::setPreload(bool preload) {
    if (preload == m_preload)
        return;
    m_preload = preload;
    emit sourceChanged();
    if (isComponentComplete())
        startSoon();
}

void PanelImage::setCropBorders(bool crop) {
    if (crop == m_cropBorders)
        return;
    m_cropBorders = crop;
    emit sourceChanged();
    startSoon();
}

void PanelImage::setSharpen(qreal sharpen) {
    sharpen = std::clamp<qreal>(sharpen, 0, 1);
    if (qFuzzyCompare(sharpen + 1, m_sharpen + 1))
        return;
    m_sharpen = sharpen;
    emit sharpenChanged();
    update();
}

void PanelImage::setFilter(const QString &filter) {
    if (filter == m_filter)
        return;
    m_filter = filter;
    emit sourceChanged();
    startSoon();
}

void PanelImage::setHalf(const QString &half) {
    if (half == m_half)
        return;
    m_half = half;
    emit sourceChanged();
    startSoon();
}

void PanelImage::setMipmap(bool mipmap) {
    if (mipmap == m_mipmap)
        return;
    m_mipmap = mipmap;
    emit mipmapChanged();
    // Textures already uploaded are rebuilt from the decoded image.
    if (m_status == Ready)
        start(true);
}

void PanelImage::componentComplete() {
    QQuickItem::componentComplete();
    start();
}

void PanelImage::reload() { start(); }

void PanelImage::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        update();
    if (newGeometry.width() != oldGeometry.width())
        checkSize();
}

void PanelImage::itemChange(ItemChange change, const ItemChangeData &value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemDevicePixelRatioHasChanged)
        checkSize();
}

int PanelImage::targetWidth() const {
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : qApp->devicePixelRatio();
    const int shown = qCeil(width() * ratio * std::max<qreal>(1, m_zoom) - 0.01);
    // Not laid out yet: as large as allowed.
    return shown > 0 ? std::min(shown, m_maxDecodeWidth) : m_maxDecodeWidth;
}

void PanelImage::checkSize() {
    if (m_preload || m_status != Ready || m_source.isEmpty() || m_decodedFor <= 0)
        return;
    // Sharper when drawn wider than decoded (unless the picture has nothing
    // more to give); lighter, and free of shimmer, when drawn much narrower.
    const auto worth = [this] {
        const int want = targetWidth();
        return (want > m_decodedFor * 21 / 20 && !m_full) || want < m_decodedFor * 7 / 10;
    };
    if (!worth()) {
        if (m_resize)
            m_resize->stop();
        return;
    }
    // Once a resize, a zoom or a window drag has settled.
    if (!m_resize) {
        m_resize = new QTimer(this);
        m_resize->setSingleShot(true);
        m_resize->setInterval(250);
        connect(m_resize, &QTimer::timeout, this, [this, worth] {
            if (m_status == Ready && worth())
                start(true);
        });
    }
    m_resize->start();
}

void PanelImage::setStatus(Status status, const QString &error) {
    if (status == m_status && error == m_error)
        return;
    m_status = status;
    m_error = error;
    emit statusChanged();
}

void PanelImage::start(bool keep) {
    if (m_resize)
        m_resize->stop();
    if (m_cancel)
        m_cancel->store(true);
    if (!keep) {
        m_pendingTiles.clear();
        m_tilesDirty = true;
        update();
    }
    if (m_source.isEmpty()) {
        setStatus(Null);
        return;
    }
    if (!keep)
        setStatus(Loading);
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const int generation = ++m_generation;
    int target = targetWidth();
    const qreal scale = (window() ? window()->effectiveDevicePixelRatio() : qApp->devicePixelRatio()) * std::max<qreal>(1, m_zoom);
    QPointer<PanelImage> self(this);
    panelPool().start([self, generation, keep, target, scale, box = m_fitIn, maxWidth = m_maxDecodeWidth, cancel = m_cancel,
                       source = m_source, referer = m_referer, preload = m_preload, zoomed = m_zoom > 1.01, crop = m_cropBorders,
                       filter = m_filter, half = m_half]() mutable {
        CancelScope scope(cancel);
        QList<QImage> tiles;
        QSize size;
        QString error;
        bool full = true;
        try {
            const auto data = ImageCache::fetch(source, referer);
            if (cancel->load())
                return;
            if (preload) {
                size = measure(data);
            } else {
                // The width a picture of `shape` is drawn at: fitted into
                // the box, or the item's own.
                const auto fitted = [&](const QSize &shape) {
                    if (box.isEmpty() || shape.isEmpty())
                        return target;
                    const double across = std::min(box.width(), box.height() * shape.width() / shape.height());
                    return std::clamp(qCeil(across * scale - 0.01), 1, maxWidth);
                };
                // Half of a spread decodes at twice the width, so each half
                // is as sharp as a single page; a page to trim a little
                // wider, as its margin goes.
                auto image = decode(data, {}, 0, &full, [&](const QSize &size) {
                    const int across = half.isEmpty() ? fitted(size) : fitted(QSize(size.width() / 2, size.height())) * 2;
                    return crop ? across * 5 / 4 : across;
                });
                if (crop)
                    image = image.copy(contentRect(image));
                if (half == QLatin1String("left"))
                    image = image.copy(0, 0, image.width() / 2, image.height());
                else if (half == QLatin1String("right"))
                    image = image.copy(image.width() / 2, 0, image.width() - image.width() / 2, image.height());
                // As many pixels across as it is drawn with.
                target = fitted(image.size());
                if (image.width() > target)
                    image = image.scaledToWidth(target, Qt::SmoothTransformation);
                if (filter.contains(QLatin1String("grey")))
                    image = image.convertToFormat(QImage::Format_Grayscale8);
                if (filter.contains(QLatin1String("invert"))) {
                    if (image.format() != QImage::Format_Grayscale8)
                        image.convertTo(QImage::Format_RGB32);
                    image.invertPixels();
                }
                size = image.size();
                // In place where it can be (a decoded JPEG is), not a copy.
                image.convertTo(image.hasAlphaChannel() ? QImage::Format_RGBA8888_Premultiplied : QImage::Format_RGBX8888);
                tiles = tilesOf(std::move(image));
            }
        } catch (const sc::Error &e) {
            error = e.message();
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        if (cancel->load())
            return;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, generation, keep, target, full, zoomed, tiles, size, error] {
                if (!self || self->m_generation != generation)
                    return;
                if (!error.isEmpty()) {
                    // A sharper copy that failed leaves the one on screen.
                    if (!keep)
                        self->setStatus(Error, error);
                    return;
                }
                self->m_size = size;
                self->m_decodedFor = target;
                self->m_full = full;
                self->m_zoomedTiles = zoomed;
                self->m_pendingTiles = tiles;
                self->m_tilesDirty = !tiles.isEmpty() || !keep;
                emit self->loaded();
                self->setStatus(Ready);
                self->update();
                // The drawn size may have moved on while this decoded.
                self->checkSize();
            },
            Qt::QueuedConnection);
    });
}

QSGNode *PanelImage::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    auto *root = old ? old : new QSGNode;
    if (m_tilesDirty) {
        while (auto *child = root->firstChild()) {
            root->removeChildNode(child);
            delete child;
        }
        const bool mipmaps = m_mipmap || m_zoomedTiles;
        for (const auto &tile : std::as_const(m_pendingTiles))
            root->appendChildNode(new PanelTile(window()->createTextureFromImage(
                tile, mipmaps ? QQuickWindow::TextureHasMipmaps : QQuickWindow::CreateTextureOptions())));
        m_textureMipmaps = mipmaps;
        // Textures now live on the GPU; drop the CPU copies.
        m_pendingTiles.clear();
        m_tilesDirty = false;
    }
    if (m_size.isEmpty() || !root->childCount())
        return root;
    const double scale = width() / m_size.width();
    // Drawn pixel for pixel (decoded for this size, not zoomed past it), each
    // screen pixel takes one picture pixel: smoothing would blur a page that
    // sits between two screen pixels by half a pixel, as pages in a
    // scrolling strip mostly do. Larger than decoded (a picture smaller than
    // the strip, or zoomed in before the sharper one is there): Catmull-Rom.
    // Smaller: smoothed, with mipmaps if it has them.
    const auto texWidth = static_cast<PanelTile *>(root->firstChild())->texture()->textureSize().width();
    const double shown = width() * window()->effectiveDevicePixelRatio() * std::max<qreal>(1, m_zoom);
    const bool exact = std::abs(shown - texWidth) < 1;
    const bool larger = !exact && shown > texWidth;
    double y = 0;
    for (auto *child = root->firstChild(); child; child = child->nextSibling()) {
        auto *tile = static_cast<PanelTile *>(child);
        const auto texSize = tile->texture()->textureSize();
        const double h = texSize.height() * (m_size.width() / double(texSize.width())) * scale;
        tile->place(QRectF(0, y, width(), h), exact ? QSGTexture::Nearest : QSGTexture::Linear, m_textureMipmaps && !exact && !larger,
                    larger, float(m_sharpen));
        y += h;
    }
    return root;
}

} // namespace sc
