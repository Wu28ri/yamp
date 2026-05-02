#include "MusicLibrary.h"

#include <QDebug>
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

#include <taglib/fileref.h>
#include <taglib/flacproperties.h>
#include <taglib/tag.h>

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
        qWarning() << "[MusicLibrary] failed to open DB:" << db.lastError().text();
        return false;
    }

    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA temp_store=MEMORY"));
    pragma.exec(QStringLiteral("PRAGMA cache_size=-32000"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tracks ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "title TEXT, artist TEXT, album TEXT, "
            "path TEXT UNIQUE, duration INTEGER, "
            "search_text TEXT, track_no INTEGER, "
            "tech_info TEXT, file_size INTEGER DEFAULT 0)"))) {
        qWarning() << "[MusicLibrary] schema tracks:" << q.lastError().text();
        return false;
    }

    bool hasFileSize = false;
    if (q.exec(QStringLiteral("PRAGMA table_info(tracks)"))) {
        while (q.next()) {
            if (q.value(1).toString() == QLatin1String("file_size")) { hasFileSize = true; break; }
        }
    }
    if (!hasFileSize) {
        QSqlQuery alterQ(db);
        if (!alterQ.exec(QStringLiteral("ALTER TABLE tracks ADD COLUMN file_size INTEGER DEFAULT 0"))) {
            qWarning() << "[MusicLibrary] add file_size:" << alterQ.lastError().text();
        }
    }

    QSqlQuery wq(db);
    if (!wq.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS watch_roots (path TEXT PRIMARY KEY)"))) {
        qWarning() << "[MusicLibrary] schema watch_roots:" << wq.lastError().text();
        return false;
    }

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS artists ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "name_norm TEXT NOT NULL UNIQUE)"))) {
        qWarning() << "[MusicLibrary] schema artists:" << q.lastError().text();
        return false;
    }
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS track_artists ("
            "track_id INTEGER NOT NULL, "
            "artist_id INTEGER NOT NULL, "
            "PRIMARY KEY (track_id, artist_id))"))) {
        qWarning() << "[MusicLibrary] schema track_artists:" << q.lastError().text();
        return false;
    }
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_track_artists_artist ON track_artists(artist_id)"));
    q.exec(QStringLiteral(
        "CREATE TRIGGER IF NOT EXISTS trk_after_delete AFTER DELETE ON tracks "
        "BEGIN DELETE FROM track_artists WHERE track_id = OLD.id; END"));
    q.exec(QStringLiteral(
        "CREATE TRIGGER IF NOT EXISTS ta_after_delete AFTER DELETE ON track_artists "
        "BEGIN DELETE FROM artists WHERE id = OLD.artist_id "
        "AND NOT EXISTS (SELECT 1 FROM track_artists WHERE artist_id = OLD.artist_id); END"));

    QSqlQuery countQ(db);
    int userVersion = 0;
    if (countQ.exec(QStringLiteral("PRAGMA user_version")) && countQ.next()) {
        userVersion = countQ.value(0).toInt();
    }
    if (userVersion < 1) {
        bool needBackfill = false;
        if (countQ.exec(QStringLiteral("SELECT (SELECT COUNT(*) FROM tracks), (SELECT COUNT(*) FROM track_artists)"))
            && countQ.next()) {
            const int tracksCount = countQ.value(0).toInt();
            const int linksCount  = countQ.value(1).toInt();
            needBackfill = tracksCount > 0 && linksCount == 0;
        }
        if (needBackfill) {
            db.transaction();
            QSqlQuery iter(db);
            if (iter.exec(QStringLiteral("SELECT id, artist FROM tracks"))) {
                while (iter.next()) {
                    linkTrackToArtists(db, iter.value(0).toLongLong(), iter.value(1).toString());
                }
            }
            db.commit();
        }
        countQ.exec(QStringLiteral("PRAGMA user_version = 1"));
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

void linkTrackToArtists(QSqlDatabase &db, qint64 trackId, const QString &rawArtists) {
    if (trackId <= 0) return;
    const QStringList names = splitArtists(rawArtists);

    QSqlQuery upsert(db);
    upsert.prepare(QStringLiteral("INSERT OR IGNORE INTO artists (name, name_norm) VALUES (?, ?)"));
    QSqlQuery findId(db);
    findId.prepare(QStringLiteral("SELECT id FROM artists WHERE name_norm = ?"));
    QSqlQuery link(db);
    link.prepare(QStringLiteral("INSERT OR IGNORE INTO track_artists (track_id, artist_id) VALUES (?, ?)"));

    for (const QString &display : names) {
        const QString norm = normalizeArtistName(display);
        if (norm.isEmpty()) continue;

        upsert.bindValue(0, display);
        upsert.bindValue(1, norm);
        if (!upsert.exec()) {
            qWarning() << "[MusicLibrary] upsert artist:" << upsert.lastError().text();
            continue;
        }

        findId.bindValue(0, norm);
        if (!findId.exec() || !findId.next()) continue;
        const qint64 artistId = findId.value(0).toLongLong();

        link.bindValue(0, trackId);
        link.bindValue(1, artistId);
        if (!link.exec()) {
            qWarning() << "[MusicLibrary] link track-artist:" << link.lastError().text();
        }
    }
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
        if (!upsertArtist.exec()) {
            qWarning() << "[MusicLibrary] upsert artist:" << upsertArtist.lastError().text();
            continue;
        }

        findArtistId.bindValue(0, norm);
        if (!findArtistId.exec() || !findArtistId.next()) continue;
        const qint64 artistId = findArtistId.value(0).toLongLong();
        findArtistId.finish();

        linkTrackArtist.bindValue(0, trackId);
        linkTrackArtist.bindValue(1, artistId);
        if (!linkTrackArtist.exec()) {
            qWarning() << "[MusicLibrary] link track-artist:" << linkTrackArtist.lastError().text();
        }
    }
}

}

