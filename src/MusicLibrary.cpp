#include "MusicLibrary.h"

#include "AudioFormats.h"
#include "LibraryDb.h"
#include "ScannerHelpers.h"

#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QVariant>

#include <taglib/aiffproperties.h>
#include <taglib/apeproperties.h>
#include <taglib/asfproperties.h>
#include <taglib/fileref.h>
#include <taglib/flacproperties.h>
#include <taglib/mp4properties.h>
#include <taglib/mpegproperties.h>
#include <taglib/opusproperties.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/vorbisproperties.h>
#include <taglib/wavproperties.h>

#include <utility>

namespace {

QString makeTechInfo(const QString &formatLabel, int sampleRate, int bitrate, int bitDepth) {
    const double khz = sampleRate / 1000.0;
    QString prefix;
    if (!formatLabel.isEmpty()) prefix = formatLabel + QStringLiteral(" | ");
    QString out = QStringLiteral("%1%2 kHz").arg(prefix).arg(khz, 0, 'f', 1);
    if (bitDepth > 0) out += QStringLiteral(" | %1 bit").arg(bitDepth);
    out += QStringLiteral(" | %1 kbps").arg(bitrate);
    return out;
}

QString fallbackFormatLabel(const QString &filePath) {
    const QString lower = filePath.toLower();
    if (lower.endsWith(QLatin1String(".flac"))) return QStringLiteral("FLAC");
    if (lower.endsWith(QLatin1String(".mp3")))  return QStringLiteral("MP3");
    if (lower.endsWith(QLatin1String(".wav")))  return QStringLiteral("WAV");
    if (lower.endsWith(QLatin1String(".m4a")))  return QStringLiteral("M4A");
    if (lower.endsWith(QLatin1String(".mp4")))  return QStringLiteral("MP4");
    if (lower.endsWith(QLatin1String(".aac")))  return QStringLiteral("AAC");
    if (lower.endsWith(QLatin1String(".ogg")) || lower.endsWith(QLatin1String(".oga")))
        return QStringLiteral("OGG");
    if (lower.endsWith(QLatin1String(".opus"))) return QStringLiteral("Opus");
    if (lower.endsWith(QLatin1String(".wma")))  return QStringLiteral("WMA");
    if (lower.endsWith(QLatin1String(".aiff")) || lower.endsWith(QLatin1String(".aif")))
        return QStringLiteral("AIFF");
    if (lower.endsWith(QLatin1String(".ape")))  return QStringLiteral("APE");
    if (lower.endsWith(QLatin1String(".alac"))) return QStringLiteral("ALAC");
    return {};
}

}

namespace MusicLibrary {

QString databasePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/music_library.db");
}

