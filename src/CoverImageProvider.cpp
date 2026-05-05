#include "CoverImageProvider.h"
#include "CoverExtractor.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QMutexLocker>
#include <QUrl>

namespace {

QString fileSignature(const QString &path) {
    QFileInfo info(path);
    if (!info.exists()) return QStringLiteral("missing");
    return QString::number(info.lastModified().toMSecsSinceEpoch())
         + QLatin1Char(':')
         + QString::number(info.size());
}

const QByteArray kPlaceholderHash = QByteArrayLiteral("__placeholder__");

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

QByteArray hashCover(const CoverExtractor::CoverData &cover) {
    QByteArray digest;
    if (!cover.bytes.isEmpty()) {
        digest = QCryptographicHash::hash(cover.bytes, QCryptographicHash::Md5);
    } else {
        QCryptographicHash h(QCryptographicHash::Md5);
        h.addData(QByteArrayView(reinterpret_cast<const char *>(cover.image.constBits()),
                                 cover.image.sizeInBytes()));
        digest = h.result();
    }
    QByteArray tagged;
    tagged.reserve(1 + digest.size());
    tagged.append('h');
    tagged.append(digest);
    return tagged;
}

QImage downscaleIfNeeded(const QImage &src, int maxEdge) {
    if (src.width() > maxEdge || src.height() > maxEdge) {
        return src.scaled(maxEdge, maxEdge,
                          Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return src;
}

int imageKb(const QImage &img) {
    return qMax(1, static_cast<int>(img.sizeInBytes() / 1024));
}

}

CoverImageProvider::CoverImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {
    m_sources.setMaxCost(m_sourceBudgetKb);
    m_scaled.setMaxCost(m_scaledBudgetKb);
}

void CoverImageProvider::setMaxEdge(int edge) {
    if (edge <= 0) return;
    QMutexLocker locker(&m_mutex);
    if (m_maxEdge == edge) return;
    m_maxEdge = edge;
    m_pathToHash.clear();
    m_sources.clear();
    m_scaled.clear();
}

void CoverImageProvider::setSourceBudgetKb(int kb) {
    if (kb <= 0) return;
    QMutexLocker locker(&m_mutex);
    if (m_sourceBudgetKb == kb) return;
    m_sourceBudgetKb = kb;
    m_sources.setMaxCost(kb);
}

void CoverImageProvider::setScaledBudgetKb(int kb) {
    if (kb <= 0) return;
    QMutexLocker locker(&m_mutex);
    if (m_scaledBudgetKb == kb) return;
    m_scaledBudgetKb = kb;
    m_scaled.setMaxCost(kb);
}

QImage CoverImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    QString path = QUrl::fromPercentEncoding(id.toUtf8());
    if (!path.startsWith(QLatin1Char('/'))) path.prepend(QLatin1Char('/'));

    int maxEdge = 0;
    {
        QMutexLocker locker(&m_mutex);
        maxEdge = m_maxEdge;
    }

    const QString pathKey = path + QLatin1Char('?') + fileSignature(path);
    const int     reqW    = requestedSize.width();
    const int     reqH    = requestedSize.height();

    QByteArray contentHash;
    bool       haveHash      = false;
    bool       isPlaceholder = false;
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_pathToHash.constFind(pathKey);
        if (it != m_pathToHash.constEnd()) {
            contentHash   = it.value();
            haveHash      = true;
            isPlaceholder = (contentHash == kPlaceholderHash);
        }
    }

    if (haveHash && isPlaceholder) {
        QImage img = makePlaceholder();
        if (size) *size = img.size();
        return img;
    }

    if (haveHash) {
        const QByteArray sourceKey = makeSourceKey(contentHash, maxEdge);
        const QByteArray scaledKey = makeScaledKey(sourceKey, reqW, reqH);

        QMutexLocker locker(&m_mutex);
        if (auto *entry = m_scaled.object(scaledKey)) {
            if (size) *size = entry->sourceSize;
            return entry->image;
        }
        if (auto *entry = m_sources.object(sourceKey)) {
            const QImage source     = entry->image;
            const QSize  sourceSize = source.size();
            QImage out = source;
            if (reqW > 0 && reqH > 0 && (out.width() != reqW || out.height() != reqH)) {
                out = source.scaled(reqW, reqH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            m_scaled.insert(scaledKey,
                            new ScaledEntry{out, sourceSize, imageKb(out)},
                            qMax(imageKb(out), 16));
            if (size) *size = sourceSize;
            return out;
        }
    }

    CoverExtractor::CoverData cover = CoverExtractor::loadCoverWithBytes(path);

    if (cover.image.isNull()) {
        QMutexLocker locker(&m_mutex);
        m_pathToHash.insert(pathKey, kPlaceholderHash);
        QImage img = makePlaceholder();
        if (size) *size = img.size();
        return img;
    }

    contentHash = hashCover(cover);
    const QByteArray sourceKey = makeSourceKey(contentHash, maxEdge);

    QImage source;
    {
        QMutexLocker locker(&m_mutex);
        if (auto *entry = m_sources.object(sourceKey)) {
            source = entry->image;
        }
    }

    if (source.isNull()) {
        source = downscaleIfNeeded(cover.image, maxEdge);
        QMutexLocker locker(&m_mutex);
        m_sources.insert(sourceKey,
                         new SourceEntry{source, imageKb(source)},
                         qMax(imageKb(source), 16));
        m_pathToHash.insert(pathKey, contentHash);
    } else {
        QMutexLocker locker(&m_mutex);
        m_pathToHash.insert(pathKey, contentHash);
    }

    const QSize sourceSize = source.size();
    QImage out = source;
    if (reqW > 0 && reqH > 0 && (out.width() != reqW || out.height() != reqH)) {
        out = source.scaled(reqW, reqH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (size) *size = sourceSize;

    {
        const QByteArray scaledKey = makeScaledKey(sourceKey, reqW, reqH);
        QMutexLocker locker(&m_mutex);
        m_scaled.insert(scaledKey,
                        new ScaledEntry{out, sourceSize, imageKb(out)},
                        qMax(imageKb(out), 16));
    }
    return out;
}
