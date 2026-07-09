#include "ScannerHelpers.h"

#include "MusicLibrary.h"

#include <QVariant>

namespace ScannerHelpers {

TrackInserter::TrackInserter(QSqlDatabase &db)
    : m_insertTrack(db), m_upsertArtist(db), m_findArtistId(db), m_linkTrackArtist(db) {
    m_insertTrack.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO tracks "
        "(title, artist, album, path, duration, search_text, track_no, tech_info, "
        " file_size, album_artist) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    m_upsertArtist.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO artists (name, name_norm) VALUES (?, ?)"));
    m_findArtistId.prepare(QStringLiteral(
        "SELECT id FROM artists WHERE name_norm = ?"));
    m_linkTrackArtist.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO track_artists (track_id, artist_id) VALUES (?, ?)"));
}

bool TrackInserter::insert(const Track &track, qint64 fileSize) {
    const QString searchText =
        (track.title + QLatin1Char(' ') + track.artist + QLatin1Char(' ') + track.album).toLower();
    m_insertTrack.bindValue(0, track.title);
    m_insertTrack.bindValue(1, track.artist);
    m_insertTrack.bindValue(2, track.album);
    m_insertTrack.bindValue(3, track.path);
    m_insertTrack.bindValue(4, track.duration);
    m_insertTrack.bindValue(5, searchText);
    m_insertTrack.bindValue(6, track.trackNo);
    m_insertTrack.bindValue(7, track.techInfo);
    m_insertTrack.bindValue(8, fileSize);
    m_insertTrack.bindValue(9, track.albumArtist);
    if (!m_insertTrack.exec() || m_insertTrack.numRowsAffected() <= 0) return false;
    MusicLibrary::linkTrackToArtistsPrepared(
        m_insertTrack.lastInsertId().toLongLong(),
        track.artist,
        m_upsertArtist, m_findArtistId, m_linkTrackArtist);
    return true;
}

InsertOutcome insertTracksFromPaths(QSqlDatabase &db, const QStringList &paths) {
    InsertOutcome out;
    if (paths.isEmpty()) return out;

    TrackInserter inserter(db);
    db.transaction();
    for (const QString &path : paths) {
        Track t;
        qint64 fileSize = 0;
        if (!MusicLibrary::readTrackFromFile(path, t, fileSize)) continue;
        if (inserter.insert(t, fileSize)) {
            ++out.inserted;
            out.tracks.append(t);
        }
    }
    db.commit();
    return out;
}

int deleteTracksByPath(QSqlDatabase &db, const QStringList &paths) {
    if (paths.isEmpty()) return 0;
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM tracks WHERE path = ?"));

    db.transaction();
    int removed = 0;
    for (const QString &p : paths) {
        del.bindValue(0, p);
        if (del.exec()) removed += del.numRowsAffected();
    }
    db.commit();
    if (removed > 0) MusicLibrary::pruneOrphanArtists(db);
    return removed;
}

bool updateTrackPath(QSqlDatabase &db, const QString &oldPath, const QString &newPath) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE tracks SET path = ? WHERE path = ?"));
    q.addBindValue(newPath);
    q.addBindValue(oldPath);
    if (!q.exec()) return false;
    return q.numRowsAffected() > 0;
}

}
