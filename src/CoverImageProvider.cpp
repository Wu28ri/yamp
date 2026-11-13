#include "CoverImageProvider.h"
#include "CoverExtractor.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QReadLocker>
#include <QRunnable>
#include <QUrl>
#include <QWriteLocker>
#include <atomic>

namespace {

class AsyncCoverResponse : public QQuickImageResponse, public QRunnable {
public:
    AsyncCoverResponse(CoverImageProvider *provider, QString id, QSize requestedSize)
        : m_provider(provider), m_id(std::move(id)), m_requestedSize(requestedSize) {
        setAutoDelete(false);
    }

    QQuickTextureFactory *textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    void cancel() override {
        m_cancelled.store(true, std::memory_order_relaxed);
    }

    void run() override {
        if (!m_cancelled.load(std::memory_order_relaxed) && m_provider) {
            QSize sourceSize;
            m_image = m_provider->loadCoverImage(m_id, &sourceSize, m_requestedSize);
        }
        emit finished();
    }

private:
    CoverImageProvider *m_provider = nullptr;
    QString m_id;
    QSize m_requestedSize;
    QImage m_image;
    std::atomic<bool> m_cancelled{false};
};

QString fileSignature(const QFileInfo &info) {
    if (!info.exists()) return QStringLiteral("missing");
    return QString::number(info.lastModified().toMSecsSinceEpoch())
         + QLatin1Char(':')
         + QString::number(info.size());
}

QString dirSignature(const QString &dirPath) {
    const QFileInfo info(dirPath);
    if (!info.exists()) return QStringLiteral("missing");

    return QString::number(info.lastModified().toMSecsSinceEpoch());
}

const QByteArray kPlaceholderHash = QByteArrayLiteral("__placeholder__");

constexpr int kMaxHashEntries = 65536;

template <typename Hash>
void capHash(Hash &hash) {
    if (hash.size() > kMaxHashEntries) hash.clear();
}

QByteArray makeSourceKey(const QByteArray &contentHash, int maxEdge) {
    QByteArray key;
    key.reserve(contentHash.size() + 8);
    key.append(contentHash);
    key.append('|');
    key.append(QByteArray::number(maxEdge));
    return key;
}

QByteArray makeScaledKey(const QByteArray &sourceKey, int w, int h) {
    QByteArray key;
    key.reserve(sourceKey.size() + 16);
    key.append(sourceKey);
    key.append('@');
    key.append(QByteArray::number(w));
    key.append('x');
    key.append(QByteArray::number(h));
    return key;
}

QImage makePlaceholder() {
    QImage img(1, 1, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    return img;
}

QByteArray hashBytes(const QByteArray &bytes) {
    const QByteArray digest = QCryptographicHash::hash(bytes, QCryptographicHash::Md5);
    QByteArray tagged;
    tagged.reserve(1 + digest.size());
    tagged.append('h');
    tagged.append(digest);
    return tagged;
}

QImage decodeCapped(const QByteArray &bytes, int maxEdge) {
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return {};

    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QSize origSize = reader.size();
    if (origSize.isValid() && maxEdge > 0 && (origSize.width() > maxEdge || origSize.height() > maxEdge)) {
        reader.setScaledSize(origSize.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio));
    }
    QImage img = reader.read();
    if (img.isNull()) {
        img.loadFromData(bytes);
        if (!img.isNull() && maxEdge > 0 && (img.width() > maxEdge || img.height() > maxEdge)) {
            img = img.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }
    return img;
}

QImage scaleForRequest(const QImage &src, int reqW, int reqH) {
    if (src.isNull() || reqW <= 0 || reqH <= 0) return src;
    if (src.width() == reqW && src.height() == reqH) return src;

    if (reqW == reqH) {
        QImage expanded = src.scaled(reqW, reqH, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int x = qMax(0, (expanded.width() - reqW) / 2);
        const int y = qMax(0, (expanded.height() - reqH) / 2);
        return expanded.copy(x, y, reqW, reqH);
    }
    return src.scaled(reqW, reqH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage roundCorners(const QImage &src) {
    if (src.isNull()) return src;
    const int w = src.width();
    const int h = src.height();
    const qreal cornerRadius = qMin(w, h) * 0.05;
    if (cornerRadius < 1.0) return src;

    QImage dst(w, h, QImage::Format_ARGB32_Premultiplied);
    dst.fill(Qt::transparent);

    QPainter painter(&dst);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, w, h), cornerRadius, cornerRadius);
    painter.setClipPath(path);
    painter.drawImage(0, 0, src);
    painter.end();

    return dst;
}

int imageKb(const QImage &img) {
    return qMax(1, static_cast<int>(img.sizeInBytes() / 1024));
}

}

CoverImageProvider::CoverImageProvider() {
    m_sources.setMaxCost(m_sourceBudgetKb);
    m_scaled.setMaxCost(m_scaledBudgetKb);
    m_pool.setMaxThreadCount(4);
}

CoverImageProvider::~CoverImageProvider() {
    m_pool.clear();
    m_pool.waitForDone();
}

QQuickImageResponse *CoverImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
    auto *response = new AsyncCoverResponse(this, id, requestedSize);
    m_pool.start(response);
    return response;
}

void CoverImageProvider::clearCache() {
    QWriteLocker locker(&m_lock);
    m_pathToHash.clear();
    m_dirToHash.clear();
    m_sources.clear();
    m_scaled.clear();
}

void CoverImageProvider::setMaxEdge(int edge) {
    if (edge <= 0) return;
    QWriteLocker locker(&m_lock);
    if (m_maxEdge == edge) return;
    m_maxEdge = edge;
    m_pathToHash.clear();
    m_dirToHash.clear();
    m_sources.clear();
    m_scaled.clear();
}

void CoverImageProvider::setSourceBudgetKb(int kb) {
    if (kb <= 0) return;
    QWriteLocker locker(&m_lock);
    if (m_sourceBudgetKb == kb) return;
    m_sourceBudgetKb = kb;
    m_sources.setMaxCost(kb);
}

void CoverImageProvider::setScaledBudgetKb(int kb) {
    if (kb <= 0) return;
    QWriteLocker locker(&m_lock);
    if (m_scaledBudgetKb == kb) return;
    m_scaledBudgetKb = kb;
    m_scaled.setMaxCost(kb);
}

QImage CoverImageProvider::loadCoverImage(const QString &id, QSize *size, const QSize &requestedSize) {
    QString path = QUrl::fromPercentEncoding(id.toUtf8());
    if (!path.startsWith(QLatin1Char('/'))) path.prepend(QLatin1Char('/'));

    int maxEdge = 0;
    {
        QReadLocker locker(&m_lock);
        maxEdge = m_maxEdge;
    }

    const QFileInfo fileInfo(path);
    const QString   pathKey = path + QLatin1Char('?') + fileSignature(fileInfo);
    const QString   dirPath = fileInfo.absolutePath();
    const QString   dirKey  = dirPath + QLatin1Char('?') + dirSignature(dirPath);

    const int reqW = requestedSize.width();
    const int reqH = requestedSize.height();

    QByteArray contentHash;
    bool       fromDirHash   = false;
    {
        QReadLocker locker(&m_lock);
        contentHash = m_pathToHash.value(pathKey);
        if (contentHash.isEmpty()) {
            contentHash = m_dirToHash.value(dirKey);
            fromDirHash = !contentHash.isEmpty();
        }
    }

    if (contentHash == kPlaceholderHash) {
        QImage img = makePlaceholder();
        if (size) *size = img.size();
        return img;
    }

    if (!contentHash.isEmpty()) {
        const QByteArray sourceKey = makeSourceKey(contentHash, maxEdge);
        const QByteArray scaledKey = makeScaledKey(sourceKey, reqW, reqH);

        QImage cachedScaled;
        QSize  cachedSourceSize;
        QImage cachedSource;
        {
            QWriteLocker locker(&m_lock);
            if (auto *entry = m_scaled.object(scaledKey)) {
                cachedScaled     = entry->image;
                cachedSourceSize = entry->sourceSize;
            } else if (auto *entry = m_sources.object(sourceKey)) {
                cachedSource     = *entry;
                cachedSourceSize = entry->size();
            }
        }

        if (!cachedScaled.isNull()) {
            if (size) *size = cachedSourceSize;

            if (fromDirHash) {
                QWriteLocker locker(&m_lock);
                m_pathToHash.insert(pathKey, contentHash);
            }
            return cachedScaled;
        }

        if (!cachedSource.isNull()) {
            QImage out = roundCorners(scaleForRequest(cachedSource, reqW, reqH));
            {
                QWriteLocker locker(&m_lock);
                m_scaled.insert(scaledKey,
                                new ScaledEntry{out, cachedSourceSize},
                                qMax(imageKb(out), 16));
                if (fromDirHash) m_pathToHash.insert(pathKey, contentHash);
            }
            if (size) *size = cachedSourceSize;
            return out;
        }
    }

    CoverExtractor::RawCoverData raw = CoverExtractor::loadRawCover(path);
    if (raw.bytes.isEmpty()) {
        QWriteLocker locker(&m_lock);
        capHash(m_pathToHash);
        m_pathToHash.insert(pathKey, kPlaceholderHash);

        QImage img = makePlaceholder();
        if (size) *size = img.size();
        return img;
    }

    contentHash = hashBytes(raw.bytes);
    const QByteArray sourceKey = makeSourceKey(contentHash, maxEdge);

    QImage source;
    {
        QWriteLocker locker(&m_lock);
        if (auto *entry = m_sources.object(sourceKey)) {
            source = *entry;
        }
    }

    const bool cacheSource = source.isNull();
    if (cacheSource) {
        source = decodeCapped(raw.bytes, maxEdge);
        if (source.isNull()) {
            QWriteLocker locker(&m_lock);
            capHash(m_pathToHash);
            m_pathToHash.insert(pathKey, kPlaceholderHash);

            QImage img = makePlaceholder();
            if (size) *size = img.size();
            return img;
        }
    }

    const bool shareAcrossDir = (raw.source == CoverExtractor::Source::Sidecar);
    {
        QWriteLocker locker(&m_lock);
        if (cacheSource)
            m_sources.insert(sourceKey, new QImage(source), qMax(imageKb(source), 16));
        capHash(m_pathToHash);
        capHash(m_dirToHash);
        m_pathToHash.insert(pathKey, contentHash);

        if (shareAcrossDir) m_dirToHash.insert(dirKey, contentHash);
    }

    const QSize sourceSize = source.size();
    QImage out = roundCorners(scaleForRequest(source, reqW, reqH));
    if (size) *size = sourceSize;

    {
        const QByteArray scaledKey = makeScaledKey(sourceKey, reqW, reqH);
        QWriteLocker locker(&m_lock);
        m_scaled.insert(scaledKey,
                        new ScaledEntry{out, sourceSize},
                        qMax(imageKb(out), 16));
    }
    return out;
}
