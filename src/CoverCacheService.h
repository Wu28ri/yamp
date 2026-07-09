#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QThreadPool>

class CoverCacheService : public QObject {
    Q_OBJECT
public:
    explicit CoverCacheService(QObject *parent = nullptr);
    ~CoverCacheService() override;

    void requestCoverFor(const QString &trackPath, quint64 generation);

signals:
    void coverResolved(const QString &trackPath, const QString &coverPath, quint64 generation);

private:
    static QString cacheDir();
    static QString pathForHash(const QByteArray &hash, const QString &ext);
    static bool    writeAtomic(const QString &targetPath, const QByteArray &data);
    static void    pruneTo(int keepCount);

    QThreadPool m_pool;
};