bool initialize() {
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isValid()) {
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
        db.setDatabaseName(databasePath());
    }
    if (!db.isOpen() && !db.open()) {
        qWarning() << "[MusicLibrary] failed to open database" << databasePath()
                   << db.lastError().text();
        return false;
    }

    QSqlQuery wal(db);
    wal.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    wal.finish();
    LibraryDb::applyConnectionPragmas(db);

    QSqlQuery q(db);
    const auto execRequired = [&q](const QString &sql, const char *operation) {
        if (q.exec(sql)) return true;
        qWarning() << "[MusicLibrary]" << operation << q.lastError().text();
        return false;
    };
    if (!execRequired(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tracks ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "title TEXT, artist TEXT, album TEXT, "
            "path TEXT UNIQUE, duration INTEGER, "
            "search_text TEXT, track_no INTEGER, "
            "tech_info TEXT, file_size INTEGER DEFAULT 0, "
            "album_artist TEXT, file_mtime INTEGER DEFAULT 0)"),
            "create tracks table")) return false;

    QSet<QString> trackColumns;
    if (!q.exec(QStringLiteral("PRAGMA table_info(tracks)"))) {
        qWarning() << "[MusicLibrary] inspect tracks schema" << q.lastError().text();
        return false;
    }
    while (q.next()) trackColumns.insert(q.value(1).toString());
    const auto addTrackColumn = [&db, &trackColumns](const QString &name,
                                                     const QString &definition) {
        if (trackColumns.contains(name)) return true;
        QSqlQuery alter(db);
        if (alter.exec(QStringLiteral("ALTER TABLE tracks ADD COLUMN ") + definition)) {
            trackColumns.insert(name);
            return true;
        }
        qWarning() << "[MusicLibrary] add tracks column" << name
                   << alter.lastError().text();
        return false;
    };
    if (!addTrackColumn(QStringLiteral("file_size"),
                        QStringLiteral("file_size INTEGER DEFAULT 0")) ||
        !addTrackColumn(QStringLiteral("album_artist"),
                        QStringLiteral("album_artist TEXT")) ||
        !addTrackColumn(QStringLiteral("file_mtime"),
                        QStringLiteral("file_mtime INTEGER DEFAULT 0"))) return false;

    if (!execRequired(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS watch_roots (path TEXT PRIMARY KEY)"),
            "create watch_roots table") ||
        !execRequired(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS artists ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "name_norm TEXT NOT NULL UNIQUE)"),
            "create artists table") ||
        !execRequired(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS track_artists ("
            "track_id INTEGER NOT NULL, "
            "artist_id INTEGER NOT NULL, "
            "PRIMARY KEY (track_id, artist_id))"),
            "create track_artists table")) return false;

    if (!execRequired(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_tracks_album_artist "
        "ON tracks(album COLLATE NOCASE, album_artist COLLATE NOCASE)"),
        "create album index") ||
        !execRequired(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_tracks_title ON tracks(title COLLATE NOCASE)"),
            "create title index") ||
        !execRequired(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist COLLATE NOCASE)"),
            "create artist index") ||
        !execRequired(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_tracks_duration ON tracks(duration)"),
            "create duration index") ||
        !execRequired(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_tracks_track_no ON tracks(track_no)"),
            "create track number index") ||
        !execRequired(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_artists_name ON artists(name COLLATE NOCASE)"),
            "create normalized artist index") ||
        !execRequired(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_track_artists_artist ON track_artists(artist_id)"),
            "create track artist index") ||
        !execRequired(QStringLiteral(
        "CREATE TRIGGER IF NOT EXISTS trk_after_delete AFTER DELETE ON tracks "
        "BEGIN DELETE FROM track_artists WHERE track_id = OLD.id; END"),
        "create track cleanup trigger")) return false;

    int userVersion = 0;
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next()) {
        qWarning() << "[MusicLibrary] read schema version" << q.lastError().text();
        return false;
    }
    userVersion = q.value(0).toInt();
    q.finish();
    if (userVersion < 5) {
        if (!db.transaction()) {
            qWarning() << "[MusicLibrary] begin schema migration" << db.lastError().text();
            return false;
        }

        QSqlQuery albumTracks(db);
        QSqlQuery updateAlbumArtist(db);
        updateAlbumArtist.prepare(QStringLiteral(
            "UPDATE tracks SET album_artist = ? WHERE id = ?"));
        if (!albumTracks.exec(QStringLiteral(
                "SELECT id, artist FROM tracks "
                "WHERE album_artist IS NULL OR album_artist = ''"))) {
            qWarning() << "[MusicLibrary] select album artists for migration"
                       << albumTracks.lastError().text();
            db.rollback();
            return false;
        }
        while (albumTracks.next()) {
            updateAlbumArtist.bindValue(
                0, pickAlbumArtist(QString(), albumTracks.value(1).toString()));
            updateAlbumArtist.bindValue(1, albumTracks.value(0));
            if (!updateAlbumArtist.exec()) {
                qWarning() << "[MusicLibrary] backfill album artists"
                           << updateAlbumArtist.lastError().text();
                db.rollback();
                return false;
            }
        }
        albumTracks.finish();
        updateAlbumArtist.finish();

        QSqlQuery tracks(db);
        QSqlQuery upsertArtist(db);
        QSqlQuery findArtistId(db);
        QSqlQuery linkTrackArtist(db);
        upsertArtist.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO artists (name, name_norm) VALUES (?, ?)"));
        findArtistId.prepare(QStringLiteral(
            "SELECT id FROM artists WHERE name_norm = ?"));
        linkTrackArtist.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO track_artists (track_id, artist_id) VALUES (?, ?)"));
        if (!tracks.exec(QStringLiteral(
                "SELECT id, artist FROM tracks WHERE NOT EXISTS "
                "(SELECT 1 FROM track_artists WHERE track_id = tracks.id)"))) {
            qWarning() << "[MusicLibrary] select tracks for artist migration"
                       << tracks.lastError().text();
            db.rollback();
            return false;
        }
        while (tracks.next()) {
            if (!linkTrackToArtistsPrepared(tracks.value(0).toLongLong(),
                                            tracks.value(1).toString(),
                                            upsertArtist, findArtistId,
                                            linkTrackArtist)) {
                qWarning() << "[MusicLibrary] migrate track artists";
                db.rollback();
                return false;
            }
        }
        tracks.finish();
        upsertArtist.finish();
        findArtistId.finish();
        linkTrackArtist.finish();
        if (!db.commit()) {
            qWarning() << "[MusicLibrary] commit schema migration" << db.lastError().text();
            db.rollback();
            return false;
        }
        if (!q.exec(QStringLiteral("PRAGMA user_version = 5"))) {
            qWarning() << "[MusicLibrary] store schema version" << q.lastError().text();
            return false;
        }
    }

    return true;
}