LibraryScanner::LibraryScanner(QString rootPath, QObject *parent)
    : QObject(parent), m_rootPath(std::move(rootPath)) {}

QString LibraryScanner::makeTechInfo(const QString &filePath, int sampleRate, int bitrate, int bitDepth) {
    const double khz = sampleRate / 1000.0;
    QString prefix;
    if (filePath.endsWith(QLatin1String(".flac"), Qt::CaseInsensitive)) prefix = QStringLiteral("FLAC | ");
    else if (filePath.endsWith(QLatin1String(".mp3"), Qt::CaseInsensitive)) prefix = QStringLiteral("MP3 | ");
    QString out = QStringLiteral("%1%2 kHz").arg(prefix).arg(khz, 0, 'f', 1);
    if (bitDepth > 0) out += QStringLiteral(" | %1 bit").arg(bitDepth);
    out += QStringLiteral(" | %1 kbps").arg(bitrate);
    return out;
}

void LibraryScanner::run() {
    if (m_rootPath.isEmpty()) {
        emit countDetermined(0);
        emit finished({});
        return;
    }

    QStringList allFiles;
    {
        QDirIterator it(m_rootPath,
                        {QStringLiteral("*.flac"), QStringLiteral("*.mp3")},
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
            qWarning() << "[Scan] failed to open DB:" << db.lastError().text();
            QSqlDatabase::removeDatabase(connName);
            emit finished({});
            return;
        }

        QSqlQuery pragma(db);
        pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
        pragma.exec(QStringLiteral("PRAGMA temp_store=MEMORY"));
        pragma.exec(QStringLiteral("PRAGMA cache_size=-32000"));

        QSqlQuery insertTrack(db);
        insertTrack.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO tracks "
            "(title, artist, album, path, duration, search_text, track_no, tech_info, file_size) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));

        QSqlQuery upsertArtist(db);
        upsertArtist.prepare(QStringLiteral("INSERT OR IGNORE INTO artists (name, name_norm) VALUES (?, ?)"));
        QSqlQuery findArtistId(db);
        findArtistId.prepare(QStringLiteral("SELECT id FROM artists WHERE name_norm = ?"));
        QSqlQuery linkTrackArtist(db);
        linkTrackArtist.prepare(QStringLiteral("INSERT OR IGNORE INTO track_artists (track_id, artist_id) VALUES (?, ?)"));

        if (!db.transaction()) {
            qWarning() << "[Scan] BEGIN failed:" << db.lastError().text();
        }

        QElapsedTimer batchTimer;
        batchTimer.start();
        int batchInTx = 0;
        int processed = 0;
        const int total = allFiles.size();

        for (const QString &filePath : allFiles) {
            const QByteArray pathBytes = filePath.toUtf8();
            TagLib::FileRef f(pathBytes.constData());

            QFileInfo info(filePath);
            QString title  = info.fileName();
            QString artist = QStringLiteral("Unknown Artist");
            QString album  = QStringLiteral("Unknown Album");
            QString techInfo;
            int duration = 0;
            int trackNo  = 0;
            const qint64 fileSize = info.size();

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
                if (auto *audio = f.audioProperties()) {
                    duration = audio->lengthInSeconds();
                    int bitDepth = 0;
                    if (auto *flac = dynamic_cast<TagLib::FLAC::Properties*>(audio)) bitDepth = flac->bitsPerSample();
                    techInfo = makeTechInfo(filePath, audio->sampleRate(), audio->bitrate(), bitDepth);
                }
            }

            const QString searchText = (title + QLatin1Char(' ') + artist + QLatin1Char(' ') + album).toLower();

            insertTrack.bindValue(0, title);
            insertTrack.bindValue(1, artist);
            insertTrack.bindValue(2, album);
            insertTrack.bindValue(3, filePath);
            insertTrack.bindValue(4, duration);
            insertTrack.bindValue(5, searchText);
            insertTrack.bindValue(6, trackNo);
            insertTrack.bindValue(7, techInfo);
            insertTrack.bindValue(8, fileSize);

            if (insertTrack.exec() && insertTrack.numRowsAffected() > 0) {
                MusicLibrary::linkTrackToArtistsPrepared(
                    insertTrack.lastInsertId().toLongLong(),
                    artist,
                    upsertArtist, findArtistId, linkTrackArtist);
                newTracks.append({filePath, title, artist, album, duration, techInfo, trackNo});
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
