#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QUrl>

class Settings;
class QNetworkAccessManager;

class ArtistImageProvider : public QQuickImageProvider {
public:
    ArtistImageProvider();
    ~ArtistImageProvider() override;

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    void insertImage(const QString &key, const QImage &image);

private:
    QImage loadFromDatabaseLocked(const QString &key);

    QMutex                   m_mutex;
    QCache<QString, QImage>  m_cache;
    QSqlDatabase             m_db;
    QString                  m_connName;
};

class ArtistImageLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit ArtistImageLoader(Settings *settings,
                               ArtistImageProvider *provider,
                               QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    Q_INVOKABLE QString artistImageUrl(const QString &artist);

signals:
    void enabledChanged();
    void artistImageReady(const QString &artist, const QString &imageUrl);

private:
    void loadIndexFromDatabase();
    void saveImageToDatabase(const QString &key, const QByteArray &pngBytes);
    void saveNegativeToDatabase(const QString &key);

    void processQueue();
    void fetchFromLastFmApi(const QString &key, const QString &artist);
    void fetchFromDeezer(const QString &key, const QString &artist);
    void downloadArtistImage(const QString &key,
                             const QString &artist,
                             const QUrl &imageUrl,
                             bool allowDeezerFallback);
    void finishFetch(const QString &key, const QString &providerUrl);

    Settings              *m_settings = nullptr;
    ArtistImageProvider   *m_provider = nullptr;
    QNetworkAccessManager *m_nam      = nullptr;
    bool                   m_enabled  = false;

    QHash<QString, QString>     m_cache;
    QHash<QString, QStringList> m_requestedNames;
    QStringList                 m_queue;
    QSet<QString>               m_inFlight;
    QSet<QString>               m_failed;
};
