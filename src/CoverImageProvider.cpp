#include "CoverImageProvider.h"
#include "CoverExtractor.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QReadLocker>
#include <QUrl>
#include <QWriteLocker>

namespace {

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

QImage CoverImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
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
    bool       haveHash      = false;
    bool       isPlaceholder = false;
    bool       fromDirHash   = false;
    {
        QReadLocker locker(&m_lock);
        auto it = m_pathToHash.constFind(pathKey);
        if (it != m_pathToHash.constEnd()) {
            contentHash   = it.value();
            haveHash      = true;
            isPlaceholder = (contentHash == kPlaceholderHash);
        } else {
            auto dit = m_dirToHash.constFind(dirKey);
            if (dit != m_dirToHash.constEnd()) {
                contentHash   = dit.value();
                haveHash      = true;
                fromDirHash   = true;
                isPlaceholder = (contentHash == kPlaceholderHash);
            }
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

        QImage cachedScaled;
        QSize  cachedSourceSize;
        QImage cachedSource;
        {
            QReadLocker locker(&m_lock);
            if (auto *entry = m_scaled.object(scaledKey)) {
                cachedScaled     = entry->image;
                cachedSourceSize = entry->sourceSize;
            } else if (auto *entry = m_sources.object(sourceKey)) {
                cachedSource     = entry->image;
                cachedSourceSize = entry->image.size();
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
            QImage out = cachedSource;
            if (reqW > 0 && reqH > 0 && (out.width() != reqW || out.height() != reqH)) {
                out = cachedSource.scaled(reqW, reqH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            {
                QWriteLocker locker(&m_lock);
                m_scaled.insert(scaledKey,
                                new ScaledEntry{out, cachedSourceSize, imageKb(out)},
                                qMax(imageKb(out), 16));
                if (fromDirHash) m_pathToHash.insert(pathKey, contentHash);
            }
            if (size) *size = cachedSourceSize;
            return out;
        }

    }

    CoverExtractor::CoverData cover = CoverExtractor::loadCoverWithBytes(path);

    if (cover.image.isNull()) {
        QWriteLocker locker(&m_lock);
        m_pathToHash.insert(pathKey, kPlaceholderHash);

        QImage img = makePlaceholder();
        if (size) *size = img.size();
        return img;
    }

    contentHash = hashCover(cover);
    const QByteArray sourceKey = makeSourceKey(contentHash, maxEdge);

    QImage source;
    {
        QReadLocker locker(&m_lock);
        if (auto *entry = m_sources.object(sourceKey)) {
            source = entry->image;
        }
    }

    if (source.isNull()) {
        source = downscaleIfNeeded(cover.image, maxEdge);
        QWriteLocker locker(&m_lock);
        m_sources.insert(sourceKey,
                         new SourceEntry{source, imageKb(source)},
                         qMax(imageKb(source), 16));
        m_pathToHash.insert(pathKey, contentHash);

        m_dirToHash.insert(dirKey, contentHash);
    } else {
        QWriteLocker locker(&m_lock);
        m_pathToHash.insert(pathKey, contentHash);
        m_dirToHash.insert(dirKey, contentHash);
    }

    const QSize sourceSize = source.size();
    QImage out = source;
    if (reqW > 0 && reqH > 0 && (out.width() != reqW || out.height() != reqH)) {
        out = source.scaled(reqW, reqH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (size) *size = sourceSize;

    {
        const QByteArray scaledKey = makeScaledKey(sourceKey, reqW, reqH);
        QWriteLocker locker(&m_lock);
        m_scaled.insert(scaledKey,
                        new ScaledEntry{out, sourceSize, imageKb(out)},
                        qMax(imageKb(out), 16));
    }
    return out;
}

