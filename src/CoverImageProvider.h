#pragma once

#include <QByteArray>
#include <QCache>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QSize>

class CoverImageProvider : public QQuickImageProvider {
public:
    CoverImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    void setMaxEdge(int edge);
    void setSourceBudgetKb(int kb);
    void setScaledBudgetKb(int kb);

private:
    struct SourceEntry {
        QImage image;
        int    kb = 0;
    };
    struct ScaledEntry {
        QImage image;
        QSize  sourceSize;
        int    kb = 0;
    };

    QHash<QString, QByteArray>     m_pathToHash;
    QCache<QByteArray, SourceEntry> m_sources;
    QCache<QByteArray, ScaledEntry> m_scaled;
    QMutex m_mutex;
    int    m_maxEdge        = 384;
    int    m_sourceBudgetKb = 48 * 1024;
    int    m_scaledBudgetKb = 16 * 1024;
};
