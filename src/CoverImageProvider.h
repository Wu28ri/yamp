#pragma once

#include <QCache>
#include <QMutex>
#include <QQuickImageProvider>

class CoverImageProvider : public QQuickImageProvider {
public:
    CoverImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    struct Entry {
        QImage image;
        int    kb = 0;
    };

    QCache<QString, Entry> m_sources;
    QCache<QString, Entry> m_scaled;
    QMutex m_mutex;
};
