#include "ArtistImageLoader.h"
#include "LibraryDb.h"
#include "Settings.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QVariant>

namespace {

#ifndef YAMP_VERSION
#define YAMP_VERSION "0.2"
#endif

constexpr const char *kDeezerSearchUrl = "https://api.deezer.com/search/artist";
constexpr const char *kUserAgent       = "YAMP/" YAMP_VERSION " (https://github.com/Wu28ri/YAMP)";

constexpr int    kRequestTimeoutMs    = 8000;
constexpr int    kMaxConcurrentFetch  = 6;
constexpr int    kMaxQueueSize        = 96;
constexpr int    kMaxDbWriteRetries   = 20;
constexpr int    kAvatarMaxEdge       = 384;
constexpr int    kProviderCacheKb     = 16 * 1024;
constexpr qint64 kNegativeCacheTtlSec = 7 * 24 * 3600;

void armHardTimeout(QNetworkReply *reply, int timeoutMs = kRequestTimeoutMs) {
    if (!reply) return;
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    timer->setInterval(timeoutMs);
    QObject::connect(timer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, timer, &QTimer::stop);
    timer->start();
}

int imageCostKb(const QImage &img) {
    return qMax(1, static_cast<int>(img.sizeInBytes() / 1024));
}

QImage makePlaceholder() {
    QImage img(1, 1, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    return img;
}

QString makeProviderUrl(const QString &key) {
    return QStringLiteral("image://artist/") +
           QString::fromLatin1(QUrl::toPercentEncoding(key));
}

bool isValidDeezerPicture(const QString &url) {
    if (url.isEmpty()) return false;
    return !url.contains(QLatin1String("/images/artist//"), Qt::CaseInsensitive) &&
           !url.contains(QLatin1String("/images/cover/"), Qt::CaseInsensitive);
}

QImage makeSquareAvatar(const QImage &src, int targetEdge) {
    if (src.isNull() || targetEdge <= 0) return {};
    const int minSide = qMin(src.width(), src.height());
    if (minSide <= 0) return {};

    const QRect cropRect((src.width() - minSide) / 2,
                         (src.height() - minSide) / 2,
                         minSide, minSide);
    const int edge = qMin(minSide, targetEdge);
    const QImage square = src.copy(cropRect)
                              .scaled(edge, edge, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QImage dst(edge, edge, QImage::Format_ARGB32_Premultiplied);
    dst.fill(Qt::transparent);

    const qreal cornerRadius = edge * 0.05;
    QPainter painter(&dst);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, edge, edge), cornerRadius, cornerRadius);
    painter.setClipPath(path);
    painter.drawImage(0, 0, square);
    painter.end();

    return dst;
}

} // namespace

ArtistImageProvider::ArtistImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image),
      m_cache(kProviderCacheKb) {
}

ArtistImageProvider::~ArtistImageProvider() {
    QMutexLocker locker(&m_mutex);
    LibraryDb::closeScopedConnection(m_connName, m_db);
}

void ArtistImageProvider::insertImage(const QString &key, const QImage &image) {
    if (key.isEmpty() || image.isNull()) return;
    QMutexLocker locker(&m_mutex);
    m_cache.insert(key, new QImage(image), imageCostKb(image));
}

void ArtistImageProvider::clearCache() {
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
}

