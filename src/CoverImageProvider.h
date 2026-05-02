#pragma once

#include <QCache>
#include <QMutex>
#include <QQuickImageProvider>

class CoverImageProvider : public QQuickImageProvider {
public:
    CoverImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    void setMaxEdge(int edge);
    void setSourceBudgetKb(int kb);
    void setScaledBudgetKb(int kb);

private:
    struct Entry {
        QImage image;
        int    kb = 0;
    };

    QCache<QString, Entry> m_sources;
    QCache<QString, Entry> m_scaled;
    QMutex m_mutex;
    int    m_maxEdge        = 384;
    int    m_sourceBudgetKb = 48 * 1024;
    int    m_scaledBudgetKb = 16 * 1024;
};