QString normalizeArtistName(const QString &raw) {
    return raw.trimmed().toLower().simplified();
}

QStringList splitArtists(const QString &raw) {
    QStringList result;
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) return result;

    static const QRegularExpression splitter(
        QStringLiteral(
            R"(\s*(?:[,;/\\&×]|\bfeat\b\.?|\bft\b\.?|\bfeaturing\b|\bvs\b\.?|\bpresents\b|\bwith\b)\s*)"
            R"(|(?-i:\s+x\s+))"),
        QRegularExpression::CaseInsensitiveOption);

    const QStringList parts = trimmed.split(splitter, Qt::SkipEmptyParts);
    QSet<QString> seen;
    for (const QString &p : parts) {
        const QString name = p.trimmed();
        if (name.isEmpty()) continue;
        const QString norm = normalizeArtistName(name);
        if (norm.isEmpty() || seen.contains(norm)) continue;
        seen.insert(norm);
        result.append(name);
    }
    if (result.isEmpty()) result.append(trimmed);
    return result;
}

QString pickAlbumArtist(const QString &albumArtistTag, const QString &artist) {
    const QString tag = albumArtistTag.trimmed();
    if (!tag.isEmpty()) {
        const QStringList tagParts = splitArtists(tag);
        if (!tagParts.isEmpty()) return tagParts.first();
        return tag;
    }
    const QStringList parts = splitArtists(artist);
    if (!parts.isEmpty()) return parts.first();
    return artist.trimmed();
}

int pruneOrphanArtists(QSqlDatabase &db) {
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "DELETE FROM artists WHERE id NOT IN "
            "(SELECT DISTINCT artist_id FROM track_artists)"))) {
        qWarning() << "[MusicLibrary] prune orphan artists" << q.lastError().text();
        return 0;
    }
    return q.numRowsAffected();
}

bool linkTrackToArtistsPrepared(qint64 trackId,
                                const QString &rawArtists,
                                QSqlQuery &upsertArtist,
                                QSqlQuery &findArtistId,
                                QSqlQuery &linkTrackArtist) {
    if (trackId <= 0) return false;
    const QStringList names = splitArtists(rawArtists);

    for (const QString &display : names) {
        const QString norm = normalizeArtistName(display);
        if (norm.isEmpty()) continue;

        upsertArtist.bindValue(0, display);
        upsertArtist.bindValue(1, norm);
        if (!upsertArtist.exec()) return false;

        findArtistId.bindValue(0, norm);
        if (!findArtistId.exec() || !findArtistId.next()) return false;
        const qint64 artistId = findArtistId.value(0).toLongLong();
        findArtistId.finish();

        linkTrackArtist.bindValue(0, trackId);
        linkTrackArtist.bindValue(1, artistId);
        if (!linkTrackArtist.exec()) return false;
    }
    return true;
}