QImage ArtistImageProvider::loadFromDatabaseLocked(const QString &key) {
    if (!m_db.isValid() || !m_db.isOpen()) {
        m_connName = LibraryDb::openScopedConnection(QStringLiteral("artist_images"), m_db);
        if (m_connName.isEmpty()) return {};
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT image FROM artist_images WHERE name_norm = ? AND status = 1"));
    q.addBindValue(key);
    if (!q.exec() || !q.next()) return {};

    const QByteArray bytes = q.value(0).toByteArray();
    if (bytes.isEmpty()) return {};

    QImage img;
    if (!img.loadFromData(bytes, "PNG")) return {};
    return img;
}

QImage ArtistImageProvider::requestImage(const QString &id,
                                         QSize *size,
                                         const QSize &requestedSize) {
    const QString key = QUrl::fromPercentEncoding(id.toUtf8());
    if (key.isEmpty()) {
        if (size) *size = QSize(1, 1);
        return makePlaceholder();
    }

    QImage img;
    {
        QMutexLocker locker(&m_mutex);
        if (const QImage *cached = m_cache.object(key)) {
            img = *cached;
        } else {
            img = loadFromDatabaseLocked(key);
            if (!img.isNull()) {
                m_cache.insert(key, new QImage(img), imageCostKb(img));
            }
        }
    }

    if (img.isNull()) {
        if (size) *size = QSize(1, 1);
        return makePlaceholder();
    }

    if (size) *size = img.size();
    if (requestedSize.width() > 0 && requestedSize.height() > 0 &&
        (requestedSize.width() < img.width() || requestedSize.height() < img.height())) {
        return img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return img;
}

ArtistImageLoader::ArtistImageLoader(Settings *settings,
                                     ArtistImageProvider *provider,
                                     QObject *parent)
    : QObject(parent),
      m_settings(settings),
      m_provider(provider),
      m_nam(new QNetworkAccessManager(this)),
      m_dbRetryTimer(new QTimer(this)) {
    m_imagePool.setMaxThreadCount(2);
    m_dbRetryTimer->setSingleShot(true);
    m_dbRetryTimer->setInterval(250);
    connect(m_dbRetryTimer, &QTimer::timeout, this, &ArtistImageLoader::flushPendingDbWrites);

    m_enabled = m_settings->lastfmArtistImages();

    const QString legacyCacheDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        QStringLiteral("/artists");
    if (QDir(legacyCacheDir).exists()) {
        QDir(legacyCacheDir).removeRecursively();
    }

    loadIndexFromDatabase();
}

ArtistImageLoader::~ArtistImageLoader() {
    ++m_generation;
    abortActiveReplies();
    m_imagePool.clear();
    m_imagePool.waitForDone();
}

void ArtistImageLoader::detachProvider() {
    m_provider = nullptr;
}

void ArtistImageLoader::clearCache() {
    ++m_generation;
    abortActiveReplies();
    m_imagePool.clear();
    m_dbRetryTimer->stop();
    m_pendingDbWrites.clear();
    m_queue.clear();
    m_inFlight.clear();
    m_requestedNames.clear();
    m_cache.clear();
    m_failed.clear();
    if (m_provider) {
        m_provider->clearCache();
    }
}

void ArtistImageLoader::abortActiveReplies() {
    const auto replies = m_activeReplies;
    m_activeReplies.clear();
    for (QNetworkReply *reply : replies) {
        if (reply && reply->isRunning()) {
            reply->abort();
        }
    }
}

void ArtistImageLoader::trackReply(QNetworkReply *reply) {
    if (!reply) return;
    m_activeReplies.insert(reply);
    connect(reply, &QObject::destroyed, this, [this, reply]() {
        m_activeReplies.remove(reply);
    });
}

void ArtistImageLoader::loadIndexFromDatabase() {
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isValid() || !db.isOpen()) return;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT name_norm, status, updated_at FROM artist_images"))) {
        return;
    }

    while (q.next()) {
        const QString key       = q.value(0).toString();
        const int     status    = q.value(1).toInt();
        const qint64  updatedAt = q.value(2).toLongLong();
        if (key.isEmpty()) continue;

        if (status == 1) {
            m_cache.insert(key, makeProviderUrl(key));
        } else if (status < 0 && (now - updatedAt) < kNegativeCacheTtlSec) {
            m_failed.insert(key);
        }
    }
}

void ArtistImageLoader::saveImageToDatabase(const QString &key, const QByteArray &pngBytes) {
    m_pendingDbWrites.insert(key, PendingDbEntry{pngBytes, 1, QDateTime::currentSecsSinceEpoch(), 0});
    flushPendingDbWrites();
}

void ArtistImageLoader::saveNegativeToDatabase(const QString &key) {
    if (m_pendingDbWrites.contains(key) && m_pendingDbWrites.value(key).status == 1) return;
    m_pendingDbWrites.insert(key, PendingDbEntry{{}, -1, QDateTime::currentSecsSinceEpoch(), 0});
    flushPendingDbWrites();
}

