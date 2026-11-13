#pragma once

#include <QByteArray>
#include <QCache>
#include <QHash>
#include <QImage>
#include <QReadWriteLock>
#include <QQuickAsyncImageProvider>
#include <QSize>
#include <QString>
#include <QThreadPool>

class CoverImageProvider : public QQuickAsyncImageProvider {
public:
    CoverImageProvider();
    ~CoverImageProvider() override;

    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
    QImage loadCoverImage(const QString &id, QSize *size, const QSize &requestedSize);

    void setMaxEdge(int edge);
    void setSourceBudgetKb(int kb);
    void setScaledBudgetKb(int kb);
    void clearCache();

private:
    struct ScaledEntry {
        QImage image;
        QSize  sourceSize;
    };

    QHash<QString, QByteArray>      m_pathToHash;

    QHash<QString, QByteArray>      m_dirToHash;
    QCache<QByteArray, QImage> m_sources;
    QCache<QByteArray, ScaledEntry> m_scaled;

    QReadWriteLock m_lock;
    QThreadPool    m_pool;

    int    m_maxEdge        = 512;
    int    m_sourceBudgetKb = 48 * 1024;
    int    m_scaledBudgetKb = 16 * 1024;
};