bool readTrackFromFile(const QString &filePath, Track &t, qint64 &fileSize,
                       qint64 &modifiedTime) {
    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) return false;
    fileSize = info.size();
    modifiedTime = info.lastModified().toMSecsSinceEpoch();

    const QByteArray pathBytes = filePath.toUtf8();
    TagLib::FileRef f(pathBytes.constData());

    QString title  = info.fileName();
    QString artist = QStringLiteral("Unknown Artist");
    QString album  = QStringLiteral("Unknown Album");
    QString albumArtistTag;
    QString techInfo;
    int duration = 0;
    int trackNo  = 0;

    if (!f.isNull()) {
        if (auto *tag = f.tag()) {
            const QString tTitle  = QString::fromStdString(tag->title().to8Bit(true));
            const QString tArtist = QString::fromStdString(tag->artist().to8Bit(true));
            const QString tAlbum  = QString::fromStdString(tag->album().to8Bit(true));
            if (!tTitle.isEmpty())  title  = tTitle;
            if (!tArtist.isEmpty()) artist = tArtist;
            if (!tAlbum.isEmpty())  album  = tAlbum;
            trackNo = tag->track();
        }
        if (auto *file = f.file()) {
            const TagLib::PropertyMap props = file->properties();
            const auto pickProp = [&props](const char *key) -> QString {
                const auto it = props.find(key);
                if (it == props.end() || it->second.isEmpty()) return {};
                return QString::fromStdString(it->second.front().to8Bit(true)).trimmed();
            };
            albumArtistTag = pickProp("ALBUMARTIST");
            if (albumArtistTag.isEmpty()) albumArtistTag = pickProp("ALBUM ARTIST");
            if (albumArtistTag.isEmpty()) albumArtistTag = pickProp("ALBUM_ARTIST");
        }
        if (auto *audio = f.audioProperties()) {
            duration = audio->lengthInSeconds();
            int bitDepth = 0;
            QString formatLabel;
            if (auto *p = dynamic_cast<TagLib::FLAC::Properties*>(audio)) {
                bitDepth = p->bitsPerSample();
                formatLabel = QStringLiteral("FLAC");
            } else if (auto *p = dynamic_cast<TagLib::RIFF::WAV::Properties*>(audio)) {
                bitDepth = p->bitsPerSample();
                formatLabel = QStringLiteral("WAV");
            } else if (auto *p = dynamic_cast<TagLib::RIFF::AIFF::Properties*>(audio)) {
                bitDepth = p->bitsPerSample();
                formatLabel = QStringLiteral("AIFF");
            } else if (auto *p = dynamic_cast<TagLib::MP4::Properties*>(audio)) {
                if (p->codec() == TagLib::MP4::Properties::ALAC) {
                    bitDepth = p->bitsPerSample();
                    formatLabel = QStringLiteral("ALAC");
                } else {
                    formatLabel = QStringLiteral("M4A");
                }
            } else if (auto *p = dynamic_cast<TagLib::APE::Properties*>(audio)) {
                bitDepth = p->bitsPerSample();
                formatLabel = QStringLiteral("APE");
            } else if (dynamic_cast<TagLib::MPEG::Properties*>(audio)) {
                formatLabel = QStringLiteral("MP3");
            } else if (dynamic_cast<TagLib::Ogg::Vorbis::Properties*>(audio)) {
                formatLabel = QStringLiteral("OGG");
            } else if (dynamic_cast<TagLib::Ogg::Opus::Properties*>(audio)) {
                formatLabel = QStringLiteral("Opus");
            } else if (dynamic_cast<TagLib::ASF::Properties*>(audio)) {
                formatLabel = QStringLiteral("WMA");
            }
            if (formatLabel.isEmpty()) formatLabel = fallbackFormatLabel(filePath);
            techInfo = makeTechInfo(formatLabel, audio->sampleRate(), audio->bitrate(), bitDepth);
        }
    }

    t.path        = filePath;
    t.title       = title;
    t.artist      = artist;
    t.albumArtist = pickAlbumArtist(albumArtistTag, artist);
    t.album       = album;
    t.duration    = duration;
    t.techInfo    = techInfo;
    t.trackNo     = trackNo;
    return true;
}

}

