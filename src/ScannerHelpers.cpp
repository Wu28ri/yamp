#include "ScannerHelpers.h"

#include "MusicLibrary.h"
#include "SqlUtils.h"

#include <QDebug>
#include <QSqlError>
#include <QVariant>

namespace ScannerHelpers {

TrackInserter::TrackInserter(QSqlDatabase &db)
    : m_upsertTrack(db), m_findTrackId(db), m_deleteTrackArtists(db),
      m_upsertArtist(db), m_findArtistId(db), m_linkTrackArtist(db) {
    m_upsertTrack.prepare(QStringLiteral(
        "INSERT INTO tracks "
        "(title, artist, album, path, duration, search_text, track_no, tech_info, "
        " file_size, album_artist, file_mtime) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET "
        "title = excluded.title, artist = excluded.artist, album = excluded.album, "
        "duration = excluded.duration, search_text = excluded.search_text, "
        "track_no = excluded.track_no, tech_info = excluded.tech_info, "
        "file_size = excluded.file_size, album_artist = excluded.album_artist, "
        "file_mtime = excluded.file_mtime"));
    m_findTrackId.prepare(QStringLiteral("SELECT id FROM tracks WHERE path = ?"));
    m_deleteTrackArtists.prepare(QStringLiteral("DELETE FROM track_artists WHERE track_id = ?"));
    m_upsertArtist.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO artists (name, name_norm) VALUES (?, ?)"));
    m_findArtistId.prepare(QStringLiteral(
        "SELECT id FROM artists WHERE name_norm = ?"));
    m_linkTrackArtist.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO track_artists (track_id, artist_id) VALUES (?, ?)"));
}

TrackWriteResult TrackInserter::upsert(const Track &track, qint64 fileSize,
                                       qint64 modifiedTime) {
    const QString searchText =
        SqlUtils::normalizeSearch(
            track.title + QLatin1Char(' ') + track.artist + QLatin1Char(' ') + track.album);

    m_findTrackId.bindValue(0, track.path);
    if (!m_findTrackId.exec()) {
        qWarning() << "[TrackInserter] find existing track"
                   << m_findTrackId.lastError().text();
        return TrackWriteResult::Error;
    }
    const bool existed = m_findTrackId.next();
    m_findTrackId.finish();

    m_upsertTrack.bindValue(0, track.title);
    m_upsertTrack.bindValue(1, track.artist);
    m_upsertTrack.bindValue(2, track.album);
    m_upsertTrack.bindValue(3, track.path);
    m_upsertTrack.bindValue(4, track.duration);
    m_upsertTrack.bindValue(5, searchText);
    m_upsertTrack.bindValue(6, track.trackNo);
    m_upsertTrack.bindValue(7, track.techInfo);
    m_upsertTrack.bindValue(8, fileSize);
    m_upsertTrack.bindValue(9, track.albumArtist);
    m_upsertTrack.bindValue(10, modifiedTime);
    if (!m_upsertTrack.exec()) {
        qWarning() << "[TrackInserter] upsert track" << track.path
                   << m_upsertTrack.lastError().text();
        return TrackWriteResult::Error;
    }

    m_findTrackId.bindValue(0, track.path);
    if (!m_findTrackId.exec() || !m_findTrackId.next()) {
        qWarning() << "[TrackInserter] find written track" << track.path
                   << m_findTrackId.lastError().text();
        return TrackWriteResult::Error;
    }
    const qint64 trackId = m_findTrackId.value(0).toLongLong();
    m_findTrackId.finish();

    m_deleteTrackArtists.bindValue(0, trackId);
    if (!m_deleteTrackArtists.exec()) {
        qWarning() << "[TrackInserter] clear track artists" << track.path
                   << m_deleteTrackArtists.lastError().text();
        return TrackWriteResult::Error;
    }
    if (!MusicLibrary::linkTrackToArtistsPrepared(
        trackId, track.artist,
        m_upsertArtist, m_findArtistId, m_linkTrackArtist)) {
        qWarning() << "[TrackInserter] link track artists" << track.path;
        return TrackWriteResult::Error;
    }
    return existed ? TrackWriteResult::Updated : TrackWriteResult::Inserted;
}

}
