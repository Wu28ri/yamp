#include "MusicLibrary.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
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
    if (!db.isOpen() && !db.open()) return false;

    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA temp_store=MEMORY"));
    pragma.exec(QStringLiteral("PRAGMA cache_size=-32000"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tracks ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "title TEXT, artist TEXT, album TEXT, "
            "path TEXT UNIQUE, duration INTEGER, "
            "search_text TEXT, track_no INTEGER, "
            "tech_info TEXT, file_size INTEGER DEFAULT 0, "
            "album_artist TEXT)"))) {
        return false;
    }

    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_tracks_album_artist "
        "ON tracks(album COLLATE NOCASE, album_artist COLLATE NOCASE)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_tracks_title ON tracks(title COLLATE NOCASE)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist COLLATE NOCASE)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_tracks_duration ON tracks(duration)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_tracks_track_no ON tracks(track_no)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_artists_name ON artists(name COLLATE NOCASE)"));

    QSqlQuery wq(db);
    if (!wq.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS watch_roots (path TEXT PRIMARY KEY)"))) {
        return false;
    }

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS artists ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "name_norm TEXT NOT NULL UNIQUE)"))) {
        return false;
    }
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS track_artists ("
            "track_id INTEGER NOT NULL, "
            "artist_id INTEGER NOT NULL, "
            "PRIMARY KEY (track_id, artist_id))"))) {
        return false;
    }
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_track_artists_artist ON track_artists(artist_id)"));
    q.exec(QStringLiteral(
        "CREATE TRIGGER IF NOT EXISTS trk_after_delete AFTER DELETE ON tracks "
        "BEGIN DELETE FROM track_artists WHERE track_id = OLD.id; END"));

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
            "(SELECT DISTINCT artist_id FROM track_artists)"))) return 0;
    return q.numRowsAffected();
}

void linkTrackToArtistsPrepared(qint64 trackId,
                                const QString &rawArtists,
                                QSqlQuery &upsertArtist,
                                QSqlQuery &findArtistId,
                                QSqlQuery &linkTrackArtist) {
    if (trackId <= 0) return;
    const QStringList names = splitArtists(rawArtists);

    for (const QString &display : names) {
        const QString norm = normalizeArtistName(display);
        if (norm.isEmpty()) continue;

        upsertArtist.bindValue(0, display);
        upsertArtist.bindValue(1, norm);
        if (!upsertArtist.exec()) continue;

        findArtistId.bindValue(0, norm);
        if (!findArtistId.exec() || !findArtistId.next()) continue;
        const qint64 artistId = findArtistId.value(0).toLongLong();
        findArtistId.finish();

        linkTrackArtist.bindValue(0, trackId);
        linkTrackArtist.bindValue(1, artistId);
        linkTrackArtist.exec();
    }
}

bool readTrackFromFile(const QString &filePath, Track &t, qint64 &fileSize) {
    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) return false;
    fileSize = info.size();

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
        emit finished({});
        return;
    }

    QStringList allFiles;
    {
        QDirIterator it(m_rootPath,
                        {QStringLiteral("*.flac"), QStringLiteral("*.mp3"),
                         QStringLiteral("*.wav"),  QStringLiteral("*.m4a"),
                         QStringLiteral("*.mp4"),  QStringLiteral("*.aac"),
                         QStringLiteral("*.ogg"),  QStringLiteral("*.oga"),
                         QStringLiteral("*.opus"), QStringLiteral("*.wma"),
                         QStringLiteral("*.aiff"), QStringLiteral("*.aif"),
                         QStringLiteral("*.ape"),  QStringLiteral("*.alac")},
                        QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) allFiles.append(it.next());
    }
    emit countDetermined(allFiles.size());

    if (allFiles.isEmpty()) {
        emit finished({});
        return;
    }

    constexpr int  kBatchSize        = 250;
    constexpr int  kBatchMaxMs       = 800;
    constexpr int  kProgressEveryN   = 10;

    const QString connName = QStringLiteral("yamp_scan_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QList<Track> newTracks;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(MusicLibrary::databasePath());
        if (!db.open()) {
            QSqlDatabase::removeDatabase(connName);
            emit finished({});
            return;
        }

        QSqlQuery pragma(db);
        pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
        pragma.exec(QStringLiteral("PRAGMA temp_store=MEMORY"));
        pragma.exec(QStringLiteral("PRAGMA cache_size=-32000"));
        pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));

        QSqlQuery insertTrack(db);
        insertTrack.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO tracks "
            "(title, artist, album, path, duration, search_text, track_no, tech_info, "
            " file_size, album_artist) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

        QSqlQuery upsertArtist(db);
        upsertArtist.prepare(QStringLiteral("INSERT OR IGNORE INTO artists (name, name_norm) VALUES (?, ?)"));
        QSqlQuery findArtistId(db);
        findArtistId.prepare(QStringLiteral("SELECT id FROM artists WHERE name_norm = ?"));
        QSqlQuery linkTrackArtist(db);
        linkTrackArtist.prepare(QStringLiteral("INSERT OR IGNORE INTO track_artists (track_id, artist_id) VALUES (?, ?)"));

        db.transaction();

        QElapsedTimer batchTimer;
        batchTimer.start();
        int batchInTx = 0;
        int processed = 0;
        const int total = allFiles.size();

        for (const QString &filePath : allFiles) {
            Track t;
            qint64 fileSize = 0;
            if (!MusicLibrary::readTrackFromFile(filePath, t, fileSize)) {
                ++processed;
                continue;
            }

            const QString searchText = (t.title + QLatin1Char(' ') + t.artist + QLatin1Char(' ') + t.album).toLower();

            insertTrack.bindValue(0, t.title);
            insertTrack.bindValue(1, t.artist);
            insertTrack.bindValue(2, t.album);
            insertTrack.bindValue(3, t.path);
            insertTrack.bindValue(4, t.duration);
            insertTrack.bindValue(5, searchText);
            insertTrack.bindValue(6, t.trackNo);
            insertTrack.bindValue(7, t.techInfo);
            insertTrack.bindValue(8, fileSize);
            insertTrack.bindValue(9, t.albumArtist);

            if (insertTrack.exec() && insertTrack.numRowsAffected() > 0) {
                MusicLibrary::linkTrackToArtistsPrepared(
                    insertTrack.lastInsertId().toLongLong(),
                    t.artist,
                    upsertArtist, findArtistId, linkTrackArtist);
                newTracks.append(t);
            }

            ++processed;
            ++batchInTx;
            if (processed % kProgressEveryN == 0 || processed == total) {
                emit progress(processed, total);
            }

            if (batchInTx >= kBatchSize || batchTimer.elapsed() >= kBatchMaxMs) {
                db.commit();
                emit batchReady();
                db.transaction();
                batchInTx = 0;
                batchTimer.restart();
            }
        }

        db.commit();
        emit batchReady();
        db.close();
    }
    QSqlDatabase::removeDatabase(connName);

    emit finished(newTracks);
}