void ArtistImageLoader::flushPendingDbWrites() {
    if (m_pendingDbWrites.isEmpty()) return;

    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isValid() || !db.isOpen()) {
        m_pendingDbWrites.clear();
        return;
    }

    LibraryDb::NonBlockingWrite nbw(db);
    if (!db.transaction()) {
        for (auto it = m_pendingDbWrites.begin(); it != m_pendingDbWrites.end(); ) {
            ++it->retries;
            if (it->retries > kMaxDbWriteRetries) {
                it = m_pendingDbWrites.erase(it);
            } else {
                ++it;
            }
        }
        if (!m_pendingDbWrites.isEmpty() && !m_dbRetryTimer->isActive()) {
            m_dbRetryTimer->start();
        }
        return;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO artist_images (name_norm, image, status, updated_at) "
        "VALUES (?, ?, ?, ?)"));

    QStringList succeeded;
    bool hitBusyOrError = false;
    for (auto it = m_pendingDbWrites.begin(); it != m_pendingDbWrites.end(); ++it) {
        q.bindValue(0, it.key());
        q.bindValue(1, it->status == 1 ? QVariant(it->pngBytes) : QVariant(QMetaType(QMetaType::QByteArray)));
        q.bindValue(2, it->status);
        q.bindValue(3, it->updatedAt);

        if (q.exec()) {
            succeeded.append(it.key());
        } else {
            hitBusyOrError = true;
            break;
        }
    }

    if (!hitBusyOrError && db.commit()) {
        for (const QString &k : succeeded) {
            m_pendingDbWrites.remove(k);
        }
    } else {
        db.rollback();
        for (auto it = m_pendingDbWrites.begin(); it != m_pendingDbWrites.end(); ) {
            ++it->retries;
            if (it->retries > kMaxDbWriteRetries) {
                it = m_pendingDbWrites.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (!m_pendingDbWrites.isEmpty() && !m_dbRetryTimer->isActive()) {
        m_dbRetryTimer->start();
    }
}

void ArtistImageLoader::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    ++m_generation;
    if (!enabled) {
        m_queue.clear();
        m_inFlight.clear();
        m_requestedNames.clear();
        abortActiveReplies();
        m_imagePool.clear();
    } else {
        m_failed.clear();
    }
    m_settings->setLastfmArtistImages(enabled);
    emit enabledChanged();
}

QString ArtistImageLoader::artistImageUrl(const QString &artist) {
    if (!m_enabled) return {};
    const QString trimmed = artist.trimmed();
    if (trimmed.isEmpty() ||
        trimmed.compare(QStringLiteral("Unknown Artist"), Qt::CaseInsensitive) == 0 ||
        trimmed.compare(QStringLiteral("[no artist]"), Qt::CaseInsensitive) == 0) {
        return {};
    }

    const QString key = trimmed.toCaseFolded();
    const auto cachedIt = m_cache.constFind(key);
    if (cachedIt != m_cache.constEnd()) {
        return cachedIt.value();
    }

    if (m_failed.contains(key)) return {};

    QStringList &names = m_requestedNames[key];
    if (!names.contains(trimmed)) names.append(trimmed);

    if (!m_inFlight.contains(key)) {
        m_queue.removeOne(key);
        m_queue.prepend(key);
        while (m_queue.size() > kMaxQueueSize) {
            const QString dropped = m_queue.takeLast();
            if (!m_inFlight.contains(dropped)) {
                m_requestedNames.remove(dropped);
            }
        }
        processQueue();
    }
    return {};
}

void ArtistImageLoader::processQueue() {
    while (m_enabled &&
           m_inFlight.size() < kMaxConcurrentFetch &&
           !m_queue.isEmpty()) {
        const QString key = m_queue.takeFirst();
        if (m_inFlight.contains(key) || m_cache.contains(key)) continue;
        const QStringList names = m_requestedNames.value(key);
        const QString queryName = names.isEmpty() ? key : names.constFirst();
        m_inFlight.insert(key);
        fetchFromDeezer(key, queryName);
    }
}

void ArtistImageLoader::fetchFromDeezer(const QString &key, const QString &artist) {
    QUrl url(QString::fromLatin1(kDeezerSearchUrl));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), artist);
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    url.setQuery(query);

    const quint64 gen = m_generation;
    QNetworkRequest request(url);
    request.setTransferTimeout(kRequestTimeoutMs);
    request.setRawHeader("User-Agent", kUserAgent);
    QNetworkReply *reply = m_nam->get(request);
    trackReply(reply);
    armHardTimeout(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, key, artist, gen]() {
        m_activeReplies.remove(reply);
        const QNetworkReply::NetworkError networkError = reply->error();
        const QByteArray payload =
            (networkError == QNetworkReply::NoError && reply->isReadable())
                ? reply->readAll()
                : QByteArray();
        reply->deleteLater();

        if (!m_enabled || gen != m_generation) return;

        QString matchedUrl;
        bool validApiResponse = false;

        if (!payload.isEmpty()) {
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                validApiResponse = true;
                const QJsonArray data = doc.object().value(QStringLiteral("data")).toArray();
                const QString targetFolded = artist.trimmed().toCaseFolded();

                int bestFans = -1;
                for (const QJsonValue &val : data) {
                    const QJsonObject item = val.toObject();
                    const QString name = item.value(QStringLiteral("name")).toString().trimmed().toCaseFolded();
                    QString pic = item.value(QStringLiteral("picture_big")).toString().trimmed();
                    if (!isValidDeezerPicture(pic)) {
                        pic = item.value(QStringLiteral("picture_medium")).toString().trimmed();
                    }
                    const int fans = item.value(QStringLiteral("nb_fan")).toInt(0);
                    if (!isValidDeezerPicture(pic)) continue;

                    if (name == key || name == targetFolded) {
                        if (fans > bestFans) {
                            bestFans   = fans;
                            matchedUrl = pic;
                        }
                    }
                }
            }
        }

        if (!matchedUrl.isEmpty()) {
            downloadArtistImage(key, QUrl(matchedUrl));
            return;
        }

        if (networkError == QNetworkReply::NoError && validApiResponse) {
            saveNegativeToDatabase(key);
        }
        finishFetch(key, {});
    });
}

