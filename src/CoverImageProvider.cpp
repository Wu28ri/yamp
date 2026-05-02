#include "CoverImageProvider.h"
#include "CoverExtractor.h"

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

    const QString sig       = fileSignature(path);
    const QString sourceKey = path + QLatin1Char('?') + sig
                              + QLatin1Char('|') + QString::number(maxEdge);
    const int reqW = requestedSize.width();
    const int reqH = requestedSize.height();
    const QString scaledKey = sourceKey + QLatin1Char('@') + QString::number(reqW);

    {
        QMutexLocker locker(&m_mutex);
        if (auto *entry = m_scaled.object(scaledKey)) {
            if (size) *size = entry->image.size();
            return entry->image;
        }
    }

    QImage source;
    {
        QMutexLocker locker(&m_mutex);
        if (auto *entry = m_sources.object(sourceKey)) source = entry->image;
    }

    bool isPlaceholder = false;
    if (source.isNull()) {
        source = CoverExtractor::loadCover(path);
        if (source.isNull()) {
            source = QImage(1, 1, QImage::Format_RGBA8888);
            source.fill(Qt::transparent);
            isPlaceholder = true;
        } else if (source.width() > maxEdge || source.height() > maxEdge) {
            source = source.scaled(maxEdge, maxEdge,
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        const int kb = qMax(1, static_cast<int>(source.sizeInBytes() / 1024));
        QMutexLocker locker(&m_mutex);
        m_sources.insert(sourceKey, new Entry{source, kb}, kb);
    } else {
        isPlaceholder = (source.width() == 1 && source.height() == 1);
    }

    QImage out = source;
    if (!isPlaceholder && reqW > 0 && reqH > 0 && (out.width() != reqW || out.height() != reqH)) {
        out = source.scaled(reqW, reqH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    if (size) *size = out.size();

    if (!isPlaceholder) {
        const int kb = qMax(1, static_cast<int>(out.sizeInBytes() / 1024));
        QMutexLocker locker(&m_mutex);
        m_scaled.insert(scaledKey, new Entry{out, kb}, kb);
    }
    return out;
}