LibraryScanner::LibraryScanner(QString rootPath, QObject *parent)
    : QObject(parent), m_rootPath(std::move(rootPath)) {}

void LibraryScanner::run() {
    if (m_rootPath.isEmpty()) {
        emit countDetermined(0);
        emit finished({}, true);
        return;
    }

    QStringList allFiles;
    {
        QDirIterator it(m_rootPath,
                        AudioFormats::nameFilters(),
                        QDir::Files | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (QThread::currentThread()->isInterruptionRequested()) {
                emit finished({}, false);
                return;
            }
            allFiles.append(it.next());
        }
    }
    emit countDetermined(allFiles.size());

    if (allFiles.isEmpty()) {
        emit finished({}, true);
        return;
    }

    constexpr int  kBatchSize        = 250;
    constexpr int  kBatchMaxMs       = 800;
    constexpr int  kProgressEveryN   = 10;

    QSqlDatabase db;
    const QString connName = LibraryDb::openScopedConnection(QStringLiteral("scan"), db);
    if (connName.isEmpty()) {
        emit finished({}, false);
        return;
    }

    QList<Track> newTracks;
    bool dbFailed = false;
    int processed = 0;
    while (processed < allFiles.size() &&
           !QThread::currentThread()->isInterruptionRequested()) {
        struct PreparedTrack {
            Track track;
            qint64 fileSize = 0;
            qint64 modifiedTime = 0;
        };
        QList<PreparedTrack> prepared;
        QElapsedTimer batchTimer;
        batchTimer.start();
        while (processed < allFiles.size() && prepared.size() < kBatchSize) {
            if (QThread::currentThread()->isInterruptionRequested()) break;
            PreparedTrack item;
            if (MusicLibrary::readTrackFromFile(allFiles.at(processed), item.track,
                                                item.fileSize, item.modifiedTime)) {
                prepared.append(item);
            }
            ++processed;
            if (processed % kProgressEveryN == 0 || processed == allFiles.size())
                emit progress(processed, allFiles.size());
            if (batchTimer.elapsed() >= kBatchMaxMs) break;
        }
        if (QThread::currentThread()->isInterruptionRequested()) break;
        if (prepared.isEmpty()) continue;

        QSqlQuery begin(db);
        if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
            dbFailed = true;
            break;
        }
        ScannerHelpers::TrackInserter inserter(db);
        for (const PreparedTrack &item : std::as_const(prepared)) {
            if (QThread::currentThread()->isInterruptionRequested()) {
                dbFailed = true;
                break;
            }
            const QFileInfo current(item.track.path);
            if (!current.exists() || !current.isFile() ||
                current.size() != item.fileSize ||
                current.lastModified().toMSecsSinceEpoch() != item.modifiedTime) {
                dbFailed = true;
                break;
            }
            const auto writeResult = inserter.upsert(
                item.track, item.fileSize, item.modifiedTime);
            if (writeResult == ScannerHelpers::TrackWriteResult::Inserted)
                newTracks.append(item.track);
            if (writeResult == ScannerHelpers::TrackWriteResult::Error) {
                dbFailed = true;
                break;
            }
        }
        if (dbFailed) {
            db.rollback();
            break;
        }
        if (QThread::currentThread()->isInterruptionRequested()) {
            db.rollback();
            break;
        }
        MusicLibrary::pruneOrphanArtists(db);
        if (!db.commit()) {
            db.rollback();
            dbFailed = true;
            break;
        }
        emit batchReady();
    }
    LibraryDb::closeScopedConnection(connName, db);

    const bool success = !QThread::currentThread()->isInterruptionRequested() && !dbFailed;
    emit finished(success ? newTracks : QList<Track>{}, success);
}