void ArtistImageLoader::downloadArtistImage(const QString &key, const QUrl &imageUrl) {
    if (!imageUrl.isValid()) {
        finishFetch(key, {});
        return;
    }

    const quint64 gen = m_generation;
    QNetworkRequest request(imageUrl);
    request.setTransferTimeout(kRequestTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", kUserAgent);
    QNetworkReply *reply = m_nam->get(request);
    trackReply(reply);
    armHardTimeout(reply);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, key, gen]() {
        m_activeReplies.remove(reply);
        const QNetworkReply::NetworkError networkError = reply->error();
        const QByteArray bytes =
            (networkError == QNetworkReply::NoError && reply->isReadable())
                ? reply->readAll()
                : QByteArray();
        reply->deleteLater();

        if (!m_enabled || gen != m_generation) return;

        if (!bytes.isEmpty()) {
            m_imagePool.start([this, bytes, key, gen]() {
                QImage square;
                QByteArray pngBytes;
                QImage raw;
                if (raw.loadFromData(bytes)) {
                    square = makeSquareAvatar(raw, kAvatarMaxEdge);
                    if (!square.isNull()) {
                        QBuffer buffer(&pngBytes);
                        buffer.open(QIODevice::WriteOnly);
                        if (!square.save(&buffer, "PNG")) {
                            square = {};
                            pngBytes.clear();
                        }
                    }
                }

                QMetaObject::invokeMethod(this, [this, square, pngBytes, key, gen]() {
                    if (!m_enabled || gen != m_generation) return;
                    if (!square.isNull() && !pngBytes.isEmpty()) {
                        if (m_provider) {
                            m_provider->insertImage(key, square);
                        }
                        saveImageToDatabase(key, pngBytes);
                        finishFetch(key, makeProviderUrl(key));
                        return;
                    }
                    finishFetch(key, {});
                }, Qt::QueuedConnection);
            });
            return;
        }

        finishFetch(key, {});
    });
}

void ArtistImageLoader::finishFetch(const QString &key, const QString &providerUrl) {
    m_inFlight.remove(key);
    const QStringList names = m_requestedNames.take(key);

    if (!providerUrl.isEmpty()) {
        m_cache.insert(key, providerUrl);
        for (const QString &name : names) {
            emit artistImageReady(name, providerUrl);
        }
    } else {
        m_failed.insert(key);
    }

    processQueue();
}
