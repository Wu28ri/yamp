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

constexpr const char *kLastFmApiRoot   = "https://ws.audioscrobbler.com/2.0/";
constexpr const char *kLastFmApiKey    = "1af929173f9ea4c2116ed32d25f8db59";
constexpr const char *kDeezerSearchUrl = "https://api.deezer.com/search/artist";
constexpr const char *kUserAgent       = "YAMP/0.3 (https://github.com/Wu28ri/YAMP)";

constexpr int    kRequestTimeoutMs    = 8000;
constexpr int    kMaxConcurrentFetch  = 6;
constexpr int    kAvatarMaxEdge       = 256;
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

bool isPlaceholderLastFmImage(const QString &url) {
    return url.isEmpty() ||
           url.contains(QLatin1String("2a96cbd8b46e442fc41c2b86b821562f"), Qt::CaseInsensitive) ||
           url.contains(QLatin1String("4128a6eb29f94943c9d206c08e625904"), Qt::CaseInsensitive) ||
           url.contains(QLatin1String("c6f59c1e5e7240a4c0d427abd71f3dbb"), Qt::CaseInsensitive);
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
      m_nam(new QNetworkAccessManager(this)) {
    m_enabled = m_settings->lastfmArtistImages();

    const QString legacyCacheDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        QStringLiteral("/artists");
    if (QDir(legacyCacheDir).exists()) {
        QDir(legacyCacheDir).removeRecursively();
    }

    loadIndexFromDatabase();
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
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isValid() || !db.isOpen()) return;

    LibraryDb::NonBlockingWrite nbw(db);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO artist_images (name_norm, image, status, updated_at) "
        "VALUES (?, ?, 1, ?)"));
    q.addBindValue(key);
    q.addBindValue(pngBytes);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.exec();
}

void ArtistImageLoader::saveNegativeToDatabase(const QString &key) {
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isValid() || !db.isOpen()) return;

    LibraryDb::NonBlockingWrite nbw(db);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO artist_images (name_norm, image, status, updated_at) "
        "VALUES (?, NULL, -1, ?)"));
    q.addBindValue(key);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.exec();
}

void ArtistImageLoader::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!enabled) {
        m_queue.clear();
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
        fetchFromLastFmApi(key, queryName);
    }
}

void ArtistImageLoader::fetchFromLastFmApi(const QString &key, const QString &artist) {
    QUrl url(QString::fromLatin1(kLastFmApiRoot));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("method"), QStringLiteral("artist.getInfo"));
    query.addQueryItem(QStringLiteral("artist"), artist);
    query.addQueryItem(QStringLiteral("autocorrect"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("api_key"), QString::fromLatin1(kLastFmApiKey));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(kRequestTimeoutMs);
    request.setRawHeader("User-Agent", kUserAgent);
    QNetworkReply *reply = m_nam->get(request);
    armHardTimeout(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, key, artist]() {
        const QNetworkReply::NetworkError networkError = reply->error();
        const QByteArray payload =
            (networkError == QNetworkReply::NoError && reply->isReadable())
                ? reply->readAll()
                : QByteArray();
        reply->deleteLater();

        QString imageUrl;
        QString canonicalName = artist;

        if (!payload.isEmpty()) {
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject root = doc.object();
                if (!root.contains(QStringLiteral("error"))) {
                    const QJsonObject artistObj = root.value(QStringLiteral("artist")).toObject();
                    const QString resolvedName = artistObj.value(QStringLiteral("name")).toString().trimmed();
                    if (!resolvedName.isEmpty()) canonicalName = resolvedName;

                    const QJsonArray images = artistObj.value(QStringLiteral("image")).toArray();
                    for (const QJsonValue &val : images) {
                        const QJsonObject imgObj = val.toObject();
                        const QString candidate  = imgObj.value(QStringLiteral("#text")).toString().trimmed();
                        if (!isPlaceholderLastFmImage(candidate)) {
                            imageUrl = candidate;
                            const QString size = imgObj.value(QStringLiteral("size")).toString();
                            if (size == QLatin1String("extralarge") || size == QLatin1String("mega"))
                                break;
                        }
                    }
                }
            }
        }

        if (!imageUrl.isEmpty()) {
            downloadArtistImage(key, canonicalName, QUrl(imageUrl), true);
            return;
        }

        fetchFromDeezer(key, canonicalName);
    });
}

void ArtistImageLoader::fetchFromDeezer(const QString &key, const QString &artist) {
    QUrl url(QString::fromLatin1(kDeezerSearchUrl));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), artist);
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(kRequestTimeoutMs);
    request.setRawHeader("User-Agent", kUserAgent);
    QNetworkReply *reply = m_nam->get(request);
    armHardTimeout(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, key, artist]() {
        const QNetworkReply::NetworkError networkError = reply->error();
        const QByteArray payload =
            (networkError == QNetworkReply::NoError && reply->isReadable())
                ? reply->readAll()
                : QByteArray();
        reply->deleteLater();

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
                    const QString pic  = item.value(QStringLiteral("picture_medium")).toString().trimmed();
                    const int     fans = item.value(QStringLiteral("nb_fan")).toInt(0);
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
            downloadArtistImage(key, artist, QUrl(matchedUrl), false);
            return;
        }

        if (networkError == QNetworkReply::NoError && validApiResponse) {
            saveNegativeToDatabase(key);
        }
        finishFetch(key, {});
    });
}

void ArtistImageLoader::downloadArtistImage(const QString &key,
                                            const QString &artist,
                                            const QUrl &imageUrl,
                                            bool allowDeezerFallback) {
    if (!imageUrl.isValid()) {
        if (allowDeezerFallback) {
            fetchFromDeezer(key, artist);
        } else {
            finishFetch(key, {});
        }
        return;
    }

    QNetworkRequest request(imageUrl);
    request.setTransferTimeout(kRequestTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", kUserAgent);
    QNetworkReply *reply = m_nam->get(request);
    armHardTimeout(reply);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, key, artist, allowDeezerFallback]() {
        const QNetworkReply::NetworkError networkError = reply->error();
        const QByteArray bytes =
            (networkError == QNetworkReply::NoError && reply->isReadable())
                ? reply->readAll()
                : QByteArray();
        reply->deleteLater();

        if (!bytes.isEmpty()) {
            QImage raw;
            if (raw.loadFromData(bytes)) {
                const QImage square = makeSquareAvatar(raw, kAvatarMaxEdge);
                if (!square.isNull()) {
                    QByteArray pngBytes;
                    QBuffer buffer(&pngBytes);
                    buffer.open(QIODevice::WriteOnly);
                    if (square.save(&buffer, "PNG")) {
                        if (m_provider) {
                            m_provider->insertImage(key, square);
                        }
                        saveImageToDatabase(key, pngBytes);
                        finishFetch(key, makeProviderUrl(key));
                        return;
                    }
                }
            }
        }

        if (allowDeezerFallback) {
            fetchFromDeezer(key, artist);
        } else {
            finishFetch(key, {});
        }
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
