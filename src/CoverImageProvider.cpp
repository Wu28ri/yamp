#include "CoverImageProvider.h"
#include "CoverExtractor.h"

#include <QFileInfo>
#include <QMutexLocker>
#include <QUrl>

namespace {
constexpr int kSourceBudgetKb = 512 * 1024;
constexpr int kScaledBudgetKb = 256 * 1024;
constexpr int kSourceMaxEdge  = 1024;

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
    m_sources.setMaxCost(kSourceBudgetKb);
    m_scaled.setMaxCost(kScaledBudgetKb);
}

QImage CoverImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    QString path = QUrl::fromPercentEncoding(id.toUtf8());
    if (!path.startsWith(QLatin1Char('/'))) path.prepend(QLatin1Char('/'));

    const QString sig       = fileSignature(path);
    const QString sourceKey = path + QLatin1Char('?') + sig;
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

    if (source.isNull()) {
        source = CoverExtractor::loadCover(path);
        if (source.isNull()) {
            source = QImage(1, 1, QImage::Format_RGBA8888);
            source.fill(Qt::transparent);
        } else if (source.width() > kSourceMaxEdge || source.height() > kSourceMaxEdge) {
            source = source.scaled(kSourceMaxEdge, kSourceMaxEdge,
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        const int kb = qMax(1, static_cast<int>(source.sizeInBytes() / 1024));
        QMutexLocker locker(&m_mutex);
        m_sources.insert(sourceKey, new Entry{source, kb}, kb);
    }

    QImage out = source;
    if (reqW > 0 && reqH > 0 && (out.width() != reqW || out.height() != reqH)) {
        out = source.scaled(reqW, reqH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    if (size) *size = out.size();

    const int kb = qMax(1, static_cast<int>(out.sizeInBytes() / 1024));
    {
        QMutexLocker locker(&m_mutex);
        m_scaled.insert(scaledKey, new Entry{out, kb}, kb);
    }
    return out;
}
