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
    if (!m_insertTrack.exec()) {
        m_error = true;
        return false;
    }
    if (m_insertTrack.numRowsAffected() <= 0) return false;
    if (!MusicLibrary::linkTrackToArtistsPrepared(
        m_insertTrack.lastInsertId().toLongLong(),
        track.artist,
        m_upsertArtist, m_findArtistId, m_linkTrackArtist)) {
        m_error = true;
        return false;
    }
    return true;
}

}
